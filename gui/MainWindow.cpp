#include "MainWindow.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include "Document.h"
#include "BedrockDatabaseDialog.h"
#include "NbtTreeModel.h"

extern "C" {
#include "nbt_builder.h"
#include "nbt_json.h"
#include "nbt_tree.h"
#include "region_file.h"
#include "region_read.h"
}

namespace {

QString jsonQuote(const QString& value) {
    QString escaped;
    escaped.reserve(value.size() + 2);
    escaped += QLatin1Char('"');
    for (QChar c : value) {
        switch (c.unicode()) {
            case '"': escaped += QStringLiteral("\\\""); break;
            case '\\': escaped += QStringLiteral("\\\\"); break;
            case '\b': escaped += QStringLiteral("\\b"); break;
            case '\f': escaped += QStringLiteral("\\f"); break;
            case '\n': escaped += QStringLiteral("\\n"); break;
            case '\r': escaped += QStringLiteral("\\r"); break;
            case '\t': escaped += QStringLiteral("\\t"); break;
            default:
                if (c.unicode() < 0x20) {
                    escaped += QStringLiteral("\\u%1").arg(
                        static_cast<unsigned int>(c.unicode()), 4, 16, QLatin1Char('0'));
                }
                else escaped += c;
                break;
        }
    }
    escaped += QLatin1Char('"');
    return escaped;
}

QString editExpression(const NBTTag* tag) {
    if (!tag) return {};
    switch (tag->type) {
        case TAG_Byte: return QString::number(tag->value.byte_val);
        case TAG_Short: return QString::number(tag->value.short_val);
        case TAG_Int: return QString::number(tag->value.int_val);
        case TAG_Long: return QString::number(tag->value.long_val);
        case TAG_Float: return QString::number(tag->value.float_val, 'g', 9);
        case TAG_Double: return QString::number(tag->value.double_val, 'g', 17);
        case TAG_String: return jsonQuote(QString::fromUtf8(tag->value.string_val ? tag->value.string_val : ""));
        case TAG_Byte_Array: {
            QStringList values;
            values.reserve(tag->value.byte_array.length);
            for (int i = 0; i < tag->value.byte_array.length; ++i) {
                values << QString::number(static_cast<qint8>(tag->value.byte_array.data[i]));
            }
            return QLatin1Char('[') + values.join(QLatin1Char(',')) + QLatin1Char(']');
        }
        case TAG_Int_Array: {
            QStringList values;
            values.reserve(tag->value.int_array.length);
            for (int i = 0; i < tag->value.int_array.length; ++i) {
                values << QString::number(tag->value.int_array.data[i]);
            }
            return QLatin1Char('[') + values.join(QLatin1Char(',')) + QLatin1Char(']');
        }
        case TAG_Long_Array: {
            QStringList values;
            values.reserve(tag->value.long_array.length);
            for (int i = 0; i < tag->value.long_array.length; ++i) {
                values << QString::number(tag->value.long_array.data[i]);
            }
            return QLatin1Char('[') + values.join(QLatin1Char(',')) + QLatin1Char(']');
        }
        case TAG_List: {
            QStringList values;
            values.reserve(tag->value.list.count);
            for (int i = 0; i < tag->value.list.count; ++i) {
                const NBTTag* child = tag->value.list.items[i];
                if (child->type == TAG_Compound || child->type == TAG_List) return {};
                values << editExpression(child);
            }
            return QLatin1Char('[') + values.join(QLatin1Char(',')) + QLatin1Char(']');
        }
        default: return {};
    }
}

bool editLargeText(QWidget* parent, const QString& title, const QString& label, QString* value) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(680, 430);
    auto* layout = new QVBoxLayout(&dialog);
    auto* description = new QLabel(label, &dialog);
    description->setWordWrap(true);
    auto* editor = new QPlainTextEdit(&dialog);
    editor->setPlainText(*value);
    editor->setTabChangesFocus(true);
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(description);
    layout->addWidget(editor, 1);
    layout->addWidget(buttons);
    editor->selectAll();
    editor->setFocus();
    if (dialog.exec() != QDialog::Accepted) return false;
    *value = editor->toPlainText();
    return true;
}

bool isContainer(const NBTTag* tag) {
    return tag && (tag->type == TAG_Compound || tag->type == TAG_List);
}

QString compressionName(uint8_t compression) {
    switch (compression) {
        case REGION_COMPRESSION_GZIP: return QObject::tr("gzip");
        case REGION_COMPRESSION_ZLIB: return QObject::tr("zlib");
        case REGION_COMPRESSION_NONE: return QObject::tr("uncompressed");
        case REGION_COMPRESSION_LZ4: return QObject::tr("LZ4");
        default: return QObject::tr("unknown");
    }
}

}  // namespace

DocumentView::DocumentView(NbtDocument* document, QWidget* parent)
    : QWidget(parent), document_(document) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search names, types, and values…"));
    search_->setClearButtonEnabled(true);
    search_->setProperty("searchField", true);

    model_ = new NbtTreeModel(document_, this);
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy_->setFilterKeyColumn(-1);
    proxy_->setRecursiveFilteringEnabled(true);
    proxy_->setAutoAcceptChildRows(true);

    tree_ = new QTreeView(this);
    tree_->setModel(proxy_);
    tree_->setAlternatingRowColors(true);
    tree_->setAnimated(true);
    tree_->setUniformRowHeights(true);
    tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setDragEnabled(true);
    tree_->setAcceptDrops(true);
    tree_->setDropIndicatorShown(true);
    tree_->setDragDropMode(QAbstractItemView::InternalMove);
    tree_->setDefaultDropAction(Qt::MoveAction);
    tree_->header()->setStretchLastSection(true);
    tree_->header()->resizeSection(0, 330);
    tree_->header()->resizeSection(1, 135);
    tree_->expandToDepth(1);

    connect(search_, &QLineEdit::textChanged, proxy_, &QSortFilterProxyModel::setFilterFixedString);
    connect(search_, &QLineEdit::returnPressed, this, [this] {
        if (proxy_->rowCount() > 0) {
            const QModelIndex first = proxy_->index(0, 0);
            tree_->setCurrentIndex(first);
            tree_->scrollTo(first);
        }
    });

    layout->addWidget(search_);
    layout->addWidget(tree_, 1);
}

QModelIndex DocumentView::selectedSourceIndex() const {
    const QModelIndex selected = tree_->currentIndex();
    return selected.isValid() ? proxy_->mapToSource(selected) : QModelIndex();
}

void DocumentView::focusSearch() {
    search_->setFocus();
    search_->selectAll();
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("C-NBT Explorer"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/cnbt-explorer.svg")));
    resize(1080, 720);
    setMinimumSize(720, 480);
    setAcceptDrops(true);

    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setUsesScrollButtons(true);
    setCentralWidget(tabs_);

    createActions();
    createMenusAndToolbars();

    connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::updateActions);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        DocumentView* view = static_cast<DocumentView*>(tabs_->widget(index));
        if (view && maybeSave(view)) tabs_->removeTab(index), view->deleteLater();
    });

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("mainWindow/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("mainWindow/state")).toByteArray());
    statusBar()->showMessage(tr("Open a Java or Bedrock NBT file, SNBT document, or region file."));
    updateActions();
}

MainWindow::~MainWindow() {
    free_nbt_tree(clipboardTag_);
}

void MainWindow::createActions() {
    auto* newJavaAction = new QAction(tr("New &Java NBT"), this);
    newJavaAction->setShortcut(QKeySequence::New);
    newJavaAction->setIcon(QIcon::fromTheme(QStringLiteral("document-new")));
    connect(newJavaAction, &QAction::triggered, this, [this] {
        createNewDocument(NBT_BINARY_JAVA);
    });

    auto* newBedrockAction = new QAction(tr("New &Bedrock NBT"), this);
    connect(newBedrockAction, &QAction::triggered, this, [this] {
        createNewDocument(NBT_BINARY_BEDROCK);
    });

    auto* newBedrockLevelAction = new QAction(tr("New Bedrock &level.dat"), this);
    connect(newBedrockLevelAction, &QAction::triggered, this, [this] {
        createNewDocument(NBT_BINARY_BEDROCK_LEVEL_DAT);
    });

    auto* newSnbtAction = new QAction(tr("New &SNBT Document"), this);
    connect(newSnbtAction, &QAction::triggered, this, [this] {
        createNewDocument(NBT_BINARY_JAVA, true);
    });

    auto* openAction = new QAction(tr("&Open…"), this);
    openAction->setShortcut(QKeySequence::Open);
    openAction->setIcon(QIcon::fromTheme(QStringLiteral("document-open")));
    connect(openAction, &QAction::triggered, this, &MainWindow::openFiles);
    addAction(openAction);

    auto* openDatabaseAction = new QAction(tr("Open Bedrock World &Database…"), this);
    connect(openDatabaseAction, &QAction::triggered, this, &MainWindow::openBedrockDatabase);

    saveAction_ = new QAction(tr("&Save"), this);
    saveAction_->setShortcut(QKeySequence::Save);
    saveAction_->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveCurrent);

    saveAsAction_ = new QAction(tr("Save &As…"), this);
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, &MainWindow::saveCurrentAs);

    exportJsonAction_ = new QAction(tr("Export Typed &JSON…"), this);
    connect(exportJsonAction_, &QAction::triggered, this, &MainWindow::exportCurrentJson);

    exportSnbtAction_ = new QAction(tr("Export &SNBT…"), this);
    connect(exportSnbtAction_, &QAction::triggered, this, &MainWindow::exportCurrentSnbt);

    closeAction_ = new QAction(tr("&Close Tab"), this);
    closeAction_->setShortcut(QKeySequence::Close);
    connect(closeAction_, &QAction::triggered, this, &MainWindow::closeCurrentTab);

    auto* quitAction = new QAction(tr("&Quit"), this);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    undoAction_ = new QAction(tr("&Undo"), this);
    undoAction_->setShortcut(QKeySequence::Undo);
    undoAction_->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));
    redoAction_ = new QAction(tr("&Redo"), this);
    redoAction_->setShortcut(QKeySequence::Redo);
    redoAction_->setIcon(QIcon::fromTheme(QStringLiteral("edit-redo")));

    editAction_ = new QAction(tr("Edit &Value…"), this);
    editAction_->setShortcut(Qt::Key_Return);
    connect(editAction_, &QAction::triggered, this, &MainWindow::editSelected);

    renameAction_ = new QAction(tr("&Rename…"), this);
    renameAction_->setShortcut(Qt::Key_F2);
    connect(renameAction_, &QAction::triggered, this, &MainWindow::renameSelected);

    addAction_ = new QAction(tr("&Add Tag…"), this);
    addAction_->setShortcut(Qt::Key_Insert);
    addAction_->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    connect(addAction_, &QAction::triggered, this, &MainWindow::addTag);

    duplicateAction_ = new QAction(tr("D&uplicate"), this);
    duplicateAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(duplicateAction_, &QAction::triggered, this, &MainWindow::duplicateSelected);

    deleteAction_ = new QAction(tr("&Delete"), this);
    deleteAction_->setShortcut(QKeySequence::Delete);
    deleteAction_->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
    connect(deleteAction_, &QAction::triggered, this, &MainWindow::deleteSelected);

    copyAction_ = new QAction(tr("&Copy Tag"), this);
    copyAction_->setShortcut(QKeySequence::Copy);
    connect(copyAction_, &QAction::triggered, this, &MainWindow::copySelected);

    cutAction_ = new QAction(tr("Cu&t Tag"), this);
    cutAction_->setShortcut(QKeySequence::Cut);
    connect(cutAction_, &QAction::triggered, this, &MainWindow::cutSelected);

    pasteAction_ = new QAction(tr("&Paste Tag"), this);
    pasteAction_->setShortcut(QKeySequence::Paste);
    connect(pasteAction_, &QAction::triggered, this, &MainWindow::pasteTag);

    chunkAction_ = new QAction(tr("Choose Region &Chunk…"), this);
    connect(chunkAction_, &QAction::triggered, this, &MainWindow::chooseRegionChunk);

    backupAction_ = new QAction(tr("Create &Backup Before Saving"), this);
    backupAction_->setCheckable(true);
    backupAction_->setChecked(
        QSettings().value(QStringLiteral("files/backupOnSave"), true).toBool());
    connect(backupAction_, &QAction::toggled, this, [this](bool enabled) {
        QSettings().setValue(QStringLiteral("files/backupOnSave"), enabled);
        for (int index = 0; index < tabs_->count(); ++index) {
            auto* view = static_cast<DocumentView*>(tabs_->widget(index));
            if (view) view->document()->setBackupOnSave(enabled);
        }
    });

    expandAction_ = new QAction(tr("Expand All"), this);
    expandAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    connect(expandAction_, &QAction::triggered, this, [this] {
        if (auto* view = currentView()) view->tree()->expandAll();
    });
    collapseAction_ = new QAction(tr("Collapse All"), this);
    connect(collapseAction_, &QAction::triggered, this, [this] {
        if (auto* view = currentView()) view->tree()->collapseAll();
    });

    auto* findAction = new QAction(tr("&Find"), this);
    findAction->setShortcut(QKeySequence::Find);
    connect(findAction, &QAction::triggered, this, [this] {
        if (auto* view = currentView()) view->focusSearch();
    });
    addAction(findAction);

    auto* aboutAction = new QAction(tr("&About C-NBT Explorer"), this);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    auto* file = menuBar()->addMenu(tr("&File"));
    auto* newMenu = file->addMenu(tr("&New"));
    newMenu->addAction(newJavaAction);
    newMenu->addAction(newBedrockAction);
    newMenu->addAction(newBedrockLevelAction);
    newMenu->addAction(newSnbtAction);
    file->addAction(openAction);
    file->addAction(openDatabaseAction);
    file->addAction(saveAction_);
    file->addAction(saveAsAction_);
    file->addSeparator();
    file->addAction(exportJsonAction_);
    file->addAction(exportSnbtAction_);
    file->addSeparator();
    file->addAction(closeAction_);
    file->addAction(quitAction);

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(undoAction_);
    edit->addAction(redoAction_);
    edit->addSeparator();
    edit->addAction(cutAction_);
    edit->addAction(copyAction_);
    edit->addAction(pasteAction_);
    edit->addSeparator();
    edit->addAction(editAction_);
    edit->addAction(renameAction_);
    edit->addAction(addAction_);
    edit->addAction(duplicateAction_);
    edit->addAction(deleteAction_);

    auto* view = menuBar()->addMenu(tr("&View"));
    view->addAction(findAction);
    view->addSeparator();
    view->addAction(expandAction_);
    view->addAction(collapseAction_);

    auto* tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(chunkAction_);
    tools->addSeparator();
    tools->addAction(backupAction_);

    auto* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(aboutAction);

    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->setMovable(false);
    toolbar->addAction(newJavaAction);
    toolbar->addAction(openAction);
    toolbar->addAction(saveAction_);
    toolbar->addSeparator();
    toolbar->addAction(undoAction_);
    toolbar->addAction(redoAction_);
    toolbar->addSeparator();
    toolbar->addAction(addAction_);
    toolbar->addAction(deleteAction_);
}

void MainWindow::createMenusAndToolbars() {
    // Menus and the primary toolbar are assembled with the actions so the
    // platform-native menu roles are available as soon as the window opens.
}

DocumentView* MainWindow::currentView() const {
    return static_cast<DocumentView*>(tabs_->currentWidget());
}

DocumentView* MainWindow::addDocumentTab(NbtDocument* document, const QString& tooltip) {
    if (!document) return nullptr;
    auto* view = new DocumentView(document, tabs_);
    document->setParent(view);
    document->setBackupOnSave(backupAction_->isChecked());
    const int index = tabs_->addTab(view, document->displayName());
    tabs_->setTabToolTip(index, tooltip.isEmpty() ? document->formatDescription() : tooltip);
    tabs_->setCurrentIndex(index);

    connect(document, &NbtDocument::titleChanged, this, [this, view, document] {
        const int tab = tabs_->indexOf(view);
        if (tab >= 0) {
            tabs_->setTabText(tab, document->displayName());
            const QString path = document->filePath();
            tabs_->setTabToolTip(tab, path.isEmpty()
                ? document->formatDescription()
                : path + QLatin1Char('\n') + document->formatDescription());
        }
        updateActions();
    });
    connect(document, &NbtDocument::statusMessage, this, [this](const QString& message) {
        statusBar()->showMessage(message, 6000);
    });
    connect(view->model(), &NbtTreeModel::operationError, this, [this](const QString& message) {
        showError(tr("Could Not Move Tag"), message);
    });
    connect(view->tree(), &QTreeView::doubleClicked, this, [this](const QModelIndex&) {
        editSelected();
    });
    connect(view->tree()->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::updateActions);

    view->tree()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view->tree(), &QTreeView::customContextMenuRequested, this,
            [this, view](const QPoint& position) {
        tabs_->setCurrentWidget(view);
        const QModelIndex clicked = view->tree()->indexAt(position);
        if (clicked.isValid()) view->tree()->setCurrentIndex(clicked);
        updateActions();

        QMenu menu(view->tree());
        menu.addAction(editAction_);
        menu.addAction(renameAction_);
        menu.addAction(addAction_);
        menu.addAction(duplicateAction_);
        menu.addAction(deleteAction_);
        menu.addSeparator();
        menu.addAction(cutAction_);
        menu.addAction(copyAction_);
        menu.addAction(pasteAction_);
        menu.exec(view->tree()->viewport()->mapToGlobal(position));
    });
    updateActions();
    return view;
}

void MainWindow::createNewDocument(int format, bool snbt) {
    auto* document = new NbtDocument();
    QString error;
    if (!document->createNew(static_cast<NBTBinaryFormat>(format), snbt, &error)) {
        delete document;
        showError(tr("Could Not Create Document"), error);
        return;
    }
    addDocumentTab(document);
}

void MainWindow::openFiles() {
    QSettings settings;
    const QString start = settings.value(QStringLiteral("files/lastDirectory")).toString();
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        tr("Open Minecraft NBT Data"),
        start,
        tr("Minecraft NBT (*.dat *.dat_old *.nbt *.mca *.mcr *.schematic *.schem *.snbt *.litematic *.mcstructure);;All files (*)")
    );
    if (paths.isEmpty()) return;
    settings.setValue(QStringLiteral("files/lastDirectory"), QFileInfo(paths.first()).absolutePath());
    openPaths(paths);
}

void MainWindow::openPaths(const QStringList& paths) {
    for (const QString& path : paths) {
        if (QFileInfo(path).isDir()) openBedrockDatabaseAt(path);
        else openOne(path);
    }
}

void MainWindow::openBedrockDatabase() {
    QSettings settings;
    const QString start = settings.value(QStringLiteral("files/lastBedrockWorldDirectory")).toString();
    const QString path = QFileDialog::getExistingDirectory(
        this,
        tr("Choose a Bedrock World or db Directory"),
        start,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (path.isEmpty()) return;
    settings.setValue(QStringLiteral("files/lastBedrockWorldDirectory"), path);
    openBedrockDatabaseAt(path);
}

bool MainWindow::openBedrockDatabaseAt(const QString& path) {
    QDir selected(path);
    QString databaseDirectory;
    if (QFileInfo(selected.filePath(QStringLiteral("CURRENT"))).isFile()) {
        databaseDirectory = selected.absolutePath();
    } else if (QFileInfo(selected.filePath(QStringLiteral("db/CURRENT"))).isFile()) {
        databaseDirectory = QDir(selected.filePath(QStringLiteral("db"))).absolutePath();
    } else {
        showError(
            tr("Not a Bedrock World Database"),
            tr("Choose a Bedrock world folder containing db/CURRENT, or choose its db folder directly."));
        return false;
    }

    QByteArray key;
    QString keyLabel;
    QString error;
    if (!BedrockDatabaseDialog::chooseRecord(
            this, databaseDirectory, &key, &keyLabel, &error)) {
        if (!error.isEmpty()) showError(tr("Could Not Browse Bedrock Database"), error);
        return false;
    }

    auto* document = new NbtDocument();
    if (!document->openBedrockDatabaseRecord(databaseDirectory, key, keyLabel, &error)) {
        delete document;
        showError(tr("Could Not Open Bedrock Record"), error);
        return false;
    }
    addDocumentTab(
        document,
        databaseDirectory + QLatin1Char('\n') + keyLabel + QLatin1Char('\n') +
            document->formatDescription());
    return true;
}

bool MainWindow::chooseChunk(const QString& path, int* chunkX, int* chunkZ) {
    const QByteArray encoded = path.toUtf8();
    char error[512]{};
    RegionFile* region = region_file_read(encoded.constData(), error, sizeof(error));
    if (!region) {
        showError(tr("Could Not Open Region"), QString::fromUtf8(error));
        return false;
    }

    QStringList labels;
    QVector<QPair<int, int>> coordinates;
    for (int i = 0; i < REGION_CHUNK_COUNT; ++i) {
        const RegionChunkSlot& slot = region->chunks[i];
        if (!slot.present) continue;
        int x = 0;
        int z = 0;
        region_chunk_coords(i, &x, &z);
        coordinates.append({x, z});
        labels.append(tr("Local (%1, %2) — %3%4")
            .arg(x)
            .arg(z)
            .arg(compressionName(slot.compression_type))
            .arg(slot.external ? tr(", external") : QString()));
    }
    region_file_free(region);
    if (labels.isEmpty()) {
        showError(tr("Empty Region"), tr("This region file contains no populated chunks."));
        return false;
    }

    bool accepted = false;
    const QString selected = QInputDialog::getItem(
        this, tr("Choose Region Chunk"), tr("Populated chunks:"), labels, 0, false, &accepted
    );
    if (!accepted) return false;
    const int chosen = labels.indexOf(selected);
    if (chosen < 0) return false;
    *chunkX = coordinates[chosen].first;
    *chunkZ = coordinates[chosen].second;
    return true;
}

bool MainWindow::openOne(const QString& path) {
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < tabs_->count(); ++i) {
        auto* existing = static_cast<DocumentView*>(tabs_->widget(i));
        if (existing && QFileInfo(existing->document()->filePath()) == QFileInfo(absolutePath)) {
            tabs_->setCurrentIndex(i);
            return true;
        }
    }

    int chunkX = -1;
    int chunkZ = -1;
    const QByteArray encoded = absolutePath.toUtf8();
    if (region_path_has_extension(encoded.constData()) && !chooseChunk(absolutePath, &chunkX, &chunkZ)) {
        return false;
    }

    auto* document = new NbtDocument();
    QString error;
    if (!document->openFile(absolutePath, &error, chunkX, chunkZ)) {
        document->deleteLater();
        showError(tr("Could Not Open File"), error);
        return false;
    }

    addDocumentTab(document, absolutePath + QLatin1Char('\n') + document->formatDescription());
    return true;
}

bool MainWindow::maybeSave(DocumentView* view) {
    if (!view || !view->document()->isModified()) return true;
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this,
        tr("Unsaved Changes"),
        tr("Save changes to %1?").arg(view->document()->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save
    );
    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::Discard) return true;
    return saveView(view, false);
}

bool MainWindow::saveView(DocumentView* view, bool saveAs) {
    if (!view) return false;
    QString path = view->document()->filePath();
    const bool needsDestination = saveAs ||
        (path.isEmpty() && !view->document()->isBedrockDatabaseRecord());
    if (needsDestination) {
        QString filter;
        if (view->document()->isRegion()) {
            filter = tr("Minecraft region (*.mca *.mcr);;All files (*)");
        } else if (view->document()->isSnbt()) {
            filter = tr("Stringified NBT (*.snbt);;NBT data (*.dat *.nbt);;All files (*)");
        } else {
            filter = tr("NBT data (*.dat *.nbt *.schematic *.schem *.litematic *.mcstructure);;Stringified NBT (*.snbt);;All files (*)");
        }
        if (path.isEmpty()) {
            if (view->document()->isSnbt()) path = QStringLiteral("Untitled.snbt");
            else if (view->document()->binaryFormat() == NBT_BINARY_BEDROCK_LEVEL_DAT)
                path = QStringLiteral("level.dat");
            else path = QStringLiteral("Untitled.nbt");
        }
        path = QFileDialog::getSaveFileName(
            this,
            tr("Save NBT Data"),
            path,
            filter
        );
        if (path.isEmpty()) return false;
    }
    QString error;
    const bool ok = needsDestination
        ? view->document()->saveAs(path, &error)
        : view->document()->save(&error);
    if (!ok) showError(tr("Could Not Save File"), error);
    return ok;
}

void MainWindow::saveCurrent() { saveView(currentView(), false); }
void MainWindow::saveCurrentAs() { saveView(currentView(), true); }

void MainWindow::exportCurrentJson() {
    auto* view = currentView();
    if (!view) return;
    const QString suggested = QFileInfo(view->document()->filePath()).completeBaseName() + QStringLiteral(".json");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Typed JSON"), suggested, tr("JSON document (*.json)")
    );
    if (path.isEmpty()) return;
    QString error;
    if (!view->document()->exportJson(path, &error)) showError(tr("Could Not Export JSON"), error);
    else statusBar()->showMessage(tr("Exported %1").arg(QFileInfo(path).fileName()), 5000);
}

void MainWindow::exportCurrentSnbt() {
    auto* view = currentView();
    if (!view) return;
    const QString suggested = QFileInfo(view->document()->filePath()).completeBaseName() + QStringLiteral(".snbt");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export SNBT"), suggested, tr("Stringified NBT (*.snbt)")
    );
    if (path.isEmpty()) return;
    QString error;
    if (!view->document()->exportSnbt(path, &error)) showError(tr("Could Not Export SNBT"), error);
    else statusBar()->showMessage(tr("Exported %1").arg(QFileInfo(path).fileName()), 5000);
}

void MainWindow::closeCurrentTab() {
    const int index = tabs_->currentIndex();
    auto* view = currentView();
    if (index >= 0 && view && maybeSave(view)) {
        tabs_->removeTab(index);
        view->deleteLater();
    }
}

void MainWindow::editSelected() {
    auto* view = currentView();
    if (!view) return;
    const QModelIndex index = view->selectedSourceIndex();
    NBTTag* tag = view->model()->tagForIndex(index);
    if (!tag || tag->type == TAG_Compound || tag->type == TAG_End) return;
    QString value = editExpression(tag);
    if (value.isNull() || (tag->type == TAG_List && value.isEmpty())) {
        showError(tr("Unsupported Direct Edit"),
                  tr("Lists containing compounds or nested lists are edited through their child tags."));
        return;
    }
    if (!editLargeText(
            this,
            tr("Edit %1").arg(QString::fromUtf8(tag->name && tag->name[0] ? tag->name : "value")),
            tr("Enter a JSON value. Numeric NBT types retain their current type."),
            &value)) return;
    QString error;
    if (!view->document()->editTag(tag, value, &error)) showError(tr("Could Not Edit Tag"), error);
}

void MainWindow::renameSelected() {
    auto* view = currentView();
    if (!view) return;
    const QModelIndex index = view->selectedSourceIndex();
    NBTTag* tag = view->model()->tagForIndex(index);
    NBTTag* parent = view->model()->parentTagForIndex(index);
    if (!tag || !parent || parent->type != TAG_Compound) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, tr("Rename Tag"), tr("Tag name:"), QLineEdit::Normal,
        QString::fromUtf8(tag->name), &accepted
    );
    if (!accepted) return;
    QString error;
    if (!view->document()->renameTag(tag, parent, name, &error)) showError(tr("Could Not Rename Tag"), error);
}

void MainWindow::addTag() {
    auto* view = currentView();
    if (!view) return;
    const QModelIndex index = view->selectedSourceIndex();
    NBTTag* selected = view->model()->tagForIndex(index);
    NBTTag* parent = isContainer(selected) ? selected : view->model()->parentTagForIndex(index);
    if (!isContainer(parent)) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add NBT Tag"));
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(&dialog);
    name->setEnabled(parent->type == TAG_Compound);
    auto* type = new QComboBox(&dialog);
    for (int value = TAG_Byte; value <= TAG_Long_Array; ++value) {
        type->addItem(QString::fromLatin1(nbt_tag_type_name(static_cast<TagType>(value))), value);
    }
    if (parent->type == TAG_List && parent->value.list.element_type != TAG_End) {
        const int required = type->findData(static_cast<int>(parent->value.list.element_type));
        type->setCurrentIndex(required);
        type->setEnabled(false);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(tr("Name:"), name);
    form->addRow(tr("Type:"), type);
    form->addRow(buttons);
    if (parent->type == TAG_Compound) name->setFocus();
    if (dialog.exec() != QDialog::Accepted) return;

    QString error;
    const TagType selectedType = static_cast<TagType>(type->currentData().toInt());
    if (!view->document()->addTag(parent, selectedType, name->text(), &error)) {
        showError(tr("Could Not Add Tag"), error);
    }
}

void MainWindow::duplicateSelected() {
    auto* view = currentView();
    if (!view) return;
    const QModelIndex index = view->selectedSourceIndex();
    QString error;
    if (!view->document()->duplicateTag(
            view->model()->tagForIndex(index),
            view->model()->parentTagForIndex(index),
            view->model()->rowInParent(index),
            &error)) {
        showError(tr("Could Not Duplicate Tag"), error);
    }
}

void MainWindow::deleteSelected() {
    auto* view = currentView();
    if (!view) return;
    const QModelIndex index = view->selectedSourceIndex();
    NBTTag* tag = view->model()->tagForIndex(index);
    NBTTag* parent = view->model()->parentTagForIndex(index);
    if (!tag || !parent) return;
    if (QMessageBox::question(
            this, tr("Delete Tag"), tr("Delete %1?").arg(QString::fromUtf8(tag->name)),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    QString error;
    if (!view->document()->deleteTag(tag, parent, view->model()->rowInParent(index), &error)) {
        showError(tr("Could Not Delete Tag"), error);
    }
}

void MainWindow::copySelected() {
    auto* view = currentView();
    if (!view) return;
    NBTTag* tag = view->model()->tagForIndex(view->selectedSourceIndex());
    if (!tag) return;
    NBTTag* copy = nbt_tag_clone(tag);
    if (!copy) {
        showError(tr("Could Not Copy Tag"), tr("Out of memory while copying the selected tag."));
        return;
    }
    free_nbt_tree(clipboardTag_);
    clipboardTag_ = copy;
    statusBar()->showMessage(tr("Copied %1").arg(QString::fromUtf8(tag->name)), 3000);
    updateActions();
}

void MainWindow::cutSelected() {
    auto* view = currentView();
    if (!view) return;
    const QModelIndex index = view->selectedSourceIndex();
    NBTTag* tag = view->model()->tagForIndex(index);
    NBTTag* parent = view->model()->parentTagForIndex(index);
    if (!tag || !parent) return;
    copySelected();
    QString error;
    if (!view->document()->deleteTag(tag, parent, view->model()->rowInParent(index), &error)) {
        showError(tr("Could Not Cut Tag"), error);
    }
}

NBTTag* MainWindow::destinationForPaste(DocumentView* view) const {
    if (!view) return nullptr;
    const QModelIndex index = view->selectedSourceIndex();
    NBTTag* selected = view->model()->tagForIndex(index);
    if (isContainer(selected)) return selected;
    NBTTag* parent = view->model()->parentTagForIndex(index);
    return isContainer(parent) ? parent : nullptr;
}

void MainWindow::pasteTag() {
    auto* view = currentView();
    NBTTag* destination = destinationForPaste(view);
    if (!view || !destination || !clipboardTag_) return;
    QString error;
    if (!view->document()->insertTag(destination, clipboardTag_, &error)) {
        showError(tr("Could Not Paste Tag"), error);
    }
}

void MainWindow::chooseRegionChunk() {
    auto* view = currentView();
    if (!view || !view->document()->isRegion()) return;
    if (!maybeSave(view)) return;
    int x = -1;
    int z = -1;
    if (!chooseChunk(view->document()->filePath(), &x, &z)) return;
    QString error;
    if (!view->document()->openFile(view->document()->filePath(), &error, x, z)) {
        showError(tr("Could Not Load Chunk"), error);
    }
}

void MainWindow::updateActions() {
    auto* view = currentView();
    const bool hasDocument = view != nullptr;
    QModelIndex index = hasDocument ? view->selectedSourceIndex() : QModelIndex();
    NBTTag* selected = hasDocument ? view->model()->tagForIndex(index) : nullptr;
    NBTTag* parent = hasDocument ? view->model()->parentTagForIndex(index) : nullptr;
    NBTTag* destination = destinationForPaste(view);

    saveAction_->setEnabled(hasDocument && view->document()->isModified());
    saveAsAction_->setEnabled(hasDocument);
    exportJsonAction_->setEnabled(hasDocument);
    exportSnbtAction_->setEnabled(hasDocument);
    closeAction_->setEnabled(hasDocument);
    editAction_->setEnabled(selected && selected->type != TAG_Compound && selected->type != TAG_End);
    renameAction_->setEnabled(selected && parent && parent->type == TAG_Compound);
    addAction_->setEnabled(isContainer(selected) || isContainer(parent));
    duplicateAction_->setEnabled(selected && parent);
    deleteAction_->setEnabled(selected && parent);
    copyAction_->setEnabled(selected);
    cutAction_->setEnabled(selected && parent);
    pasteAction_->setEnabled(clipboardTag_ && destination);
    chunkAction_->setEnabled(hasDocument && view->document()->isRegion());
    expandAction_->setEnabled(hasDocument);
    collapseAction_->setEnabled(hasDocument);

    undoAction_->disconnect();
    redoAction_->disconnect();
    if (hasDocument) {
        QUndoStack* stack = view->document()->undoStack();
        undoAction_->setEnabled(stack->canUndo());
        redoAction_->setEnabled(stack->canRedo());
        undoAction_->setText(stack->canUndo() ? tr("Undo %1").arg(stack->undoText()) : tr("Undo"));
        redoAction_->setText(stack->canRedo() ? tr("Redo %1").arg(stack->redoText()) : tr("Redo"));
        connect(undoAction_, &QAction::triggered, stack, &QUndoStack::undo);
        connect(redoAction_, &QAction::triggered, stack, &QUndoStack::redo);
        statusBar()->showMessage(view->document()->formatDescription());
    } else {
        undoAction_->setEnabled(false);
        redoAction_->setEnabled(false);
        undoAction_->setText(tr("Undo"));
        redoAction_->setText(tr("Redo"));
    }
}

void MainWindow::showAbout() {
    QMessageBox::about(
        this,
        tr("About C-NBT Explorer"),
        tr("<h2>C-NBT Explorer</h2>"
           "<p>A cross-platform Minecraft NBT editor for Windows, macOS, and Linux.</p>"
           "<p>Supports Java and Bedrock binary NBT, Bedrock LevelDB records, SNBT, "
           "Anvil/McRegion and legacy Cubic Chunks r2 files, LZ4 and external region chunks, "
           "safe backups, search, tabs, drag-and-drop, copy/paste, and unlimited undo/redo.</p>")
    );
}

void MainWindow::showError(const QString& title, const QString& error) {
    QMessageBox::critical(this, title, error.isEmpty() ? tr("An unknown error occurred.") : error);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    for (int i = tabs_->count() - 1; i >= 0; --i) {
        auto* view = static_cast<DocumentView*>(tabs_->widget(i));
        tabs_->setCurrentIndex(i);
        if (!maybeSave(view)) {
            event->ignore();
            return;
        }
    }
    QSettings settings;
    settings.setValue(QStringLiteral("mainWindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("mainWindow/state"), saveState());
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) paths << url.toLocalFile();
    }
    if (!paths.isEmpty()) {
        openPaths(paths);
        event->acceptProposedAction();
    }
}
