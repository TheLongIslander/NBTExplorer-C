#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "edit_path.h"
#include "edit_save.h"
#include "edit_value.h"

static void write_payload(gzFile f, NBTTag* tag);

static void write_string(gzFile f, const char* str) {
    if (!str) str = "";

    uint16_t len = (uint16_t)strlen(str);
    uint8_t buf[2] = { (uint8_t)((len >> 8) & 0xFF), (uint8_t)(len & 0xFF) };

    gzwrite(f, buf, 2);
    gzwrite(f, str, len);
}

static void write_payload(gzFile f, NBTTag* tag) {
    if (!tag) return;

    switch (tag->type) {
        case TAG_Byte:
            gzwrite(f, &tag->value.byte_val, 1);
            break;

        case TAG_Short: {
            int16_t v = tag->value.short_val;
            uint8_t buf[2] = {
                (uint8_t)((v >> 8) & 0xFF),
                (uint8_t)(v & 0xFF)
            };
            gzwrite(f, buf, 2);
            break;
        }

        case TAG_Int: {
            int32_t v = tag->value.int_val;
            uint8_t buf[4] = {
                (uint8_t)((v >> 24) & 0xFF),
                (uint8_t)((v >> 16) & 0xFF),
                (uint8_t)((v >> 8) & 0xFF),
                (uint8_t)(v & 0xFF)
            };
            gzwrite(f, buf, 4);
            break;
        }

        case TAG_Long: {
            int64_t v = tag->value.long_val;
            uint8_t buf[8];
            for (int i = 0; i < 8; i++) {
                buf[7 - i] = (uint8_t)((v >> (i * 8)) & 0xFF);
            }
            gzwrite(f, buf, 8);
            break;
        }

        case TAG_Float: {
            union {
                float f;
                uint32_t i;
            } u;
            u.f = tag->value.float_val;
            uint8_t buf[4] = {
                (uint8_t)((u.i >> 24) & 0xFF),
                (uint8_t)((u.i >> 16) & 0xFF),
                (uint8_t)((u.i >> 8) & 0xFF),
                (uint8_t)(u.i & 0xFF)
            };
            gzwrite(f, buf, 4);
            break;
        }

        case TAG_Double: {
            union {
                double d;
                uint64_t i;
            } u;
            u.d = tag->value.double_val;
            uint8_t buf[8];
            for (int i = 0; i < 8; i++) {
                buf[7 - i] = (uint8_t)((u.i >> (i * 8)) & 0xFF);
            }
            gzwrite(f, buf, 8);
            break;
        }

        case TAG_Byte_Array: {
            int32_t len = tag->value.byte_array.length;
            uint8_t len_buf[4] = {
                (uint8_t)((len >> 24) & 0xFF),
                (uint8_t)((len >> 16) & 0xFF),
                (uint8_t)((len >> 8) & 0xFF),
                (uint8_t)(len & 0xFF)
            };
            gzwrite(f, len_buf, 4);
            if (len > 0 && tag->value.byte_array.data) {
                gzwrite(f, tag->value.byte_array.data, len);
            }
            break;
        }

        case TAG_String:
            write_string(f, tag->value.string_val);
            break;

        case TAG_List: {
            int32_t real_count = 0;
            uint8_t elem_type = (uint8_t)tag->value.list.element_type;

            gzputc(f, elem_type);

            for (int i = 0; i < tag->value.list.count; i++) {
                if (tag->value.list.items[i] && tag->value.list.items[i]->type == tag->value.list.element_type) {
                    real_count++;
                }
            }

            uint8_t len_buf[4] = {
                (uint8_t)((real_count >> 24) & 0xFF),
                (uint8_t)((real_count >> 16) & 0xFF),
                (uint8_t)((real_count >> 8) & 0xFF),
                (uint8_t)(real_count & 0xFF)
            };
            gzwrite(f, len_buf, 4);

            for (int i = 0; i < tag->value.list.count; i++) {
                if (tag->value.list.items[i] && tag->value.list.items[i]->type == tag->value.list.element_type) {
                    write_payload(f, tag->value.list.items[i]);
                }
            }
            break;
        }

        case TAG_Compound:
            for (int i = 0; i < tag->value.compound.count; i++) {
                if (tag->value.compound.items[i]) {
                    write_tag(f, tag->value.compound.items[i]);
                }
            }
            gzputc(f, TAG_End);
            break;

        case TAG_Int_Array: {
            int32_t len = tag->value.int_array.length;
            uint8_t len_buf[4] = {
                (uint8_t)((len >> 24) & 0xFF),
                (uint8_t)((len >> 16) & 0xFF),
                (uint8_t)((len >> 8) & 0xFF),
                (uint8_t)(len & 0xFF)
            };
            gzwrite(f, len_buf, 4);
            for (int32_t i = 0; i < len; i++) {
                int32_t v = tag->value.int_array.data[i];
                uint8_t buf[4] = {
                    (uint8_t)((v >> 24) & 0xFF),
                    (uint8_t)((v >> 16) & 0xFF),
                    (uint8_t)((v >> 8) & 0xFF),
                    (uint8_t)(v & 0xFF)
                };
                gzwrite(f, buf, 4);
            }
            break;
        }

        case TAG_Long_Array: {
            int32_t len = tag->value.long_array.length;
            uint8_t len_buf[4] = {
                (uint8_t)((len >> 24) & 0xFF),
                (uint8_t)((len >> 16) & 0xFF),
                (uint8_t)((len >> 8) & 0xFF),
                (uint8_t)(len & 0xFF)
            };
            gzwrite(f, len_buf, 4);
            for (int32_t i = 0; i < len; i++) {
                int64_t v = tag->value.long_array.data[i];
                uint8_t buf[8];
                for (int b = 0; b < 8; b++) {
                    buf[7 - b] = (uint8_t)((v >> (b * 8)) & 0xFF);
                }
                gzwrite(f, buf, 8);
            }
            break;
        }

        default:
            break;
    }
}

void write_tag(gzFile f, NBTTag* tag) {
    if (!tag) return;

    gzputc(f, tag->type);
    write_string(f, tag->name ? tag->name : "");
    write_payload(f, tag);
}

const char* edit_status_name(EditStatus status) {
    switch (status) {
        case EDIT_OK:
            return "ok";
        case EDIT_ERR_PATH_SYNTAX:
            return "invalid path syntax";
        case EDIT_ERR_PATH_NOT_FOUND:
            return "path not found";
        case EDIT_ERR_INDEX_BOUNDS:
            return "index out of bounds";
        case EDIT_ERR_TYPE_MISMATCH:
            return "type mismatch";
        case EDIT_ERR_INVALID_JSON:
            return "invalid json";
        case EDIT_ERR_NUMERIC_RANGE:
            return "numeric overflow";
        case EDIT_ERR_UNSUPPORTED:
            return "unsupported operation";
        case EDIT_ERR_MEMORY:
            return "out of memory";
        default:
            return "unknown edit error";
    }
}

NBTTag* find_tag_by_path(NBTTag* root, const char* path) {
    PathTarget target;
    char err[128];

    if (resolve_edit_path(root, path, &target, err, sizeof(err)) != EDIT_OK) {
        return NULL;
    }

    if (target.kind == PATH_TARGET_TAG) {
        return target.tag;
    }

    if (target.kind == PATH_TARGET_LIST_ELEMENT) {
        if (!target.tag || target.tag->type != TAG_List) return NULL;
        if (target.index < 0 || target.index >= target.tag->value.list.count) return NULL;
        return target.tag->value.list.items[target.index];
    }

    return NULL;
}

EditStatus edit_tag_by_path(NBTTag* root, const char* path, const char* value_expr, char* err, size_t err_sz) {
    PathTarget target;
    EditStatus st;

    st = resolve_edit_path(root, path, &target, err, err_sz);
    if (st != EDIT_OK) return st;

    switch (target.kind) {
        case PATH_TARGET_TAG:
            if (target.tag->type == TAG_Compound) {
                return apply_json_patch_to_compound(target.tag, value_expr, err, err_sz);
            }
            return parse_json_for_tag_type(target.tag, value_expr, err, err_sz);

        case PATH_TARGET_LIST_ELEMENT:
            return parse_json_for_list_element(target.tag, target.index, value_expr, err, err_sz);

        case PATH_TARGET_BYTE_ARRAY_ELEMENT:
        case PATH_TARGET_INT_ARRAY_ELEMENT:
        case PATH_TARGET_LONG_ARRAY_ELEMENT:
            return parse_json_for_array_element(target.tag, target.index, value_expr, err, err_sz);

        default:
            if (err && err_sz > 0) {
                snprintf(err, err_sz, "unsupported path target kind");
            }
            return EDIT_ERR_UNSUPPORTED;
    }
}
