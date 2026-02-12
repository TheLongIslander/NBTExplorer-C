#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nbt_parser.h"
#include "nbt_utils.h"

void free_nbt_tree(NBTTag* tag);
NBTTag* build_nbt_tree(const unsigned char* data, size_t* offset);

static char* read_nbt_name(const unsigned char* data, size_t* offset) {
    uint16_t len = read_u16(data, offset);
    char* str = malloc(len + 1);
    if (!str) return NULL;
    memcpy(str, data + *offset, len);
    str[len] = '\0';
    *offset += len;
    return str;
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
    NBTTag** new_items = realloc(compound->value.compound.items, new_count * sizeof(NBTTag*));
    if (!new_items) return 0;
    compound->value.compound.items = new_items;
    compound->value.compound.items[new_count - 1] = child;
    compound->value.compound.count = new_count;
    return 1;
}

static int parse_payload(NBTTag* tag, const unsigned char* data, size_t* offset) {
    switch (tag->type) {
        case TAG_End:
            return 1;

        case TAG_Byte:
            tag->value.byte_val = (int8_t)read_u8(data, offset);
            return 1;

        case TAG_Short:
            tag->value.short_val = (int16_t)read_u16(data, offset);
            return 1;

        case TAG_Int:
            tag->value.int_val = read_i32(data, offset);
            return 1;

        case TAG_Long:
            tag->value.long_val = read_i64(data, offset);
            return 1;

        case TAG_Float: {
            union {
                uint32_t i;
                float f;
            } u;
            u.i = (uint32_t)read_i32(data, offset);
            tag->value.float_val = u.f;
            return 1;
        }

        case TAG_Double: {
            union {
                uint64_t i;
                double d;
            } u;
            u.i = (uint64_t)read_i64(data, offset);
            tag->value.double_val = u.d;
            return 1;
        }

        case TAG_String: {
            uint16_t len = read_u16(data, offset);
            char* str = realloc(tag->value.string_val, len + 1);
            if (!str) return 0;
            tag->value.string_val = str;
            memcpy(tag->value.string_val, data + *offset, len);
            tag->value.string_val[len] = '\0';
            *offset += len;
            return 1;
        }

        case TAG_Byte_Array: {
            int32_t len = read_i32(data, offset);
            if (len < 0) return 0;
            tag->value.byte_array.length = len;
            if (len == 0) {
                tag->value.byte_array.data = NULL;
                return 1;
            }
            tag->value.byte_array.data = malloc((size_t)len);
            if (!tag->value.byte_array.data) return 0;
            memcpy(tag->value.byte_array.data, data + *offset, (size_t)len);
            *offset += (size_t)len;
            return 1;
        }

        case TAG_Int_Array: {
            int32_t len = read_i32(data, offset);
            if (len < 0) return 0;
            tag->value.int_array.length = len;
            if (len == 0) {
                tag->value.int_array.data = NULL;
                return 1;
            }
            tag->value.int_array.data = malloc((size_t)len * sizeof(int32_t));
            if (!tag->value.int_array.data) return 0;
            for (int32_t i = 0; i < len; i++) {
                tag->value.int_array.data[i] = read_i32(data, offset);
            }
            return 1;
        }

        case TAG_Long_Array: {
            int32_t len = read_i32(data, offset);
            if (len < 0) return 0;
            tag->value.long_array.length = len;
            if (len == 0) {
                tag->value.long_array.data = NULL;
                return 1;
            }
            tag->value.long_array.data = malloc((size_t)len * sizeof(int64_t));
            if (!tag->value.long_array.data) return 0;
            for (int32_t i = 0; i < len; i++) {
                tag->value.long_array.data[i] = read_i64(data, offset);
            }
            return 1;
        }

        case TAG_Compound: {
            tag->value.compound.count = 0;
            tag->value.compound.items = NULL;

            while (1) {
                uint8_t next_type = read_u8(data, offset);
                if (next_type == TAG_End) {
                    break;
                }
                (*offset)--;

                NBTTag* child = build_nbt_tree(data, offset);
                if (!child) return 0;

                if (!append_compound_child(tag, child)) {
                    free_nbt_tree(child);
                    return 0;
                }
            }

            return 1;
        }

        case TAG_List: {
            uint8_t elem_type = read_u8(data, offset);
            int32_t count = read_i32(data, offset);
            if (count < 0) return 0;

            tag->value.list.element_type = (TagType)elem_type;
            tag->value.list.count = count;
            tag->value.list.items = NULL;

            if (count == 0) return 1;
            if (elem_type == TAG_End) return 0;

            tag->value.list.items = calloc((size_t)count, sizeof(NBTTag*));
            if (!tag->value.list.items) return 0;

            for (int32_t i = 0; i < count; i++) {
                NBTTag* elem = create_empty_tag((TagType)elem_type, "");
                if (!elem) return 0;

                if (!parse_payload(elem, data, offset)) {
                    free_nbt_tree(elem);
                    return 0;
                }

                tag->value.list.items[i] = elem;
            }

            return 1;
        }

        default:
            return 0;
    }
}

NBTTag* build_nbt_tree(const unsigned char* data, size_t* offset) {
    uint8_t tag_type = read_u8(data, offset);
    if (tag_type == TAG_End) return NULL;

    char* name = read_nbt_name(data, offset);
    if (!name) return NULL;

    NBTTag* tag = create_empty_tag((TagType)tag_type, name);
    free(name);
    if (!tag) return NULL;

    if (!parse_payload(tag, data, offset)) {
        free_nbt_tree(tag);
        return NULL;
    }

    return tag;
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
