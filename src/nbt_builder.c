#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nbt_builder.h"
#include "nbt_utils.h"

static void set_err(char* err, size_t err_sz, const char* msg) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", msg);
    }
}

static int is_valid_tag_type(uint8_t tag_type) {
    return tag_type >= TAG_End && tag_type <= TAG_Long_Array;
}

static NBTTag* create_empty_tag(TagType type, const char* name) {
    NBTTag* tag = malloc(sizeof(NBTTag));
    if (!tag) return NULL;

    memset(tag, 0, sizeof(NBTTag));
    tag->type = type;
    tag->name = strdup(name ? name : "");
    if (!tag->name) {
        free(tag);
        return NULL;
    }

    if (type == TAG_String) {
        tag->value.string_val = strdup("");
        if (!tag->value.string_val) {
            free(tag->name);
            free(tag);
            return NULL;
        }
    } else if (type == TAG_List) {
        tag->value.list.element_type = TAG_End;
    }

    return tag;
}

static int append_compound_child(NBTTag* compound, NBTTag* child) {
    int new_count = compound->value.compound.count + 1;
    NBTTag** new_items = realloc(compound->value.compound.items, (size_t)new_count * sizeof(NBTTag*));
    if (!new_items) return 0;
    compound->value.compound.items = new_items;
    compound->value.compound.items[new_count - 1] = child;
    compound->value.compound.count = new_count;
    return 1;
}

static void free_partial_list_items(NBTTag** items, int count) {
    if (!items) return;
    for (int i = 0; i < count; i++) {
        free_nbt_tree(items[i]);
    }
    free(items);
}

static char* read_nbt_name(NBTReader* reader) {
    uint16_t len = 0;
    char* str;

    if (!nbt_read_u16(reader, &len)) return NULL;
    str = malloc((size_t)len + 1);
    if (!str) return NULL;
    if (!nbt_read_bytes(reader, (unsigned char*)str, len)) {
        free(str);
        return NULL;
    }
    str[len] = '\0';
    return str;
}

static NBTTag* build_tag_from_reader(NBTReader* reader, char* err, size_t err_sz);

static int parse_payload(NBTTag* tag, NBTReader* reader, char* err, size_t err_sz) {
    switch (tag->type) {
        case TAG_End:
            return 1;

        case TAG_Byte: {
            uint8_t v = 0;
            if (!nbt_read_u8(reader, &v)) return 0;
            tag->value.byte_val = (int8_t)v;
            return 1;
        }

        case TAG_Short: {
            uint16_t v = 0;
            if (!nbt_read_u16(reader, &v)) return 0;
            tag->value.short_val = (int16_t)v;
            return 1;
        }

        case TAG_Int: {
            int32_t v = 0;
            if (!nbt_read_i32(reader, &v)) return 0;
            tag->value.int_val = v;
            return 1;
        }

        case TAG_Long: {
            int64_t v = 0;
            if (!nbt_read_i64(reader, &v)) return 0;
            tag->value.long_val = v;
            return 1;
        }

        case TAG_Float: {
            union {
                uint32_t i;
                float f;
            } u;
            int32_t raw = 0;
            if (!nbt_read_i32(reader, &raw)) return 0;
            u.i = (uint32_t)raw;
            tag->value.float_val = u.f;
            return 1;
        }

        case TAG_Double: {
            union {
                uint64_t i;
                double d;
            } u;
            int64_t raw = 0;
            if (!nbt_read_i64(reader, &raw)) return 0;
            u.i = (uint64_t)raw;
            tag->value.double_val = u.d;
            return 1;
        }

        case TAG_String: {
            uint16_t len = 0;
            char* str;
            if (!nbt_read_u16(reader, &len)) return 0;
            str = realloc(tag->value.string_val, (size_t)len + 1);
            if (!str) {
                set_err(err, err_sz, "out of memory while parsing string");
                return 0;
            }
            tag->value.string_val = str;
            if (!nbt_read_bytes(reader, (unsigned char*)tag->value.string_val, len)) return 0;
            tag->value.string_val[len] = '\0';
            return 1;
        }

        case TAG_Byte_Array: {
            int32_t len = 0;
            if (!nbt_read_i32(reader, &len)) return 0;
            if (len < 0) {
                set_err(err, err_sz, "corrupt NBT: negative TAG_Byte_Array length");
                return 0;
            }
            tag->value.byte_array.length = len;
            if (len == 0) {
                tag->value.byte_array.data = NULL;
                return 1;
            }
            tag->value.byte_array.data = malloc((size_t)len);
            if (!tag->value.byte_array.data) {
                set_err(err, err_sz, "out of memory while parsing TAG_Byte_Array");
                return 0;
            }
            if (!nbt_read_bytes(reader, tag->value.byte_array.data, (size_t)len)) return 0;
            return 1;
        }

        case TAG_Int_Array: {
            int32_t len = 0;
            if (!nbt_read_i32(reader, &len)) return 0;
            if (len < 0) {
                set_err(err, err_sz, "corrupt NBT: negative TAG_Int_Array length");
                return 0;
            }
            tag->value.int_array.length = len;
            if (len == 0) {
                tag->value.int_array.data = NULL;
                return 1;
            }
            if ((size_t)len > SIZE_MAX / sizeof(int32_t)) {
                set_err(err, err_sz, "corrupt NBT: TAG_Int_Array length overflow");
                return 0;
            }
            tag->value.int_array.data = malloc((size_t)len * sizeof(int32_t));
            if (!tag->value.int_array.data) {
                set_err(err, err_sz, "out of memory while parsing TAG_Int_Array");
                return 0;
            }
            for (int32_t i = 0; i < len; i++) {
                if (!nbt_read_i32(reader, &tag->value.int_array.data[i])) return 0;
            }
            return 1;
        }

        case TAG_Long_Array: {
            int32_t len = 0;
            if (!nbt_read_i32(reader, &len)) return 0;
            if (len < 0) {
                set_err(err, err_sz, "corrupt NBT: negative TAG_Long_Array length");
                return 0;
            }
            tag->value.long_array.length = len;
            if (len == 0) {
                tag->value.long_array.data = NULL;
                return 1;
            }
            if ((size_t)len > SIZE_MAX / sizeof(int64_t)) {
                set_err(err, err_sz, "corrupt NBT: TAG_Long_Array length overflow");
                return 0;
            }
            tag->value.long_array.data = malloc((size_t)len * sizeof(int64_t));
            if (!tag->value.long_array.data) {
                set_err(err, err_sz, "out of memory while parsing TAG_Long_Array");
                return 0;
            }
            for (int32_t i = 0; i < len; i++) {
                if (!nbt_read_i64(reader, &tag->value.long_array.data[i])) return 0;
            }
            return 1;
        }

        case TAG_Compound: {
            tag->value.compound.count = 0;
            tag->value.compound.items = NULL;

            while (1) {
                uint8_t next_type = 0;
                NBTTag* child;

                if (!nbt_peek_u8(reader, &next_type)) return 0;
                if (next_type == TAG_End) {
                    if (!nbt_skip_bytes(reader, 1)) return 0;
                    break;
                }

                child = build_tag_from_reader(reader, err, err_sz);
                if (!child) return 0;
                if (!append_compound_child(tag, child)) {
                    free_nbt_tree(child);
                    set_err(err, err_sz, "out of memory while growing TAG_Compound");
                    return 0;
                }
            }

            return 1;
        }

        case TAG_List: {
            uint8_t elem_type = 0;
            int32_t count = 0;

            if (!nbt_read_u8(reader, &elem_type)) return 0;
            if (!is_valid_tag_type(elem_type)) {
                set_err(err, err_sz, "corrupt NBT: invalid TAG_List element type");
                return 0;
            }
            if (!nbt_read_i32(reader, &count)) return 0;
            if (count < 0) {
                set_err(err, err_sz, "corrupt NBT: negative TAG_List length");
                return 0;
            }

            tag->value.list.element_type = (TagType)elem_type;
            tag->value.list.count = count;
            tag->value.list.items = NULL;

            if (count == 0) return 1;
            if (elem_type == TAG_End) {
                set_err(err, err_sz, "corrupt NBT: TAG_List with TAG_End element type must be empty");
                return 0;
            }
            if ((size_t)count > SIZE_MAX / sizeof(NBTTag*)) {
                set_err(err, err_sz, "corrupt NBT: TAG_List length overflow");
                return 0;
            }

            tag->value.list.items = calloc((size_t)count, sizeof(NBTTag*));
            if (!tag->value.list.items) {
                set_err(err, err_sz, "out of memory while parsing TAG_List");
                return 0;
            }

            for (int32_t i = 0; i < count; i++) {
                NBTTag* elem = create_empty_tag((TagType)elem_type, "");
                if (!elem) {
                    set_err(err, err_sz, "out of memory while creating TAG_List element");
                    free_partial_list_items(tag->value.list.items, i);
                    tag->value.list.items = NULL;
                    tag->value.list.count = 0;
                    return 0;
                }
                if (!parse_payload(elem, reader, err, err_sz)) {
                    free_nbt_tree(elem);
                    free_partial_list_items(tag->value.list.items, i);
                    tag->value.list.items = NULL;
                    tag->value.list.count = 0;
                    return 0;
                }
                tag->value.list.items[i] = elem;
            }

            return 1;
        }

        default:
            set_err(err, err_sz, "corrupt NBT: unknown tag type");
            return 0;
    }
}

static NBTTag* build_tag_from_reader(NBTReader* reader, char* err, size_t err_sz) {
    uint8_t tag_type = TAG_End;
    char* name;
    NBTTag* tag;

    if (!nbt_read_u8(reader, &tag_type)) return NULL;
    if (!is_valid_tag_type(tag_type)) {
        set_err(err, err_sz, "corrupt NBT: invalid tag type");
        return NULL;
    }
    if (tag_type == TAG_End) {
        set_err(err, err_sz, "corrupt NBT: unexpected TAG_End tag");
        return NULL;
    }

    name = read_nbt_name(reader);
    if (!name) return NULL;

    tag = create_empty_tag((TagType)tag_type, name);
    free(name);
    if (!tag) {
        set_err(err, err_sz, "out of memory while creating NBT tag");
        return NULL;
    }

    if (!parse_payload(tag, reader, err, err_sz)) {
        free_nbt_tree(tag);
        return NULL;
    }

    return tag;
}

NBTTag* build_nbt_tree(const unsigned char* data, size_t data_size, size_t* offset, char* err, size_t err_sz) {
    NBTReader reader;
    NBTTag* root;

    if (!data) {
        set_err(err, err_sz, "invalid input buffer");
        return NULL;
    }

    nbt_reader_init(&reader, data, data_size);
    if (offset && !nbt_reader_set_offset(&reader, *offset)) {
        set_err(err, err_sz, nbt_reader_error(&reader));
        return NULL;
    }

    root = build_tag_from_reader(&reader, err, err_sz);
    if (!root) {
        if (nbt_reader_failed(&reader) && (!err || err[0] == '\0')) {
            set_err(err, err_sz, nbt_reader_error(&reader));
        }
        return NULL;
    }

    if (offset) {
        *offset = nbt_reader_offset(&reader);
    }

    return root;
}

void free_nbt_tree(NBTTag* tag) {
    if (!tag) return;
    free(tag->name);

    switch (tag->type) {
        case TAG_String:
            free(tag->value.string_val);
            break;

        case TAG_Byte_Array:
            free(tag->value.byte_array.data);
            break;

        case TAG_Int_Array:
            free(tag->value.int_array.data);
            break;

        case TAG_Long_Array:
            free(tag->value.long_array.data);
            break;

        case TAG_List:
            for (int i = 0; i < tag->value.list.count; i++) {
                free_nbt_tree(tag->value.list.items[i]);
            }
            free(tag->value.list.items);
            break;

        case TAG_Compound:
            for (int i = 0; i < tag->value.compound.count; i++) {
                free_nbt_tree(tag->value.compound.items[i]);
            }
            free(tag->value.compound.items);
            break;

        default:
            break;
    }

    free(tag);
}
