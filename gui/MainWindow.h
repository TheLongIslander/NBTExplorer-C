#ifndef CNBT_MAIN_WINDOW_H
#define CNBT_MAIN_WINDOW_H

#include <QMainWindow>

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLineEdit;
class QSortFilterProxyModel;
class QTabWidget;
class QTreeView;

class NbtDocument;
class NbtTreeModel;
struct NBTTag;

class DocumentView final : public QWidget {
public:
    explicit DocumentView(NbtDocument* document, QWidget* parent = nullptr);

    NbtDocument* document() const { return document_; }
    NbtTreeModel* model() const { return model_; }
    QTreeView* tree() const { return tree_; }
    QModelIndex selectedSourceIndex() const;
    void focusSearch();

private:
    NbtDocument* document_;
    NbtTreeModel* model_;
    QSortFilterProxyModel* proxy_;
    QTreeView* tree_;
    QLineEdit* search_;
};

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openPaths(const QStringList& paths);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void openFiles();
    void openBedrockDatabase();
    void saveCurrent();
    void saveCurrentAs();
    void exportCurrentJson();
    void exportCurrentSnbt();
    void closeCurrentTab();
    void editSelected();
    void renameSelected();
    void addTag();
    void duplicateSelected();
    void deleteSelected();
    void copySelected();
    void cutSelected();
    void pasteTag();
    void chooseRegionChunk();
    void updateActions();
    void showAbout();

private:
    void createActions();
    void createMenusAndToolbars();
    void createNewDocument(int format, bool snbt = false);
    DocumentView* addDocumentTab(NbtDocument* document, const QString& tooltip = {});
    DocumentView* currentView() const;
    bool openOne(const QString& path);
    bool openBedrockDatabaseAt(const QString& path);
    bool maybeSave(DocumentView* view);
    bool saveView(DocumentView* view, bool saveAs);
    bool chooseChunk(const QString& path, int* chunkX, int* chunkZ);
    void showError(const QString& title, const QString& error);
    NBTTag* destinationForPaste(DocumentView* view) const;

    QTabWidget* tabs_;
    QAction* saveAction_;
    QAction* saveAsAction_;
    QAction* exportJsonAction_;
    QAction* exportSnbtAction_;
    QAction* closeAction_;
    QAction* undoAction_;
    QAction* redoAction_;
    QAction* editAction_;
    QAction* renameAction_;
    QAction* addAction_;
    QAction* duplicateAction_;
    QAction* deleteAction_;
    QAction* copyAction_;
    QAction* cutAction_;
    QAction* pasteAction_;
    QAction* chunkAction_;
    QAction* expandAction_;
    QAction* collapseAction_;
    QAction* backupAction_;
    NBTTag* clipboardTag_ = nullptr;
};

#endif
