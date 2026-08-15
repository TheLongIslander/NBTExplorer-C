#include "BedrockDatabaseDialog.h"

#include <cstdint>

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

extern "C" {
#include "bedrock_db.h"
}

namespace {
constexpr int kKeyRole = Qt::UserRole + 1;
constexpr qsizetype kRecordLimit = 200000;

int32_t readLittleI32(const unsigned char* bytes) {
    const uint32_t value = static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
    return static_cast<int32_t>(value);
}

QString keyLabel(const unsigned char* key, size_t size) {
    bool printable = size > 0;
    for (size_t index = 0; index < size; ++index) {
        if (key[index] < 0x20 || key[index] > 0x7e) {
            printable = false;
            break;
        }
    }
    if (printable) {
        return QString::fromLatin1(
            reinterpret_cast<const char*>(key), static_cast<qsizetype>(size));
    }

    if (size == 9 || size == 10 || size == 13 || size == 14) {
        const int32_t x = readLittleI32(key);
        const int32_t z = readLittleI32(key + 4);
        const bool hasDimension = size >= 13;
        const size_t recordOffset = hasDimension ? 12 : 8;
        QString result = hasDimension
            ? QObject::tr("Chunk (%1, %2), dimension %3, record 0x%4")
                .arg(x).arg(z).arg(readLittleI32(key + 8))
                .arg(key[recordOffset], 2, 16, QLatin1Char('0'))
            : QObject::tr("Chunk (%1, %2), record 0x%3")
                .arg(x).arg(z).arg(key[recordOffset], 2, 16, QLatin1Char('0'));
        if (size == recordOffset + 2) {
            result += QObject::tr(", subchunk %1").arg(static_cast<qint8>(key[recordOffset + 1]));
        }
        return result;
    }

    return QObject::tr("Hex: %1").arg(
        QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(key), size).toHex(' ')));
}

struct EnumerationContext {
    QStandardItemModel* model = nullptr;
    qsizetype count = 0;
    bool truncated = false;
};

int appendRecord(
    const unsigned char* key,
    size_t keySize,
    const unsigned char* value,
    size_t valueSize,
    void* userData
) {
    auto* context = static_cast<EnumerationContext*>(userData);
    if (context->count >= kRecordLimit) {
        context->truncated = true;
        return 0;
    }

    const QByteArray keyBytes(reinterpret_cast<const char*>(key), static_cast<qsizetype>(keySize));
    auto* name = new QStandardItem(keyLabel(key, keySize));
    name->setData(keyBytes, kKeyRole);
    name->setToolTip(QString::fromLatin1(keyBytes.toHex(' ')));
    auto* sizeItem = new QStandardItem(QString::number(static_cast<qulonglong>(valueSize)));
    sizeItem->setData(static_cast<qulonglong>(valueSize), Qt::UserRole);
    auto* kind = new QStandardItem(
        valueSize > 0 && value && value[0] == 10
            ? QObject::tr("NBT compound candidate")
            : QObject::tr("Binary/custom record"));
    context->model->appendRow({name, sizeItem, kind});
    ++context->count;
    return 1;
}
}  // namespace

BedrockDatabaseDialog::BedrockDatabaseDialog(QWidget* parent) : QDialog(parent) {}

bool BedrockDatabaseDialog::chooseRecord(
    QWidget* parent,
    const QString& databaseDirectory,
    QByteArray* selectedKey,
    QString* selectedLabel,
    QString* error
) {
    if (!selectedKey || !selectedLabel) {
        if (error) *error = QObject::tr("Invalid Bedrock database selection output.");
        return false;
    }

    char backendError[512]{};
    const QByteArray encodedDirectory = databaseDirectory.toUtf8();
    BedrockDB* database = bedrock_db_open(
        encodedDirectory.constData(), BEDROCK_DB_LOGICAL_READ_ONLY, nullptr,
        backendError, sizeof(backendError));
    if (!database) {
        if (error) *error = QString::fromUtf8(backendError);
        return false;
    }

    BedrockDatabaseDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Choose Bedrock World Record"));
    dialog.resize(900, 620);
    auto* layout = new QVBoxLayout(&dialog);
    auto* explanation = new QLabel(
        QObject::tr("Minecraft must be closed while its world database is open. "
                    "Choose a record whose value is a complete little-endian NBT document; "
                    "chunk terrain and other custom binary records are listed but cannot be opened as NBT."),
        &dialog);
    explanation->setWordWrap(true);
    auto* search = new QLineEdit(&dialog);
    search->setPlaceholderText(QObject::tr("Filter keys, chunk coordinates, or record type…"));
    search->setClearButtonEnabled(true);

    auto* model = new QStandardItemModel(&dialog);
    model->setHorizontalHeaderLabels({
        QObject::tr("Key / decoded coordinates"),
        QObject::tr("Bytes"),
        QObject::tr("Value kind")
    });
    EnumerationContext context{model};
    const bool enumerated = bedrock_db_iterate(
        database, appendRecord, &context, backendError, sizeof(backendError));
    bedrock_db_close(database);
    if (!enumerated) {
        if (error) *error = QString::fromUtf8(backendError);
        return false;
    }

    auto* proxy = new QSortFilterProxyModel(&dialog);
    proxy->setSourceModel(model);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setFilterKeyColumn(-1);
    proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    auto* table = new QTableView(&dialog);
    table->setModel(proxy);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setSortingEnabled(true);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->resizeSection(0, 510);
    table->horizontalHeader()->resizeSection(1, 100);

    auto* countLabel = new QLabel(
        context.truncated
            ? QObject::tr("Showing the first %1 records (safety limit reached).").arg(context.count)
            : QObject::tr("%1 records").arg(context.count),
        &dialog);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, &dialog);
    QPushButton* openButton = buttons->button(QDialogButtonBox::Open);
    openButton->setEnabled(false);

    QObject::connect(search, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
    QObject::connect(table->selectionModel(), &QItemSelectionModel::selectionChanged,
                     &dialog, [table, openButton] {
        openButton->setEnabled(table->currentIndex().isValid());
    });
    QObject::connect(table, &QTableView::doubleClicked, &dialog, [&dialog](const QModelIndex&) {
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->addWidget(explanation);
    layout->addWidget(search);
    layout->addWidget(table, 1);
    layout->addWidget(countLabel);
    layout->addWidget(buttons);

    if (proxy->rowCount() > 0) {
        table->selectRow(0);
        table->setCurrentIndex(proxy->index(0, 0));
    }
    search->setFocus();
    if (dialog.exec() != QDialog::Accepted || !table->currentIndex().isValid()) return false;

    const QModelIndex proxyIndex = table->currentIndex().siblingAtColumn(0);
    const QModelIndex sourceIndex = proxy->mapToSource(proxyIndex);
    *selectedKey = model->itemFromIndex(sourceIndex)->data(kKeyRole).toByteArray();
    *selectedLabel = model->itemFromIndex(sourceIndex)->text();
    return true;
}
