#ifndef CNBT_DOCUMENT_H
#define CNBT_DOCUMENT_H

#include <functional>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUndoStack>

extern "C" {
#include "nbt_io.h"
#include "nbt_binary.h"
#include "nbt_parser.h"
}

class NbtDocument final : public QObject {
    Q_OBJECT

public:
    explicit NbtDocument(QObject* parent = nullptr);
    ~NbtDocument() override;

    bool createNew(NBTBinaryFormat format, bool snbt, QString* error);
    bool openFile(const QString& path, QString* error, int chunkX = -1, int chunkZ = -1);
    bool openBedrockDatabaseRecord(
        const QString& databaseDirectory,
        const QByteArray& key,
        const QString& keyLabel,
        QString* error
    );
    bool loadRegionChunk(int chunkX, int chunkZ, QString* error);
    bool save(QString* error);
    bool saveAs(const QString& path, QString* error);
    bool exportJson(const QString& path, QString* error) const;
    bool exportSnbt(const QString& path, QString* error) const;

    NBTTag* root() const { return root_; }
    const QString& filePath() const { return filePath_; }
    QString displayName() const;
    QString formatDescription() const;
    bool isRegion() const { return loadInfo_.source_type == NBT_SOURCE_REGION_CHUNK; }
    bool isBedrockDatabaseRecord() const { return bedrockDatabaseRecord_; }
    bool isSnbt() const { return sourceIsSnbt_; }
    NBTBinaryFormat binaryFormat() const { return binaryInfo_.format; }
    int chunkX() const { return loadInfo_.chunk_x; }
    int chunkZ() const { return loadInfo_.chunk_z; }
    bool isModified() const { return !undoStack_.isClean(); }
    QUndoStack* undoStack() { return &undoStack_; }

    void setBackupOnSave(bool enabled) { backupOnSave_ = enabled; }
    bool backupOnSave() const { return backupOnSave_; }

    bool editTag(NBTTag* tag, const QString& jsonValue, QString* error);
    bool renameTag(NBTTag* tag, NBTTag* parent, const QString& newName, QString* error);
    bool addTag(NBTTag* parent, TagType type, const QString& name, QString* error);
    bool insertTag(NBTTag* parent, const NBTTag* source, QString* error);
    bool deleteTag(NBTTag* tag, NBTTag* parent, int row, QString* error);
    bool duplicateTag(NBTTag* tag, NBTTag* parent, int row, QString* error);
    bool moveTag(NBTTag* source, NBTTag* sourceParent, int sourceRow,
                 NBTTag* destination, int destinationRow, QString* error);

signals:
    void treeChanged();
    void titleChanged();
    void statusMessage(const QString& message);

private:
    bool performMutation(const QString& label, const std::function<bool(QString*)>& operation, QString* error);
    QByteArray snapshot(QString* error) const;
    bool restoreSnapshot(const QByteArray& bytes, QString* error);
    bool writeStandalone(const QString& path, QString* error);
    bool writeRegion(const QString& path, QString* error);
    bool writeBedrockDatabaseRecord(QString* error);
    bool createBackupIfNeeded(const QString& targetPath, QString* backupPath, QString* error) const;
    void replaceRoot(NBTTag* replacement);

    friend class DocumentSnapshotCommand;

    NBTTag* root_ = nullptr;
    QString filePath_;
    NBTLoadInfo loadInfo_{};
    NBTBinaryInfo binaryInfo_{};
    bool sourceIsSnbt_ = false;
    bool bedrockDatabaseRecord_ = false;
    QString bedrockDatabaseDirectory_;
    QByteArray bedrockDatabaseKey_;
    QString bedrockDatabaseKeyLabel_;
    QUndoStack undoStack_;
    bool backupOnSave_ = true;
};

#endif
