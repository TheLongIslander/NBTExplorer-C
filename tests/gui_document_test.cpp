#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "Document.h"

extern "C" {
#include "edit_save.h"
#include "nbt_binary.h"
#include "nbt_builder.h"
#include "snbt.h"
#ifdef NBT_EXPLORER_TEST_BUNDLED_LEVELDB
#include <leveldb/c.h>
#endif
}

namespace {
bool check(bool condition, const char* message) {
    if (!condition) qCritical("FAIL: %s", message);
    return condition;
}

#ifdef NBT_EXPLORER_TEST_BUNDLED_LEVELDB
bool createBedrockDatabaseFixture(
    const QString& path,
    const QByteArray& key,
    const unsigned char* value,
    size_t valueSize,
    QString* error
) {
    if (!QDir().mkpath(path)) {
        *error = QStringLiteral("could not create Bedrock database directory");
        return false;
    }
    leveldb_options_t* options = leveldb_options_create();
    leveldb_writeoptions_t* writeOptions = leveldb_writeoptions_create();
    leveldb_options_set_create_if_missing(options, 1);
    leveldb_options_set_compression(options, leveldb_zlib_raw_compression);
    char* backendError = nullptr;
    const QByteArray encodedPath = path.toUtf8();
    leveldb_t* database = leveldb_open(options, encodedPath.constData(), &backendError);
    if (!backendError && database) {
        leveldb_put(database, writeOptions, key.constData(), static_cast<size_t>(key.size()),
                    reinterpret_cast<const char*>(value), valueSize, &backendError);
    }
    if (database) leveldb_close(database);
    leveldb_writeoptions_destroy(writeOptions);
    leveldb_options_destroy(options);
    if (backendError) {
        *error = QString::fromUtf8(backendError);
        leveldb_free(backendError);
        return false;
    }
    return true;
}
#endif
}

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!check(directory.isValid(), "temporary directory could not be created")) return 1;

    char error[512]{};
    NBTTag* fixture = snbt_parse(
        "{Data:{SpawnX:1014,LevelName:\"world\",Enabled:1b}}", "", error, sizeof(error));
    if (!check(fixture != nullptr, error)) return 1;
    unsigned char* encoded = nullptr;
    size_t encodedSize = 0;
    if (!check(nbt_binary_serialize(
            fixture, NBT_BINARY_JAVA, 0, &encoded, &encodedSize, error, sizeof(error)), error)) {
        free_nbt_tree(fixture);
        return 1;
    }
    free_nbt_tree(fixture);

    const QString sourcePath = directory.filePath(QStringLiteral("fixture.dat"));
    QFile source(sourcePath);
    if (!check(source.open(QIODevice::WriteOnly), "fixture could not be opened") ||
        !check(source.write(reinterpret_cast<const char*>(encoded), static_cast<qint64>(encodedSize)) ==
                   static_cast<qint64>(encodedSize), "fixture could not be written")) {
        free(encoded);
        return 1;
    }
    source.close();
    free(encoded);

    QString qtError;
    NbtDocument document;
    if (!check(document.openFile(sourcePath, &qtError), qPrintable(qtError))) return 1;
    NBTTag* spawn = find_tag_by_path(document.root(), "Data/SpawnX");
    if (!check(spawn && spawn->value.int_val == 1014, "initial value mismatch")) return 1;
    if (!check(document.editTag(spawn, QStringLiteral("4242"), &qtError), qPrintable(qtError))) return 1;
    spawn = find_tag_by_path(document.root(), "Data/SpawnX");
    if (!check(spawn && spawn->value.int_val == 4242, "edit was not applied")) return 1;

    document.undoStack()->undo();
    spawn = find_tag_by_path(document.root(), "Data/SpawnX");
    if (!check(spawn && spawn->value.int_val == 1014, "undo did not restore value")) return 1;
    document.undoStack()->redo();
    spawn = find_tag_by_path(document.root(), "Data/SpawnX");
    if (!check(spawn && spawn->value.int_val == 4242, "redo did not restore edit")) return 1;

    NBTTag* data = find_tag_by_path(document.root(), "Data");
    if (!check(document.addTag(data, TAG_String, QStringLiteral("Added"), &qtError), qPrintable(qtError))) return 1;
    NBTTag* added = find_tag_by_path(document.root(), "Data/Added");
    data = find_tag_by_path(document.root(), "Data");
    if (!check(added != nullptr, "new tag was not added") ||
        !check(document.renameTag(added, data, QStringLiteral("Renamed"), &qtError), qPrintable(qtError))) return 1;
    if (!check(find_tag_by_path(document.root(), "Data/Renamed") != nullptr, "tag was not renamed")) return 1;

    const QString savedPath = directory.filePath(QStringLiteral("saved.dat"));
    if (!check(document.saveAs(savedPath, &qtError), qPrintable(qtError))) return 1;
    NbtDocument reopened;
    if (!check(reopened.openFile(savedPath, &qtError), qPrintable(qtError))) return 1;
    spawn = find_tag_by_path(reopened.root(), "Data/SpawnX");
    if (!check(spawn && spawn->value.int_val == 4242, "saved edit did not round trip")) return 1;

    const QString snbtPath = directory.filePath(QStringLiteral("export.snbt"));
    if (!check(reopened.exportSnbt(snbtPath, &qtError), qPrintable(qtError))) return 1;
    NbtDocument snbtDocument;
    if (!check(snbtDocument.openFile(snbtPath, &qtError), qPrintable(qtError))) return 1;
    if (!check(find_tag_by_path(snbtDocument.root(), "Data/Renamed") != nullptr,
               "SNBT export did not round trip")) return 1;

    const QString jsonPath = directory.filePath(QStringLiteral("export.json"));
    if (!check(reopened.exportJson(jsonPath, &qtError), qPrintable(qtError)) ||
        !check(QFile(jsonPath).size() > 100, "JSON export is unexpectedly empty")) return 1;

    NbtDocument newBedrockLevel;
    if (!check(newBedrockLevel.createNew(NBT_BINARY_BEDROCK_LEVEL_DAT, false, &qtError),
               qPrintable(qtError))) return 1;
    if (!check(newBedrockLevel.addTag(
            newBedrockLevel.root(), TAG_Int, QStringLiteral("StorageTest"), &qtError),
            qPrintable(qtError))) return 1;
    NBTTag* storageTest = find_tag_by_path(newBedrockLevel.root(), "StorageTest");
    if (!check(storageTest && newBedrockLevel.editTag(
            storageTest, QStringLiteral("73"), &qtError), qPrintable(qtError))) return 1;
    const QString bedrockLevelPath = directory.filePath(QStringLiteral("bedrock-level.dat"));
    if (!check(newBedrockLevel.saveAs(bedrockLevelPath, &qtError), qPrintable(qtError))) return 1;
    NbtDocument reopenedBedrockLevel;
    if (!check(reopenedBedrockLevel.openFile(bedrockLevelPath, &qtError), qPrintable(qtError)) ||
        !check(reopenedBedrockLevel.binaryFormat() == NBT_BINARY_BEDROCK_LEVEL_DAT,
               "Bedrock level.dat format was not preserved")) return 1;
    storageTest = find_tag_by_path(reopenedBedrockLevel.root(), "StorageTest");
    if (!check(storageTest && storageTest->value.int_val == 73,
               "new Bedrock level.dat did not round trip")) return 1;

#ifdef NBT_EXPLORER_TEST_BUNDLED_LEVELDB
    NBTTag* databaseFixture = snbt_parse("{PlayerName:\"Alex\",Health:20s}", "", error, sizeof(error));
    unsigned char* databaseBytes = nullptr;
    size_t databaseSize = 0;
    if (!check(databaseFixture != nullptr, error) ||
        !check(nbt_binary_serialize(databaseFixture, NBT_BINARY_BEDROCK, 0,
                                    &databaseBytes, &databaseSize, error, sizeof(error)), error)) {
        free_nbt_tree(databaseFixture);
        return 1;
    }
    free_nbt_tree(databaseFixture);
    const QString databasePath = directory.filePath(QStringLiteral("world/db"));
    const QByteArray databaseKey("~local_player");
    if (!check(createBedrockDatabaseFixture(
            databasePath, databaseKey, databaseBytes, databaseSize, &qtError), qPrintable(qtError))) {
        free(databaseBytes);
        return 1;
    }
    free(databaseBytes);

    NbtDocument databaseDocument;
    if (!check(databaseDocument.openBedrockDatabaseRecord(
            databasePath, databaseKey, QStringLiteral("~local_player"), &qtError), qPrintable(qtError))) return 1;
    NBTTag* health = find_tag_by_path(databaseDocument.root(), "Health");
    if (!check(health && databaseDocument.editTag(
            health, QStringLiteral("18"), &qtError), qPrintable(qtError)) ||
        !check(databaseDocument.save(&qtError), qPrintable(qtError))) return 1;
    const QDir backupDirectory(directory.filePath(QStringLiteral("world/cnbt-record-backups")));
    if (!check(backupDirectory.entryList({QStringLiteral("*.bin")}, QDir::Files).size() == 1,
               "Bedrock database save did not create one record backup")) return 1;
    NbtDocument reopenedDatabaseDocument;
    if (!check(reopenedDatabaseDocument.openBedrockDatabaseRecord(
            databasePath, databaseKey, QStringLiteral("~local_player"), &qtError), qPrintable(qtError))) return 1;
    health = find_tag_by_path(reopenedDatabaseDocument.root(), "Health");
    if (!check(health && health->value.short_val == 18,
               "Bedrock database edit did not round trip")) return 1;
#endif

    qInfo("GUI document tests passed");
    return 0;
}
