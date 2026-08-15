#include "Document.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <limits>

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUndoCommand>

#include <zlib.h>

extern "C" {
#include "bedrock_db.h"
#include "edit_save.h"
#include "edit_value.h"
#include "nbt_builder.h"
#include "nbt_binary.h"
#include "nbt_json.h"
#include "nbt_tree.h"
#include "region_read.h"
#include "region_write.h"
#include "snbt.h"
}

namespace {

QString cError(const char* error, const QString& fallback) {
    return error && error[0] ? QString::fromUtf8(error) : fallback;
}

bool containsTag(const NBTTag* root, const NBTTag* candidate) {
    if (!root || !candidate) return false;
    if (root == candidate) return true;
    if (root->type == TAG_Compound) {
        for (int i = 0; i < root->value.compound.count; ++i) {
            if (containsTag(root->value.compound.items[i], candidate)) return true;
        }
    } else if (root->type == TAG_List) {
        for (int i = 0; i < root->value.list.count; ++i) {
            if (containsTag(root->value.list.items[i], candidate)) return true;
        }
    }
    return false;
}

QByteArray compressNbt(const QByteArray& raw, NBTInputFormat format, QString* error) {
    if (format == NBT_INPUT_FORMAT_RAW) return raw;

    if (raw.size() > static_cast<qsizetype>(INT_MAX - 65536)) {
        if (error) *error = QObject::tr("The NBT document is too large to compress safely.");
        return {};
    }

    z_stream stream{};
    const int windowBits = format == NBT_INPUT_FORMAT_GZIP ? 15 + 16 : 15;
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        if (error) *error = QObject::tr("Could not initialize compression.");
        return {};
    }

    QByteArray output;
    output.resize(std::max<qsizetype>(16384, raw.size() / 2));
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(raw.constData()));
    stream.avail_in = static_cast<uInt>(raw.size());
    int result = Z_OK;

    while (result != Z_STREAM_END) {
        if (stream.total_out >= static_cast<uLong>(output.size())) {
            output.resize(output.size() + 16384);
        }
        stream.next_out = reinterpret_cast<Bytef*>(output.data() + stream.total_out);
        stream.avail_out = static_cast<uInt>(output.size() - static_cast<int>(stream.total_out));
        result = deflate(&stream, Z_FINISH);
        if (result != Z_OK && result != Z_STREAM_END && result != Z_BUF_ERROR) {
            deflateEnd(&stream);
            if (error) *error = QObject::tr("Compression failed.");
            return {};
        }
    }

    const int finalSize = static_cast<int>(stream.total_out);
    deflateEnd(&stream);
    output.resize(finalSize);
    return output;
}

}  // namespace

class DocumentSnapshotCommand final : public QUndoCommand {
public:
    DocumentSnapshotCommand(NbtDocument* document, QByteArray before, QByteArray after, const QString& label)
        : document_(document), before_(std::move(before)), after_(std::move(after)) {
        setText(label);
    }

    void undo() override {
        QString error;
        if (!document_->restoreSnapshot(before_, &error)) {
            emit document_->statusMessage(QObject::tr("Undo failed: %1").arg(error));
        }
    }

    void redo() override {
        QString error;
        if (!document_->restoreSnapshot(after_, &error)) {
            emit document_->statusMessage(QObject::tr("Redo failed: %1").arg(error));
        }
    }

private:
    NbtDocument* document_;
    QByteArray before_;
    QByteArray after_;
};

NbtDocument::NbtDocument(QObject* parent) : QObject(parent) {
    connect(&undoStack_, &QUndoStack::cleanChanged, this, [this] {
        emit titleChanged();
    });
}

NbtDocument::~NbtDocument() {
    free_nbt_tree(root_);
}

bool NbtDocument::createNew(NBTBinaryFormat format, bool snbt, QString* error) {
    if ((!snbt && format != NBT_BINARY_JAVA && format != NBT_BINARY_BEDROCK &&
         format != NBT_BINARY_BEDROCK_LEVEL_DAT) || (snbt && format != NBT_BINARY_JAVA)) {
        if (error) *error = tr("Invalid format for a new NBT document.");
        return false;
    }

    NBTTag* root = nbt_tag_create(TAG_Compound, "");
    if (!root) {
        if (error) *error = tr("Out of memory while creating the document.");
        return false;
    }

    replaceRoot(root);
    filePath_.clear();
    bedrockDatabaseRecord_ = false;
    bedrockDatabaseDirectory_.clear();
    bedrockDatabaseKey_.clear();
    bedrockDatabaseKeyLabel_.clear();
    memset(&loadInfo_, 0, sizeof(loadInfo_));
    loadInfo_.source_type = NBT_SOURCE_STANDALONE;
    loadInfo_.input_format = snbt || format != NBT_BINARY_JAVA
        ? NBT_INPUT_FORMAT_RAW : NBT_INPUT_FORMAT_GZIP;
    loadInfo_.chunk_x = -1;
    loadInfo_.chunk_z = -1;
    memset(&binaryInfo_, 0, sizeof(binaryInfo_));
    binaryInfo_.format = format;
    if (format == NBT_BINARY_BEDROCK_LEVEL_DAT) binaryInfo_.bedrock_storage_version = 10;
    sourceIsSnbt_ = snbt;
    undoStack_.clear();
    undoStack_.setClean();
    emit titleChanged();
    emit statusMessage(tr("Created a new %1").arg(formatDescription()));
    return true;
}

void NbtDocument::replaceRoot(NBTTag* replacement) {
    if (root_ == replacement) return;
    free_nbt_tree(root_);
    root_ = replacement;
    emit treeChanged();
    emit titleChanged();
}

bool NbtDocument::openFile(const QString& path, QString* error, int chunkX, int chunkZ) {
    const QByteArray nativePath = path.toUtf8();
    NBTLoadOptions options{};
    NBTLoadInfo info{};
    NBTBinaryInfo binaryInfo{};
    size_t size = 0;
    char loadError[512]{};
    char parseError[512]{};

    if (chunkX >= 0 || chunkZ >= 0) {
        if (chunkX < 0 || chunkX > 31 || chunkZ < 0 || chunkZ > 31) {
            if (error) *error = tr("Chunk coordinates must both be between 0 and 31.");
            return false;
        }
        options.has_chunk_coords = 1;
        options.chunk_x = chunkX;
        options.chunk_z = chunkZ;
    }

    const bool isSnbt = QFileInfo(path).suffix().compare(QStringLiteral("snbt"), Qt::CaseInsensitive) == 0;
    NBTTag* parsed = nullptr;
    if (isSnbt) {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) {
            if (error) *error = input.errorString();
            return false;
        }
        QByteArray text = input.readAll();
        text.append('\0');
        parsed = snbt_parse(text.constData(), "", parseError, sizeof(parseError));
        info.input_format = NBT_INPUT_FORMAT_RAW;
        info.source_type = NBT_SOURCE_STANDALONE;
        info.chunk_x = -1;
        info.chunk_z = -1;
    } else {
        unsigned char* bytes = load_nbt_data(
            nativePath.constData(), &size, &options, &info, loadError, sizeof(loadError)
        );
        if (!bytes) {
            if (error) *error = cError(loadError, tr("Could not load the NBT file."));
            return false;
        }
        const NBTBinaryFormat requested = info.source_type == NBT_SOURCE_REGION_CHUNK
            ? NBT_BINARY_JAVA : NBT_BINARY_AUTO;
        parsed = nbt_binary_parse(
            bytes, size, requested, &binaryInfo, parseError, sizeof(parseError));
        free(bytes);
    }
    if (!parsed) {
        if (error) *error = cError(parseError, tr("Could not parse the NBT document."));
        return false;
    }

    replaceRoot(parsed);
    filePath_ = QFileInfo(path).absoluteFilePath();
    bedrockDatabaseRecord_ = false;
    bedrockDatabaseDirectory_.clear();
    bedrockDatabaseKey_.clear();
    bedrockDatabaseKeyLabel_.clear();
    loadInfo_ = info;
    binaryInfo_ = binaryInfo;
    sourceIsSnbt_ = isSnbt;
    undoStack_.clear();
    undoStack_.setClean();
    emit titleChanged();
    emit statusMessage(tr("Opened %1").arg(QFileInfo(filePath_).fileName()));
    return true;
}

bool NbtDocument::openBedrockDatabaseRecord(
    const QString& databaseDirectory,
    const QByteArray& key,
    const QString& keyLabel,
    QString* error
) {
    if (databaseDirectory.isEmpty() || key.isEmpty()) {
        if (error) *error = tr("The Bedrock database directory or record key is empty.");
        return false;
    }

    char backendError[512]{};
    const QByteArray encodedDirectory = databaseDirectory.toUtf8();
    BedrockDB* database = bedrock_db_open(
        encodedDirectory.constData(), BEDROCK_DB_LOGICAL_READ_ONLY, nullptr,
        backendError, sizeof(backendError));
    if (!database) {
        if (error) *error = cError(backendError, tr("Could not open the Bedrock world database."));
        return false;
    }
    int found = 0;
    NBTTag* parsed = bedrock_db_get_nbt(
        database,
        reinterpret_cast<const unsigned char*>(key.constData()),
        static_cast<size_t>(key.size()),
        &found,
        backendError,
        sizeof(backendError));
    bedrock_db_close(database);
    if (!parsed) {
        if (error) {
            *error = found
                ? cError(backendError, tr("The selected record is not one complete little-endian NBT document."))
                : tr("The selected Bedrock database record no longer exists.");
        }
        return false;
    }

    replaceRoot(parsed);
    filePath_.clear();
    memset(&loadInfo_, 0, sizeof(loadInfo_));
    loadInfo_.source_type = NBT_SOURCE_STANDALONE;
    loadInfo_.input_format = NBT_INPUT_FORMAT_RAW;
    loadInfo_.chunk_x = -1;
    loadInfo_.chunk_z = -1;
    memset(&binaryInfo_, 0, sizeof(binaryInfo_));
    binaryInfo_.format = NBT_BINARY_BEDROCK;
    sourceIsSnbt_ = false;
    bedrockDatabaseRecord_ = true;
    bedrockDatabaseDirectory_ = QDir(databaseDirectory).absolutePath();
    bedrockDatabaseKey_ = key;
    bedrockDatabaseKeyLabel_ = keyLabel;
    undoStack_.clear();
    undoStack_.setClean();
    emit titleChanged();
    emit statusMessage(tr("Opened Bedrock database record %1").arg(keyLabel));
    return true;
}

bool NbtDocument::loadRegionChunk(int chunkX, int chunkZ, QString* error) {
    if (!isRegion()) {
        if (error) *error = tr("The current document is not a region file.");
        return false;
    }
    if (isModified()) {
        if (error) *error = tr("Save or discard the current chunk changes before switching chunks.");
        return false;
    }
    return openFile(filePath_, error, chunkX, chunkZ);
}

QString NbtDocument::displayName() const {
    QString name = bedrockDatabaseRecord_
        ? tr("DB: %1").arg(bedrockDatabaseKeyLabel_)
        : filePath_.isEmpty() ? tr("Untitled") : QFileInfo(filePath_).fileName();
    if (isRegion()) name += tr(" [%1, %2]").arg(chunkX()).arg(chunkZ());
    if (isModified()) name += QLatin1Char('*');
    return name;
}

QString NbtDocument::formatDescription() const {
    if (bedrockDatabaseRecord_) {
        return tr("Bedrock LevelDB record · little-endian NBT");
    }
    if (sourceIsSnbt_) return tr("SNBT text document");
    QString compression = QString::fromLatin1(nbt_input_format_name(loadInfo_.input_format));
    if (isRegion()) {
        return tr("Region chunk (%1, %2), %3").arg(chunkX()).arg(chunkZ()).arg(compression);
    }
    return tr("Standalone %1, %2")
        .arg(QString::fromLatin1(nbt_binary_format_name(binaryInfo_.format)), compression);
}

QByteArray NbtDocument::snapshot(QString* error) const {
    unsigned char* bytes = nullptr;
    size_t size = 0;
    char serializationError[512]{};
    if (!root_) {
        if (error) *error = tr("No document is loaded.");
        return {};
    }
    if (!serialize_tag_to_nbt_bytes(root_, &bytes, &size, serializationError, sizeof(serializationError))) {
        if (error) *error = cError(serializationError, tr("Could not serialize the document."));
        return {};
    }
    QByteArray result(reinterpret_cast<const char*>(bytes), static_cast<qsizetype>(size));
    free(bytes);
    return result;
}

bool NbtDocument::restoreSnapshot(const QByteArray& bytes, QString* error) {
    char parseError[512]{};
    size_t offset = 0;
    NBTTag* parsed = build_nbt_tree(
        reinterpret_cast<const unsigned char*>(bytes.constData()),
        static_cast<size_t>(bytes.size()),
        &offset,
        parseError,
        sizeof(parseError)
    );
    if (!parsed) {
        if (error) *error = cError(parseError, tr("Could not restore the document snapshot."));
        return false;
    }
    replaceRoot(parsed);
    return true;
}

bool NbtDocument::performMutation(
    const QString& label,
    const std::function<bool(QString*)>& operation,
    QString* error
) {
    QString localError;
    const QByteArray before = snapshot(&localError);
    if (before.isEmpty() && !localError.isEmpty()) {
        if (error) *error = localError;
        return false;
    }
    if (!operation(&localError)) {
        if (error) *error = localError;
        return false;
    }
    const QByteArray after = snapshot(&localError);
    if (after.isEmpty() && !localError.isEmpty()) {
        restoreSnapshot(before, nullptr);
        if (error) *error = localError;
        return false;
    }

    if (!restoreSnapshot(before, &localError)) {
        if (error) *error = localError;
        return false;
    }
    undoStack_.push(new DocumentSnapshotCommand(this, before, after, label));
    return true;
}

bool NbtDocument::editTag(NBTTag* tag, const QString& jsonValue, QString* error) {
    if (!tag) {
        if (error) *error = tr("No tag is selected.");
        return false;
    }
    return performMutation(tr("Edit %1").arg(QString::fromUtf8(tag->name)), [=](QString* localError) {
        char editError[512]{};
        const QByteArray value = jsonValue.toUtf8();
        EditStatus status = tag->type == TAG_Compound
            ? apply_json_patch_to_compound(tag, value.constData(), editError, sizeof(editError))
            : parse_json_for_tag_type(tag, value.constData(), editError, sizeof(editError));
        if (status != EDIT_OK) {
            *localError = cError(editError, QString::fromLatin1(edit_status_name(status)));
            return false;
        }
        return true;
    }, error);
}

bool NbtDocument::renameTag(NBTTag* tag, NBTTag* parent, const QString& newName, QString* error) {
    if (!tag || !parent || parent->type != TAG_Compound) {
        if (error) *error = tr("Only named tags inside compounds can be renamed.");
        return false;
    }
    const QByteArray encoded = newName.toUtf8();
    if (encoded.size() > 65535) {
        if (error) *error = tr("NBT tag names cannot exceed 65,535 bytes.");
        return false;
    }
    const int existing = nbt_compound_find_index(parent, encoded.constData());
    if (existing >= 0 && parent->value.compound.items[existing] != tag) {
        if (error) *error = tr("A sibling tag already has that name.");
        return false;
    }

    return performMutation(tr("Rename tag"), [=](QString* localError) {
        if (!nbt_tag_rename(tag, encoded.constData())) {
            *localError = tr("Out of memory while renaming the tag.");
            return false;
        }
        return true;
    }, error);
}

bool NbtDocument::addTag(NBTTag* parent, TagType type, const QString& name, QString* error) {
    if (!parent || (parent->type != TAG_Compound && parent->type != TAG_List)) {
        if (error) *error = tr("Tags can only be added to compounds or lists.");
        return false;
    }
    if (parent->type == TAG_List && parent->value.list.count > 0 && parent->value.list.element_type != type) {
        if (error) *error = tr("Every element in an NBT list must have the same type (%1).")
            .arg(QString::fromLatin1(nbt_tag_type_name(parent->value.list.element_type)));
        return false;
    }
    const QByteArray encodedName = parent->type == TAG_List ? QByteArray() : name.toUtf8();
    if (parent->type == TAG_Compound && nbt_compound_find_index(parent, encodedName.constData()) >= 0) {
        if (error) *error = tr("A sibling tag already has that name.");
        return false;
    }

    return performMutation(tr("Add %1").arg(QString::fromLatin1(nbt_tag_type_name(type))), [=](QString* localError) {
        NBTTag* child = nbt_tag_create(type, encodedName.constData());
        if (!child) {
            *localError = tr("Out of memory while creating the tag.");
            return false;
        }
        const bool appended = parent->type == TAG_Compound
            ? nbt_compound_append(parent, child) != 0
            : nbt_list_append(parent, child) != 0;
        if (!appended) {
            free_nbt_tree(child);
            *localError = tr("Could not insert the tag.");
            return false;
        }
        return true;
    }, error);
}

bool NbtDocument::deleteTag(NBTTag* tag, NBTTag* parent, int row, QString* error) {
    if (!tag || !parent || row < 0) {
        if (error) *error = tr("The root tag cannot be deleted.");
        return false;
    }
    return performMutation(tr("Delete %1").arg(QString::fromUtf8(tag->name)), [=](QString* localError) {
        NBTTag* removed = parent->type == TAG_Compound
            ? nbt_compound_take(parent, row)
            : parent->type == TAG_List ? nbt_list_take(parent, row) : nullptr;
        if (!removed) {
            *localError = tr("Could not remove the selected tag.");
            return false;
        }
        free_nbt_tree(removed);
        return true;
    }, error);
}

bool NbtDocument::insertTag(NBTTag* parent, const NBTTag* source, QString* error) {
    if (!parent || !source || (parent->type != TAG_Compound && parent->type != TAG_List)) {
        if (error) *error = tr("Choose a compound or list as the destination.");
        return false;
    }
    if (parent->type == TAG_List && parent->value.list.count > 0 &&
        parent->value.list.element_type != source->type) {
        if (error) *error = tr("The copied tag does not match the destination list type.");
        return false;
    }

    return performMutation(tr("Paste tag"), [=](QString* localError) {
        NBTTag* copy = nbt_tag_clone(source);
        if (!copy) {
            *localError = tr("Out of memory while copying the tag.");
            return false;
        }
        if (parent->type == TAG_Compound) {
            QString base = QString::fromUtf8(copy->name);
            QString candidate = base;
            int suffix = 2;
            while (nbt_compound_find_index(parent, candidate.toUtf8().constData()) >= 0) {
                candidate = base + tr(" Copy %1").arg(suffix++);
            }
            if (!nbt_tag_rename(copy, candidate.toUtf8().constData()) ||
                !nbt_compound_append(parent, copy)) {
                free_nbt_tree(copy);
                *localError = tr("Could not insert the copied tag.");
                return false;
            }
        } else if (!nbt_list_append(parent, copy)) {
            free_nbt_tree(copy);
            *localError = tr("Could not insert the copied tag.");
            return false;
        }
        return true;
    }, error);
}

bool NbtDocument::duplicateTag(NBTTag* tag, NBTTag* parent, int row, QString* error) {
    if (!tag || !parent || row < 0) {
        if (error) *error = tr("The root tag cannot be duplicated here.");
        return false;
    }
    return performMutation(tr("Duplicate %1").arg(QString::fromUtf8(tag->name)), [=](QString* localError) {
        NBTTag* copy = nbt_tag_clone(tag);
        if (!copy) {
            *localError = tr("Out of memory while duplicating the tag.");
            return false;
        }

        bool inserted = false;
        if (parent->type == TAG_Compound) {
            QString base = QString::fromUtf8(copy->name);
            QString candidate = base + tr(" Copy");
            int suffix = 2;
            while (nbt_compound_find_index(parent, candidate.toUtf8().constData()) >= 0) {
                candidate = base + tr(" Copy %1").arg(suffix++);
            }
            if (!nbt_tag_rename(copy, candidate.toUtf8().constData())) {
                free_nbt_tree(copy);
                *localError = tr("Out of memory while naming the duplicate.");
                return false;
            }
            inserted = nbt_compound_insert(parent, row + 1, copy) != 0;
        } else if (parent->type == TAG_List) {
            inserted = nbt_list_insert(parent, row + 1, copy) != 0;
        }
        if (!inserted) {
            free_nbt_tree(copy);
            *localError = tr("Could not insert the duplicate.");
            return false;
        }
        return true;
    }, error);
}

bool NbtDocument::moveTag(
    NBTTag* source,
    NBTTag* sourceParent,
    int sourceRow,
    NBTTag* destination,
    int destinationRow,
    QString* error
) {
    if (!source || !sourceParent || !destination ||
        (destination->type != TAG_Compound && destination->type != TAG_List)) {
        if (error) *error = tr("Choose a compound or list as the destination.");
        return false;
    }
    if (containsTag(source, destination)) {
        if (error) *error = tr("A tag cannot be moved into itself or one of its descendants.");
        return false;
    }
    if (destination != sourceParent && destination->type == TAG_Compound &&
        nbt_compound_find_index(destination, source->name) >= 0) {
        if (error) *error = tr("The destination already contains a tag with that name.");
        return false;
    }
    if (destination->type == TAG_List && destination->value.list.count > 0 &&
        destination->value.list.element_type != source->type) {
        if (error) *error = tr("The tag type does not match the destination list.");
        return false;
    }

    return performMutation(tr("Move %1").arg(QString::fromUtf8(source->name)), [=](QString* localError) {
        NBTTag* removed = sourceParent->type == TAG_Compound
            ? nbt_compound_take(sourceParent, sourceRow)
            : sourceParent->type == TAG_List ? nbt_list_take(sourceParent, sourceRow) : nullptr;
        if (!removed) {
            *localError = tr("Could not detach the selected tag.");
            return false;
        }
        int insertAt = destinationRow;
        const int destinationCount = destination->type == TAG_Compound
            ? destination->value.compound.count : destination->value.list.count;
        if (destination == sourceParent && insertAt > sourceRow) --insertAt;
        if (insertAt < 0 || insertAt > destinationCount) insertAt = destinationCount;
        const bool inserted = destination->type == TAG_Compound
            ? nbt_compound_insert(destination, insertAt, removed) != 0
            : nbt_list_insert(destination, insertAt, removed) != 0;
        if (!inserted) {
            if (sourceParent->type == TAG_Compound) nbt_compound_insert(sourceParent, sourceRow, removed);
            else nbt_list_insert(sourceParent, sourceRow, removed);
            *localError = tr("Could not insert the tag at the destination.");
            return false;
        }
        return true;
    }, error);
}

bool NbtDocument::createBackupIfNeeded(const QString& targetPath, QString* backupPath, QString* error) const {
    if (!backupOnSave_ || !QFileInfo::exists(targetPath)) return true;

    QString candidate = targetPath + QStringLiteral(".bak");
    if (QFileInfo::exists(candidate)) {
        candidate = targetPath + QLatin1Char('.') +
            QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")) +
            QStringLiteral(".bak");
    }
    if (!QFile::copy(targetPath, candidate)) {
        if (error) *error = tr("Could not create backup: %1").arg(candidate);
        return false;
    }
    if (isRegion()) {
        const QByteArray encodedPath = targetPath.toUtf8();
        char regionError[512]{};
        RegionFile* region = region_file_read(encodedPath.constData(), regionError, sizeof(regionError));
        if (!region) {
            if (error) *error = cError(regionError, tr("Could not inspect external chunks for backup."));
            return false;
        }
        const QString suffix = candidate.mid(targetPath.size());
        for (int i = 0; i < REGION_CHUNK_COUNT; ++i) {
            const RegionChunkSlot& slot = region->chunks[i];
            if (!slot.present || !slot.external) continue;
            int x = 0;
            int z = 0;
            region_chunk_coords(i, &x, &z);
            char* rawSidecar = region_external_chunk_path(encodedPath.constData(), x, z);
            if (!rawSidecar) {
                region_file_free(region);
                if (error) *error = tr("Out of memory while preparing an external chunk backup.");
                return false;
            }
            const QString sidecar = QString::fromUtf8(rawSidecar);
            free(rawSidecar);
            if (!QFile::copy(sidecar, sidecar + suffix)) {
                region_file_free(region);
                if (error) *error = tr("Could not back up external chunk: %1").arg(sidecar);
                return false;
            }
        }
        region_file_free(region);
    }
    if (backupPath) *backupPath = candidate;
    return true;
}

bool NbtDocument::writeStandalone(const QString& path, QString* error) {
    const bool writeSnbt = QFileInfo(path).suffix().compare(QStringLiteral("snbt"), Qt::CaseInsensitive) == 0;
    QByteArray encoded;
    if (writeSnbt) {
        char serializationError[512]{};
        char* text = snbt_serialize(root_, 1, serializationError, sizeof(serializationError));
        if (!text) {
            if (error) *error = cError(serializationError, tr("Could not serialize SNBT."));
            return false;
        }
        encoded = QByteArray(text);
        free(text);
    } else {
        char serializationError[512]{};
        unsigned char* bytes = nullptr;
        size_t size = 0;
        NBTBinaryFormat format = sourceIsSnbt_ ? NBT_BINARY_JAVA : binaryInfo_.format;
        if (format == NBT_BINARY_AUTO) format = NBT_BINARY_JAVA;
        const uint32_t storageVersion = sourceIsSnbt_ ? 0 : binaryInfo_.bedrock_storage_version;
        if (!nbt_binary_serialize(
                root_, format, storageVersion, &bytes, &size,
                serializationError, sizeof(serializationError))) {
            if (error) *error = cError(serializationError, tr("Could not serialize binary NBT."));
            return false;
        }
        if (size > static_cast<size_t>(std::numeric_limits<qsizetype>::max())) {
            free(bytes);
            if (error) *error = tr("The serialized NBT document is too large for this platform.");
            return false;
        }
        const QByteArray raw(reinterpret_cast<const char*>(bytes), static_cast<qsizetype>(size));
        free(bytes);
        NBTInputFormat outputFormat = sourceIsSnbt_ ? NBT_INPUT_FORMAT_GZIP : loadInfo_.input_format;
        if (outputFormat == NBT_INPUT_FORMAT_UNKNOWN || outputFormat == NBT_INPUT_FORMAT_LZ4) {
            outputFormat = NBT_INPUT_FORMAT_GZIP;
        }
        encoded = compressNbt(raw, outputFormat, error);
        if (encoded.isEmpty() && !raw.isEmpty()) return false;
    }

    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) *error = output.errorString();
        return false;
    }
    if (output.write(encoded) != encoded.size()) {
        if (error) *error = output.errorString();
        output.cancelWriting();
        return false;
    }
    if (!output.commit()) {
        if (error) *error = output.errorString();
        return false;
    }
    return true;
}

bool NbtDocument::writeRegion(const QString& path, QString* error) {
    const QByteArray sourcePath = filePath_.toUtf8();
    const QByteArray outputPath = path.toUtf8();
    char regionError[512]{};
    RegionFile* region = region_file_read(sourcePath.constData(), regionError, sizeof(regionError));
    if (!region) {
        if (error) *error = cError(regionError, tr("Could not reload the region file."));
        return false;
    }
    if (!region_file_update_chunk_from_nbt(
            region, chunkX(), chunkZ(), root_, -1, regionError, sizeof(regionError))) {
        if (error) *error = cError(regionError, tr("Could not update the selected region chunk."));
        region_file_free(region);
        return false;
    }
    const int ok = region_file_write_atomic(region, outputPath.constData(), regionError, sizeof(regionError));
    region_file_free(region);
    if (!ok) {
        if (error) *error = cError(regionError, tr("Could not write the region file."));
        return false;
    }
    return true;
}

bool NbtDocument::writeBedrockDatabaseRecord(QString* error) {
    if (!bedrockDatabaseRecord_ || bedrockDatabaseDirectory_.isEmpty()) {
        if (error) *error = tr("This document is not linked to a Bedrock database record.");
        return false;
    }

    char backendError[512]{};
    const QByteArray encodedDirectory = bedrockDatabaseDirectory_.toUtf8();
    BedrockDB* database = bedrock_db_open(
        encodedDirectory.constData(), BEDROCK_DB_READ_WRITE, nullptr,
        backendError, sizeof(backendError));
    if (!database) {
        if (error) {
            *error = cError(
                backendError,
                tr("Could not lock the Bedrock database. Make sure Minecraft is closed."));
        }
        return false;
    }

    QString backupPath;
    if (backupOnSave_) {
        unsigned char* previousValue = nullptr;
        size_t previousSize = 0;
        int found = 0;
        const bool readPrevious = bedrock_db_get(
                database,
                reinterpret_cast<const unsigned char*>(bedrockDatabaseKey_.constData()),
                static_cast<size_t>(bedrockDatabaseKey_.size()),
                &previousValue,
                &previousSize,
                &found,
                backendError,
                sizeof(backendError));
        if (!readPrevious || !found) {
            free(previousValue);
            bedrock_db_close(database);
            if (error) {
                *error = !readPrevious
                    ? cError(backendError, tr("Could not read the previous database value for backup."))
                    : tr("The Bedrock database record no longer exists; no changes were written.");
            }
            return false;
        }
        if (previousSize > static_cast<size_t>(std::numeric_limits<qint64>::max())) {
            free(previousValue);
            bedrock_db_close(database);
            if (error) *error = tr("The Bedrock record is too large to back up.");
            return false;
        }

        QDir databaseDirectory(bedrockDatabaseDirectory_);
        const QString backupDirectoryPath = databaseDirectory.absoluteFilePath(
            QStringLiteral("../cnbt-record-backups"));
        QDir backupDirectory;
        if (!backupDirectory.mkpath(backupDirectoryPath)) {
            free(previousValue);
            bedrock_db_close(database);
            if (error) *error = tr("Could not create the Bedrock record backup directory: %1")
                .arg(backupDirectoryPath);
            return false;
        }
        const QByteArray keyHash = QCryptographicHash::hash(
            bedrockDatabaseKey_, QCryptographicHash::Sha256).toHex().left(16);
        backupPath = QDir(backupDirectoryPath).filePath(
            QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz-")) +
            QString::fromLatin1(keyHash) + QStringLiteral(".bin"));
        QSaveFile backup(backupPath);
        backup.setDirectWriteFallback(false);
        const bool backedUp = backup.open(QIODevice::WriteOnly) &&
            backup.write(reinterpret_cast<const char*>(previousValue),
                         static_cast<qint64>(previousSize)) == static_cast<qint64>(previousSize) &&
            backup.commit();
        free(previousValue);
        if (!backedUp) {
            bedrock_db_close(database);
            if (error) *error = tr("Could not create the Bedrock record backup: %1")
                .arg(backup.errorString());
            return false;
        }
    }

    const bool written = bedrock_db_put_nbt(
        database,
        reinterpret_cast<const unsigned char*>(bedrockDatabaseKey_.constData()),
        static_cast<size_t>(bedrockDatabaseKey_.size()),
        root_,
        backendError,
        sizeof(backendError));
    bedrock_db_close(database);
    if (!written) {
        if (error) *error = cError(backendError, tr("Could not update the Bedrock database record."));
        return false;
    }

    undoStack_.setClean();
    emit titleChanged();
    emit statusMessage(backupPath.isEmpty()
        ? tr("Saved Bedrock database record %1").arg(bedrockDatabaseKeyLabel_)
        : tr("Saved Bedrock database record (backup: %1)").arg(backupPath));
    return true;
}

bool NbtDocument::save(QString* error) {
    if (bedrockDatabaseRecord_) return writeBedrockDatabaseRecord(error);
    if (filePath_.isEmpty()) {
        if (error) *error = tr("Choose a destination with Save As.");
        return false;
    }
    return saveAs(filePath_, error);
}

bool NbtDocument::saveAs(const QString& path, QString* error) {
    QString backupPath;
    if (!createBackupIfNeeded(path, &backupPath, error)) return false;
    const bool ok = isRegion() ? writeRegion(path, error) : writeStandalone(path, error);
    if (!ok) return false;

    filePath_ = QFileInfo(path).absoluteFilePath();
    bedrockDatabaseRecord_ = false;
    bedrockDatabaseDirectory_.clear();
    bedrockDatabaseKey_.clear();
    bedrockDatabaseKeyLabel_.clear();
    if (!isRegion()) {
        const bool nowSnbt = QFileInfo(path).suffix().compare(QStringLiteral("snbt"), Qt::CaseInsensitive) == 0;
        if (sourceIsSnbt_ && !nowSnbt) {
            memset(&binaryInfo_, 0, sizeof(binaryInfo_));
            binaryInfo_.format = NBT_BINARY_JAVA;
            loadInfo_.input_format = NBT_INPUT_FORMAT_GZIP;
        }
        sourceIsSnbt_ = nowSnbt;
        if (nowSnbt) loadInfo_.input_format = NBT_INPUT_FORMAT_RAW;
    }
    undoStack_.setClean();
    emit titleChanged();
    emit statusMessage(backupPath.isEmpty()
        ? tr("Saved %1").arg(QFileInfo(filePath_).fileName())
        : tr("Saved %1 (backup: %2)").arg(QFileInfo(filePath_).fileName(), backupPath));
    return true;
}

bool NbtDocument::exportJson(const QString& path, QString* error) const {
    if (!root_) {
        if (error) *error = tr("No document is loaded.");
        return false;
    }
    const QByteArray outputPath = path.toUtf8();
    char jsonError[512]{};
    if (!nbt_write_typed_json_file(outputPath.constData(), root_, 1, jsonError, sizeof(jsonError))) {
        if (error) *error = cError(jsonError, tr("Could not export JSON."));
        return false;
    }
    return true;
}

bool NbtDocument::exportSnbt(const QString& path, QString* error) const {
    if (!root_) {
        if (error) *error = tr("No document is loaded.");
        return false;
    }
    char serializationError[512]{};
    char* text = snbt_serialize(root_, 1, serializationError, sizeof(serializationError));
    if (!text) {
        if (error) *error = cError(serializationError, tr("Could not export SNBT."));
        return false;
    }
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    const QByteArray bytes(text);
    free(text);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        if (error) *error = output.errorString();
        return false;
    }
    return true;
}
