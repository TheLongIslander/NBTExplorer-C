#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nbt_binary.h"
#include "nbt_builder.h"

#define NBT_MAX_DEPTH 512u

typedef struct {
    const unsigned char* data;
    size_t size;
    size_t pos;
    int little_endian;
    size_t depth;
    size_t nodes;
    char* err;
    size_t err_sz;
    int failed;
} BinaryReader;

typedef struct {
    unsigned char* data;
    size_t size;
    size_t capacity;
    int little_endian;
    size_t depth;
    size_t nodes;
    char* err;
    size_t err_sz;
    int failed;
} BinaryWriter;

static void set_error(char* err, size_t err_sz, const char* message) {
    if (err && err_sz > 0) snprintf(err, err_sz, "%s", message);
}

static int reader_error(BinaryReader* r, const char* message) {
    if (r && !r->failed && r->err && r->err_sz > 0) {
        snprintf(r->err, r->err_sz, "%s at byte offset %zu", message, r->pos);
    }
    if (r) r->failed = 1;
    return 0;
}

static int reader_take(BinaryReader* r, void* out, size_t length) {
    if (!r || r->failed) return 0;
    if (r->pos > r->size || length > r->size - r->pos) {
        return reader_error(r, "unexpected end of binary NBT");
    }
    if (out && length > 0) memcpy(out, r->data + r->pos, length);
    r->pos += length;
    return 1;
}

static int read_u8(BinaryReader* r, uint8_t* value) {
    return reader_take(r, value, 1);
}

static int read_u16(BinaryReader* r, uint16_t* value) {
    unsigned char b[2];
    if (!reader_take(r, b, sizeof(b))) return 0;
    if (r->little_endian) {
        *value = (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
    } else {
        *value = (uint16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
    }
    return 1;
}

static int read_u32(BinaryReader* r, uint32_t* value) {
    unsigned char b[4];
    if (!reader_take(r, b, sizeof(b))) return 0;
    if (r->little_endian) {
        *value = (uint32_t)b[0] |
                 ((uint32_t)b[1] << 8) |
                 ((uint32_t)b[2] << 16) |
                 ((uint32_t)b[3] << 24);
    } else {
        *value = ((uint32_t)b[0] << 24) |
                 ((uint32_t)b[1] << 16) |
                 ((uint32_t)b[2] << 8) |
                 (uint32_t)b[3];
    }
    return 1;
}

static int read_u64(BinaryReader* r, uint64_t* value) {
    unsigned char b[8];
    uint64_t result = 0;
    if (!reader_take(r, b, sizeof(b))) return 0;
    if (r->little_endian) {
        for (int i = 7; i >= 0; --i) result = (result << 8) | b[i];
    } else {
        for (int i = 0; i < 8; ++i) result = (result << 8) | b[i];
    }
    *value = result;
    return 1;
}

static char* duplicate_string(const char* value) {
    size_t length = value ? strlen(value) : 0;
    char* copy = malloc(length + 1);
    if (!copy) return NULL;
    if (length > 0) memcpy(copy, value, length);
    copy[length] = '\0';
    return copy;
}

static char* read_string(BinaryReader* r) {
    uint16_t length;
    char* value;
    if (!read_u16(r, &length)) return NULL;
    value = malloc((size_t)length + 1);
    if (!value) {
        reader_error(r, "out of memory while reading NBT string");
        return NULL;
    }
    if (!reader_take(r, value, length)) {
        free(value);
        return NULL;
    }
    value[length] = '\0';
    return value;
}

static int valid_type(uint8_t type) {
    return type <= (uint8_t)TAG_Long_Array;
}

static NBTTag* allocate_tag(TagType type, const char* name, BinaryReader* r) {
    NBTTag* tag;
    if (++r->nodes > r->size + 1) {
        reader_error(r, "binary NBT contains too many tags");
        return NULL;
    }
    tag = calloc(1, sizeof(*tag));
    if (!tag) {
        reader_error(r, "out of memory while creating NBT tag");
        return NULL;
    }
    tag->type = type;
    tag->name = duplicate_string(name);
    if (!tag->name) {
        free(tag);
        reader_error(r, "out of memory while copying NBT tag name");
        return NULL;
    }
    return tag;
}

static int parse_payload(BinaryReader* r, NBTTag* tag);

static NBTTag* parse_named_tag(BinaryReader* r) {
    uint8_t raw_type = TAG_End;
    char* name;
    NBTTag* tag;

    if (!read_u8(r, &raw_type)) return NULL;
    if (!valid_type(raw_type) || raw_type == TAG_End) {
        reader_error(r, raw_type == TAG_End ? "unexpected TAG_End" : "invalid NBT tag type");
        return NULL;
    }
    name = read_string(r);
    if (!name) return NULL;
    tag = allocate_tag((TagType)raw_type, name, r);
    free(name);
    if (!tag) return NULL;
    if (!parse_payload(r, tag)) {
        free_nbt_tree(tag);
        return NULL;
    }
    return tag;
}

static int parse_array_length(BinaryReader* r, int32_t* length, size_t width, const char* kind) {
    uint32_t raw;
    if (!read_u32(r, &raw)) return 0;
    *length = (int32_t)raw;
    if (*length < 0) {
        char message[96];
        snprintf(message, sizeof(message), "negative %s length", kind);
        return reader_error(r, message);
    }
    if (width > 0 && (size_t)*length > (r->size - r->pos) / width) {
        char message[96];
        snprintf(message, sizeof(message), "%s exceeds remaining input", kind);
        return reader_error(r, message);
    }
    return 1;
}

static int parse_payload(BinaryReader* r, NBTTag* tag) {
    if (!r || !tag) return 0;
    if (++r->depth > NBT_MAX_DEPTH) {
        --r->depth;
        return reader_error(r, "binary NBT nesting depth limit exceeded");
    }

    switch (tag->type) {
        case TAG_Byte: {
            uint8_t value;
            if (!read_u8(r, &value)) goto fail;
            tag->value.byte_val = (int8_t)value;
            break;
        }
        case TAG_Short: {
            uint16_t value;
            if (!read_u16(r, &value)) goto fail;
            tag->value.short_val = (int16_t)value;
            break;
        }
        case TAG_Int:
        case TAG_Float: {
            uint32_t value;
            if (!read_u32(r, &value)) goto fail;
            if (tag->type == TAG_Int) {
                tag->value.int_val = (int32_t)value;
            } else {
                memcpy(&tag->value.float_val, &value, sizeof(value));
            }
            break;
        }
        case TAG_Long:
        case TAG_Double: {
            uint64_t value;
            if (!read_u64(r, &value)) goto fail;
            if (tag->type == TAG_Long) {
                tag->value.long_val = (int64_t)value;
            } else {
                memcpy(&tag->value.double_val, &value, sizeof(value));
            }
            break;
        }
        case TAG_Byte_Array: {
            int32_t length;
            if (!parse_array_length(r, &length, 1, "TAG_Byte_Array")) goto fail;
            tag->value.byte_array.length = length;
            if (length > 0) {
                tag->value.byte_array.data = malloc((size_t)length);
                if (!tag->value.byte_array.data) {
                    reader_error(r, "out of memory while reading TAG_Byte_Array");
                    goto fail;
                }
                if (!reader_take(r, tag->value.byte_array.data, (size_t)length)) goto fail;
            }
            break;
        }
        case TAG_String:
            tag->value.string_val = read_string(r);
            if (!tag->value.string_val) goto fail;
            break;
        case TAG_List: {
            uint8_t element_type;
            int32_t count;
            uint32_t raw_count;
            if (!read_u8(r, &element_type) || !valid_type(element_type) ||
                !read_u32(r, &raw_count)) {
                if (!r->failed) reader_error(r, "invalid TAG_List header");
                goto fail;
            }
            count = (int32_t)raw_count;
            if (count < 0 || (count > 0 && element_type == TAG_End)) {
                reader_error(r, "invalid TAG_List length or element type");
                goto fail;
            }
            if ((size_t)count > r->size - r->pos ||
                (size_t)count > SIZE_MAX / sizeof(NBTTag*)) {
                reader_error(r, "TAG_List length exceeds remaining input");
                goto fail;
            }
            tag->value.list.element_type = (TagType)element_type;
            tag->value.list.count = count;
            if (count > 0) {
                tag->value.list.items = calloc((size_t)count, sizeof(NBTTag*));
                if (!tag->value.list.items) {
                    reader_error(r, "out of memory while reading TAG_List");
                    goto fail;
                }
            }
            for (int32_t i = 0; i < count; ++i) {
                tag->value.list.items[i] = allocate_tag((TagType)element_type, "", r);
                if (!tag->value.list.items[i] ||
                    !parse_payload(r, tag->value.list.items[i])) goto fail;
            }
            break;
        }
        case TAG_Compound:
            while (1) {
                uint8_t next;
                NBTTag* child;
                NBTTag** grown;
                if (r->pos >= r->size) {
                    reader_error(r, "unterminated TAG_Compound");
                    goto fail;
                }
                next = r->data[r->pos];
                if (next == TAG_End) {
                    ++r->pos;
                    break;
                }
                child = parse_named_tag(r);
                if (!child) goto fail;
                if (tag->value.compound.count == INT_MAX) {
                    free_nbt_tree(child);
                    reader_error(r, "TAG_Compound contains too many children");
                    goto fail;
                }
                grown = realloc(tag->value.compound.items,
                                (size_t)(tag->value.compound.count + 1) * sizeof(NBTTag*));
                if (!grown) {
                    free_nbt_tree(child);
                    reader_error(r, "out of memory while reading TAG_Compound");
                    goto fail;
                }
                tag->value.compound.items = grown;
                tag->value.compound.items[tag->value.compound.count++] = child;
            }
            break;
        case TAG_Int_Array: {
            int32_t length;
            if (!parse_array_length(r, &length, 4, "TAG_Int_Array")) goto fail;
            tag->value.int_array.length = length;
            if (length > 0) {
                tag->value.int_array.data = malloc((size_t)length * sizeof(int32_t));
                if (!tag->value.int_array.data) {
                    reader_error(r, "out of memory while reading TAG_Int_Array");
                    goto fail;
                }
                for (int32_t i = 0; i < length; ++i) {
                    uint32_t value;
                    if (!read_u32(r, &value)) goto fail;
                    tag->value.int_array.data[i] = (int32_t)value;
                }
            }
            break;
        }
        case TAG_Long_Array: {
            int32_t length;
            if (!parse_array_length(r, &length, 8, "TAG_Long_Array")) goto fail;
            tag->value.long_array.length = length;
            if (length > 0) {
                tag->value.long_array.data = malloc((size_t)length * sizeof(int64_t));
                if (!tag->value.long_array.data) {
                    reader_error(r, "out of memory while reading TAG_Long_Array");
                    goto fail;
                }
                for (int32_t i = 0; i < length; ++i) {
                    uint64_t value;
                    if (!read_u64(r, &value)) goto fail;
                    tag->value.long_array.data[i] = (int64_t)value;
                }
            }
            break;
        }
        case TAG_End:
        default:
            reader_error(r, "TAG_End cannot be used as a payload");
            goto fail;
    }

    --r->depth;
    return 1;

fail:
    --r->depth;
    return 0;
}

static NBTTag* parse_payload_document(
    const unsigned char* data,
    size_t size,
    int little_endian,
    size_t* consumed,
    char* err,
    size_t err_sz
) {
    BinaryReader reader;
    NBTTag* root;
    memset(&reader, 0, sizeof(reader));
    reader.data = data;
    reader.size = size;
    reader.little_endian = little_endian;
    reader.err = err;
    reader.err_sz = err_sz;
    root = parse_named_tag(&reader);
    if (!root) return NULL;
    if (consumed) *consumed = reader.pos;
    return root;
}

static uint32_t load_le32(const unsigned char* data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void initialize_info(NBTBinaryInfo* info) {
    if (!info) return;
    memset(info, 0, sizeof(*info));
    info->format = NBT_BINARY_AUTO;
}

static NBTTag* parse_explicit(
    const unsigned char* data,
    size_t size,
    NBTBinaryFormat format,
    NBTBinaryInfo* info,
    char* err,
    size_t err_sz
) {
    const unsigned char* payload = data;
    size_t payload_size = size;
    size_t payload_offset = 0;
    size_t consumed = 0;
    uint32_t version = 0;
    uint32_t declared = 0;
    NBTTag* root;
    int little_endian = format != NBT_BINARY_JAVA;

    if (format == NBT_BINARY_BEDROCK_LEVEL_DAT) {
        if (size < 8) {
            set_error(err, err_sz, "Bedrock level.dat is shorter than its eight-byte header");
            return NULL;
        }
        version = load_le32(data);
        declared = load_le32(data + 4);
        if ((size_t)declared > size - 8) {
            set_error(err, err_sz, "Bedrock level.dat payload length exceeds the file size");
            return NULL;
        }
        payload = data + 8;
        payload_size = declared;
        payload_offset = 8;
    }

    root = parse_payload_document(payload, payload_size, little_endian,
                                  &consumed, err, err_sz);
    if (!root) return NULL;
    if (format == NBT_BINARY_BEDROCK_LEVEL_DAT && consumed != payload_size) {
        free_nbt_tree(root);
        set_error(err, err_sz, "Bedrock level.dat payload contains trailing bytes");
        return NULL;
    }
    if (info) {
        info->format = format;
        info->payload_offset = payload_offset;
        info->payload_size = payload_size;
        info->bytes_consumed = payload_offset + consumed;
        info->bedrock_storage_version = version;
        info->bedrock_declared_payload_size = declared;
    }
    return root;
}

NBTTag* nbt_binary_parse(
    const unsigned char* data,
    size_t size,
    NBTBinaryFormat format,
    NBTBinaryInfo* info,
    char* err,
    size_t err_sz
) {
    NBTTag* root;
    NBTBinaryInfo candidate;
    char candidate_err[256] = {0};

    initialize_info(info);
    if (err && err_sz > 0) err[0] = '\0';
    if (!data || size == 0) {
        set_error(err, err_sz, "binary NBT input is empty");
        return NULL;
    }
    if (format != NBT_BINARY_AUTO && format != NBT_BINARY_JAVA &&
        format != NBT_BINARY_BEDROCK && format != NBT_BINARY_BEDROCK_LEVEL_DAT) {
        set_error(err, err_sz, "invalid binary NBT format");
        return NULL;
    }
    if (format != NBT_BINARY_AUTO) {
        return parse_explicit(data, size, format, info, err, err_sz);
    }

    if (size >= 8 && (size_t)load_le32(data + 4) == size - 8) {
        initialize_info(&candidate);
        root = parse_explicit(data, size, NBT_BINARY_BEDROCK_LEVEL_DAT,
                              &candidate, candidate_err, sizeof(candidate_err));
        if (root && candidate.bytes_consumed == size) {
            if (info) *info = candidate;
            return root;
        }
        free_nbt_tree(root);
    }

    initialize_info(&candidate);
    candidate_err[0] = '\0';
    root = parse_explicit(data, size, NBT_BINARY_JAVA,
                          &candidate, candidate_err, sizeof(candidate_err));
    if (root && candidate.bytes_consumed == size) {
        if (info) *info = candidate;
        return root;
    }
    free_nbt_tree(root);

    initialize_info(&candidate);
    candidate_err[0] = '\0';
    root = parse_explicit(data, size, NBT_BINARY_BEDROCK,
                          &candidate, candidate_err, sizeof(candidate_err));
    if (root && candidate.bytes_consumed == size) {
        if (info) *info = candidate;
        return root;
    }
    free_nbt_tree(root);

    if (candidate_err[0]) set_error(err, err_sz, candidate_err);
    else set_error(err, err_sz, "input is not a complete Java or Bedrock NBT document");
    return NULL;
}

static int writer_error(BinaryWriter* w, const char* message) {
    if (w && !w->failed && w->err && w->err_sz > 0) {
        snprintf(w->err, w->err_sz, "%s", message);
    }
    if (w) w->failed = 1;
    return 0;
}

static int writer_reserve(BinaryWriter* w, size_t extra) {
    size_t needed;
    size_t capacity;
    unsigned char* grown;
    if (!w || w->failed) return 0;
    if (extra > SIZE_MAX - w->size) return writer_error(w, "binary NBT output is too large");
    needed = w->size + extra;
    if (needed <= w->capacity) return 1;
    capacity = w->capacity ? w->capacity : 256;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    grown = realloc(w->data, capacity);
    if (!grown) return writer_error(w, "out of memory while serializing binary NBT");
    w->data = grown;
    w->capacity = capacity;
    return 1;
}

static int write_bytes(BinaryWriter* w, const void* data, size_t length) {
    if (!writer_reserve(w, length)) return 0;
    if (length > 0) memcpy(w->data + w->size, data, length);
    w->size += length;
    return 1;
}

static int write_u8(BinaryWriter* w, uint8_t value) {
    return write_bytes(w, &value, 1);
}

static int write_u16(BinaryWriter* w, uint16_t value) {
    unsigned char b[2];
    if (w->little_endian) {
        b[0] = (unsigned char)value;
        b[1] = (unsigned char)(value >> 8);
    } else {
        b[0] = (unsigned char)(value >> 8);
        b[1] = (unsigned char)value;
    }
    return write_bytes(w, b, sizeof(b));
}

static int write_u32(BinaryWriter* w, uint32_t value) {
    unsigned char b[4];
    for (int i = 0; i < 4; ++i) {
        int index = w->little_endian ? i : 3 - i;
        b[index] = (unsigned char)(value >> (i * 8));
    }
    return write_bytes(w, b, sizeof(b));
}

static int write_u64(BinaryWriter* w, uint64_t value) {
    unsigned char b[8];
    for (int i = 0; i < 8; ++i) {
        int index = w->little_endian ? i : 7 - i;
        b[index] = (unsigned char)(value >> (i * 8));
    }
    return write_bytes(w, b, sizeof(b));
}

static int write_string(BinaryWriter* w, const char* value) {
    size_t length = value ? strlen(value) : 0;
    if (length > UINT16_MAX) return writer_error(w, "NBT string exceeds 65535 bytes");
    return write_u16(w, (uint16_t)length) && write_bytes(w, value, length);
}

static int write_payload(BinaryWriter* w, const NBTTag* tag);

static int write_named_tag(BinaryWriter* w, const NBTTag* tag) {
    if (!tag || tag->type <= TAG_End || tag->type > TAG_Long_Array) {
        return writer_error(w, "invalid named NBT tag");
    }
    if (++w->nodes == 0) return writer_error(w, "too many NBT tags to serialize");
    return write_u8(w, (uint8_t)tag->type) &&
           write_string(w, tag->name ? tag->name : "") &&
           write_payload(w, tag);
}

static int validate_count(BinaryWriter* w, int count, const void* data, const char* kind) {
    char message[128];
    if (count < 0 || (count > 0 && !data)) {
        snprintf(message, sizeof(message), "invalid %s storage", kind);
        return writer_error(w, message);
    }
    return 1;
}

static int write_payload(BinaryWriter* w, const NBTTag* tag) {
    if (++w->depth > NBT_MAX_DEPTH) {
        --w->depth;
        return writer_error(w, "binary NBT nesting depth limit exceeded");
    }
    switch (tag->type) {
        case TAG_Byte:
            if (!write_u8(w, (uint8_t)tag->value.byte_val)) goto fail;
            break;
        case TAG_Short:
            if (!write_u16(w, (uint16_t)tag->value.short_val)) goto fail;
            break;
        case TAG_Int:
            if (!write_u32(w, (uint32_t)tag->value.int_val)) goto fail;
            break;
        case TAG_Long:
            if (!write_u64(w, (uint64_t)tag->value.long_val)) goto fail;
            break;
        case TAG_Float: {
            uint32_t bits;
            memcpy(&bits, &tag->value.float_val, sizeof(bits));
            if (!write_u32(w, bits)) goto fail;
            break;
        }
        case TAG_Double: {
            uint64_t bits;
            memcpy(&bits, &tag->value.double_val, sizeof(bits));
            if (!write_u64(w, bits)) goto fail;
            break;
        }
        case TAG_Byte_Array:
            if (!validate_count(w, tag->value.byte_array.length,
                                tag->value.byte_array.data, "TAG_Byte_Array") ||
                !write_u32(w, (uint32_t)tag->value.byte_array.length) ||
                !write_bytes(w, tag->value.byte_array.data,
                             (size_t)tag->value.byte_array.length)) goto fail;
            break;
        case TAG_String:
            if (!write_string(w, tag->value.string_val ? tag->value.string_val : "")) goto fail;
            break;
        case TAG_List:
            if (tag->value.list.element_type < TAG_End ||
                tag->value.list.element_type > TAG_Long_Array ||
                !validate_count(w, tag->value.list.count,
                                tag->value.list.items, "TAG_List") ||
                (tag->value.list.count > 0 && tag->value.list.element_type == TAG_End) ||
                !write_u8(w, (uint8_t)tag->value.list.element_type) ||
                !write_u32(w, (uint32_t)tag->value.list.count)) goto fail;
            for (int i = 0; i < tag->value.list.count; ++i) {
                NBTTag* item = tag->value.list.items[i];
                if (!item || item->type != tag->value.list.element_type) {
                    writer_error(w, "TAG_List contains a null or mismatched element");
                    goto fail;
                }
                if (++w->nodes == 0 || !write_payload(w, item)) goto fail;
            }
            break;
        case TAG_Compound:
            if (!validate_count(w, tag->value.compound.count,
                                tag->value.compound.items, "TAG_Compound")) goto fail;
            for (int i = 0; i < tag->value.compound.count; ++i) {
                if (!write_named_tag(w, tag->value.compound.items[i])) goto fail;
            }
            if (!write_u8(w, TAG_End)) goto fail;
            break;
        case TAG_Int_Array:
            if (!validate_count(w, tag->value.int_array.length,
                                tag->value.int_array.data, "TAG_Int_Array") ||
                !write_u32(w, (uint32_t)tag->value.int_array.length)) goto fail;
            for (int32_t i = 0; i < tag->value.int_array.length; ++i) {
                if (!write_u32(w, (uint32_t)tag->value.int_array.data[i])) goto fail;
            }
            break;
        case TAG_Long_Array:
            if (!validate_count(w, tag->value.long_array.length,
                                tag->value.long_array.data, "TAG_Long_Array") ||
                !write_u32(w, (uint32_t)tag->value.long_array.length)) goto fail;
            for (int32_t i = 0; i < tag->value.long_array.length; ++i) {
                if (!write_u64(w, (uint64_t)tag->value.long_array.data[i])) goto fail;
            }
            break;
        case TAG_End:
        default:
            writer_error(w, "TAG_End cannot be serialized as a value");
            goto fail;
    }
    --w->depth;
    return 1;

fail:
    --w->depth;
    return 0;
}

int nbt_binary_serialize(
    const NBTTag* root,
    NBTBinaryFormat format,
    uint32_t bedrock_storage_version,
    unsigned char** out_data,
    size_t* out_size,
    char* err,
    size_t err_sz
) {
    BinaryWriter writer;
    size_t header_size = format == NBT_BINARY_BEDROCK_LEVEL_DAT ? 8 : 0;
    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;
    if (err && err_sz > 0) err[0] = '\0';
    if (!root || !out_data || !out_size) {
        set_error(err, err_sz, "invalid binary NBT serialization arguments");
        return 0;
    }
    if (format != NBT_BINARY_JAVA && format != NBT_BINARY_BEDROCK &&
        format != NBT_BINARY_BEDROCK_LEVEL_DAT) {
        set_error(err, err_sz, "AUTO is not a binary NBT output format");
        return 0;
    }
    memset(&writer, 0, sizeof(writer));
    writer.little_endian = format != NBT_BINARY_JAVA;
    writer.err = err;
    writer.err_sz = err_sz;
    if (header_size && !writer_reserve(&writer, header_size)) goto fail;
    writer.size = header_size;
    if (!write_named_tag(&writer, root)) goto fail;
    if (header_size) {
        size_t payload_size = writer.size - header_size;
        if (payload_size > UINT32_MAX) {
            writer_error(&writer, "Bedrock level.dat payload exceeds 4 GiB");
            goto fail;
        }
        writer.data[0] = (unsigned char)bedrock_storage_version;
        writer.data[1] = (unsigned char)(bedrock_storage_version >> 8);
        writer.data[2] = (unsigned char)(bedrock_storage_version >> 16);
        writer.data[3] = (unsigned char)(bedrock_storage_version >> 24);
        writer.data[4] = (unsigned char)payload_size;
        writer.data[5] = (unsigned char)(payload_size >> 8);
        writer.data[6] = (unsigned char)(payload_size >> 16);
        writer.data[7] = (unsigned char)(payload_size >> 24);
    }
    *out_data = writer.data;
    *out_size = writer.size;
    return 1;

fail:
    free(writer.data);
    return 0;
}

const char* nbt_binary_format_name(NBTBinaryFormat format) {
    switch (format) {
        case NBT_BINARY_AUTO: return "auto";
        case NBT_BINARY_JAVA: return "java-big-endian";
        case NBT_BINARY_BEDROCK: return "bedrock-little-endian";
        case NBT_BINARY_BEDROCK_LEVEL_DAT: return "bedrock-level.dat";
        default: return "unknown";
    }
}
