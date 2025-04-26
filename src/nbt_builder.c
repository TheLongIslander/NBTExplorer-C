#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nbt_parser.h"
#include "nbt_utils.h"

static char* read_nbt_name(const unsigned char* data, size_t* offset) {
    uint16_t len = read_u16(data, offset);
    char* str = malloc(len + 1);
    if (!str) return NULL;
    memcpy(str, data + *offset, len);
    str[len] = '\0';
    *offset += len;
    return str;
}

NBTTag* build_nbt_tree(const unsigned char* data, size_t* offset) {
    uint8_t tag_type = read_u8(data, offset);
    if (tag_type == TAG_End) return NULL;

    NBTTag* tag = malloc(sizeof(NBTTag));
    if (!tag) return NULL;

    tag->type = tag_type;
    tag->name = read_nbt_name(data, offset);

    switch (tag_type) {
        case TAG_Byte:
            tag->value.byte_val = read_u8(data, offset);
            break;
        case TAG_Short:
            tag->value.short_val = (int16_t)read_u16(data, offset);
            break;
        case TAG_Int:
            tag->value.int_val = read_i32(data, offset);
            break;
        case TAG_Long:
            tag->value.long_val = read_i64(data, offset);
            break;
        case TAG_Float: {
            union { uint32_t i; float f; } u;
            u.i = read_i32(data, offset);
            tag->value.float_val = u.f;
            break;
        }
        case TAG_Double: {
            union { uint64_t i; double d; } u;
            u.i = read_i64(data, offset);
            tag->value.double_val = u.d;
            break;
        }
        case TAG_String: {
            uint16_t len = read_u16(data, offset);
            tag->value.string_val = malloc(len + 1);
            if (tag->value.string_val) {
                memcpy(tag->value.string_val, data + *offset, len);
                tag->value.string_val[len] = '\0';
            }
            *offset += len;
            break;
        }
        case TAG_Byte_Array: {
            int32_t len = read_i32(data, offset);
            tag->value.byte_array.length = len;
            tag->value.byte_array.data = malloc(len);
            if (tag->value.byte_array.data) {
                memcpy(tag->value.byte_array.data, data + *offset, len);
            }
            *offset += len;
            break;
        }
        case TAG_Int_Array: {
            int32_t len = read_i32(data, offset);
            tag->value.int_array.length = len;
            tag->value.int_array.data = malloc(len * sizeof(int32_t));
            if (tag->value.int_array.data) {
                for (int i = 0; i < len; i++) {
                    tag->value.int_array.data[i] = read_i32(data, offset);
                }
            }
            break;
        }
        case TAG_Long_Array: {
            int32_t len = read_i32(data, offset);
            tag->value.long_array.length = len;
            tag->value.long_array.data = malloc(len * sizeof(int64_t));
            if (tag->value.long_array.data) {
                for (int i = 0; i < len; i++) {
                    tag->value.long_array.data[i] = read_i64(data, offset);
                }
            }
            break;
        }
        case TAG_Compound: {
            tag->value.compound.count = 0;
            tag->value.compound.items = NULL;

            while (data[*offset] != TAG_End) {
                NBTTag* child = build_nbt_tree(data, offset);
                if (!child) break;

                tag->value.compound.count++;
                tag->value.compound.items = realloc(
                    tag->value.compound.items,
                    tag->value.compound.count * sizeof(NBTTag*)
                );
                tag->value.compound.items[tag->value.compound.count - 1] = child;
            }
            (*offset)++; // consume TAG_End
            break;
        }
        case TAG_List: {
            uint8_t elem_type = read_u8(data, offset);
            int32_t count = read_i32(data, offset);
            tag->value.list.count = count;
            tag->value.list.element_type = elem_type;
            tag->value.list.items = calloc(count, sizeof(NBTTag*)); // <-- calloc (sets NULLs!)

            for (int i = 0; i < count; i++) {
                NBTTag* elem = malloc(sizeof(NBTTag));
                if (!elem) continue;

                elem->type = elem_type;
                elem->name = strdup(""); // Lists elements unnamed

                switch (elem_type) {
                    case TAG_Byte:
                        elem->value.byte_val = read_u8(data, offset);
                        break;
                    case TAG_Short:
                        elem->value.short_val = (int16_t)read_u16(data, offset);
                        break;
                    case TAG_Int:
                        elem->value.int_val = read_i32(data, offset);
                        break;
                    case TAG_Long:
                        elem->value.long_val = read_i64(data, offset);
                        break;
                    case TAG_Float: {
                        union { uint32_t i; float f; } u;
                        u.i = read_i32(data, offset);
                        elem->value.float_val = u.f;
                        break;
                    }
                    case TAG_Double: {
                        union { uint64_t i; double d; } u;
                        u.i = read_i64(data, offset);
                        elem->value.double_val = u.d;
                        break;
                    }
                    case TAG_String: {
                        uint16_t len = read_u16(data, offset);
                        elem->value.string_val = malloc(len + 1);
                        if (elem->value.string_val) {
                            memcpy(elem->value.string_val, data + *offset, len);
                            elem->value.string_val[len] = '\0';
                        }
                        *offset += len;
                        break;
                    }
                    case TAG_Compound:
                        free(elem); // We'll reassign below
                        elem = build_nbt_tree(data, offset);
                        break;
                    default:
                        printf("[WARN] Unsupported list element type %d — skipping element\n", elem_type);
                        free(elem);
                        elem = NULL;
                        break;
                }

                tag->value.list.items[i] = elem; // Safe even if elem == NULL
            }
            break;
        }
        default:
            free(tag->name);
            free(tag);
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
