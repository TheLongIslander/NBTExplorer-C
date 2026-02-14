#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "edit_path.h"
#include "edit_save.h"
#include "edit_value.h"
#include "nbt_builder.h"

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

static void set_err(char* err, size_t err_sz, const char* msg) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", msg);
    }
}

static NBTTag* find_child_by_name(NBTTag* compound, const char* name, int* out_index) {
    if (!compound || compound->type != TAG_Compound || !name) return NULL;

    for (int i = 0; i < compound->value.compound.count; i++) {
        NBTTag* child = compound->value.compound.items[i];
        if (child && child->name && strcmp(child->name, name) == 0) {
            if (out_index) *out_index = i;
            return child;
        }
    }

    return NULL;
}

static int append_compound_child(NBTTag* compound, NBTTag* child) {
    int new_count;
    NBTTag** new_items;

    if (!compound || compound->type != TAG_Compound || !child) return 0;

    new_count = compound->value.compound.count + 1;
    new_items = realloc(compound->value.compound.items, (size_t)new_count * sizeof(NBTTag*));
    if (!new_items) return 0;

    compound->value.compound.items = new_items;
    compound->value.compound.items[new_count - 1] = child;
    compound->value.compound.count = new_count;
    return 1;
}

static void remove_compound_child_at(NBTTag* compound, int index) {
    int count;
    NBTTag** new_items;

    if (!compound || compound->type != TAG_Compound) return;

    count = compound->value.compound.count;
    if (index < 0 || index >= count) return;

    free_nbt_tree(compound->value.compound.items[index]);
    if (index < count - 1) {
        memmove(
            &compound->value.compound.items[index],
            &compound->value.compound.items[index + 1],
            (size_t)(count - index - 1) * sizeof(NBTTag*)
        );
    }

    count--;
    compound->value.compound.count = count;

    if (count == 0) {
        free(compound->value.compound.items);
        compound->value.compound.items = NULL;
        return;
    }

    new_items = realloc(compound->value.compound.items, (size_t)count * sizeof(NBTTag*));
    if (new_items) {
        compound->value.compound.items = new_items;
    }
}

static EditStatus delete_list_element(NBTTag* list_tag, int index, char* err, size_t err_sz) {
    int new_count;
    NBTTag** new_items;

    if (!list_tag || list_tag->type != TAG_List) {
        set_err(err, err_sz, "type mismatch: target is not a list");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    if (index < 0 || index >= list_tag->value.list.count) {
        set_err(err, err_sz, "index out of bounds");
        return EDIT_ERR_INDEX_BOUNDS;
    }

    free_nbt_tree(list_tag->value.list.items[index]);
    if (index < list_tag->value.list.count - 1) {
        memmove(
            &list_tag->value.list.items[index],
            &list_tag->value.list.items[index + 1],
            (size_t)(list_tag->value.list.count - index - 1) * sizeof(NBTTag*)
        );
    }

    new_count = list_tag->value.list.count - 1;
    list_tag->value.list.count = new_count;

    if (new_count == 0) {
        free(list_tag->value.list.items);
        list_tag->value.list.items = NULL;
        return EDIT_OK;
    }

    new_items = realloc(list_tag->value.list.items, (size_t)new_count * sizeof(NBTTag*));
    if (new_items) {
        list_tag->value.list.items = new_items;
    }

    return EDIT_OK;
}

static EditStatus delete_array_element(NBTTag* array_tag, int index, char* err, size_t err_sz) {
    if (!array_tag) {
        set_err(err, err_sz, "invalid array target");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    switch (array_tag->type) {
        case TAG_Byte_Array: {
            int len = array_tag->value.byte_array.length;
            uint8_t* new_data;

            if (index < 0 || index >= len) {
                set_err(err, err_sz, "index out of bounds");
                return EDIT_ERR_INDEX_BOUNDS;
            }

            if (index < len - 1) {
                memmove(
                    &array_tag->value.byte_array.data[index],
                    &array_tag->value.byte_array.data[index + 1],
                    (size_t)(len - index - 1)
                );
            }
            len--;
            array_tag->value.byte_array.length = len;

            if (len == 0) {
                free(array_tag->value.byte_array.data);
                array_tag->value.byte_array.data = NULL;
                return EDIT_OK;
            }

            new_data = realloc(array_tag->value.byte_array.data, (size_t)len);
            if (new_data) array_tag->value.byte_array.data = new_data;
            return EDIT_OK;
        }

        case TAG_Int_Array: {
            int len = array_tag->value.int_array.length;
            int32_t* new_data;

            if (index < 0 || index >= len) {
                set_err(err, err_sz, "index out of bounds");
                return EDIT_ERR_INDEX_BOUNDS;
            }

            if (index < len - 1) {
                memmove(
                    &array_tag->value.int_array.data[index],
                    &array_tag->value.int_array.data[index + 1],
                    (size_t)(len - index - 1) * sizeof(int32_t)
                );
            }
            len--;
            array_tag->value.int_array.length = len;

            if (len == 0) {
                free(array_tag->value.int_array.data);
                array_tag->value.int_array.data = NULL;
                return EDIT_OK;
            }

            new_data = realloc(array_tag->value.int_array.data, (size_t)len * sizeof(int32_t));
            if (new_data) array_tag->value.int_array.data = new_data;
            return EDIT_OK;
        }

        case TAG_Long_Array: {
            int len = array_tag->value.long_array.length;
            int64_t* new_data;

            if (index < 0 || index >= len) {
                set_err(err, err_sz, "index out of bounds");
                return EDIT_ERR_INDEX_BOUNDS;
            }

            if (index < len - 1) {
                memmove(
                    &array_tag->value.long_array.data[index],
                    &array_tag->value.long_array.data[index + 1],
                    (size_t)(len - index - 1) * sizeof(int64_t)
                );
            }
            len--;
            array_tag->value.long_array.length = len;

            if (len == 0) {
                free(array_tag->value.long_array.data);
                array_tag->value.long_array.data = NULL;
                return EDIT_OK;
            }

            new_data = realloc(array_tag->value.long_array.data, (size_t)len * sizeof(int64_t));
            if (new_data) array_tag->value.long_array.data = new_data;
            return EDIT_OK;
        }

        default:
            set_err(err, err_sz, "type mismatch: target is not an editable array");
            return EDIT_ERR_TYPE_MISMATCH;
    }
}

static EditStatus edit_single_target(const PathTarget* target, const char* value_expr, char* err, size_t err_sz) {
    switch (target->kind) {
        case PATH_TARGET_TAG:
            if (target->tag->type == TAG_Compound) {
                return apply_json_patch_to_compound(target->tag, value_expr, err, err_sz);
            }
            return parse_json_for_tag_type(target->tag, value_expr, err, err_sz);

        case PATH_TARGET_LIST_ELEMENT:
            return parse_json_for_list_element(target->tag, target->index, value_expr, err, err_sz);

        case PATH_TARGET_BYTE_ARRAY_ELEMENT:
        case PATH_TARGET_INT_ARRAY_ELEMENT:
        case PATH_TARGET_LONG_ARRAY_ELEMENT:
            return parse_json_for_array_element(target->tag, target->index, value_expr, err, err_sz);

        default:
            set_err(err, err_sz, "unsupported path target kind");
            return EDIT_ERR_UNSUPPORTED;
    }
}

static uintptr_t delete_container_key(const PathTarget* t) {
    if (!t) return 0;
    if (t->kind == PATH_TARGET_TAG) return (uintptr_t)t->parent;
    return (uintptr_t)t->tag;
}

static int compare_delete_targets(const void* a, const void* b) {
    const PathTarget* ta = (const PathTarget*)a;
    const PathTarget* tb = (const PathTarget*)b;
    uintptr_t ca = delete_container_key(ta);
    uintptr_t cb = delete_container_key(tb);

    if (ca < cb) return -1;
    if (ca > cb) return 1;

    if ((int)ta->kind < (int)tb->kind) return -1;
    if ((int)ta->kind > (int)tb->kind) return 1;

    if (ta->index > tb->index) return -1;
    if (ta->index < tb->index) return 1;
    return 0;
}

static EditStatus delete_single_target(const PathTarget* target, NBTTag* root, char* err, size_t err_sz) {
    switch (target->kind) {
        case PATH_TARGET_LIST_ELEMENT:
            return delete_list_element(target->tag, target->index, err, err_sz);

        case PATH_TARGET_BYTE_ARRAY_ELEMENT:
        case PATH_TARGET_INT_ARRAY_ELEMENT:
        case PATH_TARGET_LONG_ARRAY_ELEMENT:
            return delete_array_element(target->tag, target->index, err, err_sz);

        case PATH_TARGET_TAG:
            if (target->tag == root || !target->parent) {
                set_err(err, err_sz, "unsupported operation: cannot delete root tag");
                return EDIT_ERR_UNSUPPORTED;
            }

            if (target->parent->type == TAG_Compound) {
                if (target->index < 0 || target->index >= target->parent->value.compound.count) {
                    set_err(err, err_sz, "path not found");
                    return EDIT_ERR_PATH_NOT_FOUND;
                }
                remove_compound_child_at(target->parent, target->index);
                return EDIT_OK;
            }

            if (target->parent->type == TAG_List) {
                return delete_list_element(target->parent, target->index, err, err_sz);
            }

            set_err(err, err_sz, "unsupported operation");
            return EDIT_ERR_UNSUPPORTED;

        default:
            set_err(err, err_sz, "unsupported operation");
            return EDIT_ERR_UNSUPPORTED;
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
    PathTarget* targets = NULL;
    size_t count = 0;
    EditStatus st;

    st = resolve_edit_paths(root, path, &targets, &count, err, err_sz);
    if (st != EDIT_OK) return st;

    for (size_t i = 0; i < count; i++) {
        st = edit_single_target(&targets[i], value_expr, err, err_sz);
        if (st != EDIT_OK) {
            free_edit_paths(targets);
            return st;
        }
    }

    free_edit_paths(targets);
    return EDIT_OK;
}

EditStatus set_tag_by_path(NBTTag* root, const char* path, const char* value_expr, char* err, size_t err_sz) {
    EditStatus st;
    char* key = NULL;
    NBTTag* parent = NULL;
    NBTTag* existing = NULL;
    NBTTag* new_tag = NULL;

    st = edit_tag_by_path(root, path, value_expr, err, err_sz);
    if (st == EDIT_OK) return EDIT_OK;
    if (st != EDIT_ERR_PATH_NOT_FOUND) return st;

    st = resolve_set_parent_and_key(root, path, &parent, &key, err, err_sz);
    if (st != EDIT_OK) goto done;

    existing = find_child_by_name(parent, key, NULL);
    if (existing) {
        st = parse_json_for_tag_type(existing, value_expr, err, err_sz);
        goto done;
    }

    st = create_tag_from_json_expr(key, value_expr, &new_tag, err, err_sz);
    if (st != EDIT_OK) goto done;

    if (!append_compound_child(parent, new_tag)) {
        free_nbt_tree(new_tag);
        set_err(err, err_sz, "out of memory");
        st = EDIT_ERR_MEMORY;
        goto done;
    }

    st = EDIT_OK;

done:
    free(key);
    return st;
}

EditStatus delete_tag_by_path(NBTTag* root, const char* path, char* err, size_t err_sz) {
    PathTarget* targets = NULL;
    size_t count = 0;
    EditStatus st;

    if (!root || !path) {
        set_err(err, err_sz, "invalid delete arguments");
        return EDIT_ERR_PATH_SYNTAX;
    }

    st = resolve_edit_paths(root, path, &targets, &count, err, err_sz);
    if (st != EDIT_OK) return st;

    qsort(targets, count, sizeof(PathTarget), compare_delete_targets);
    for (size_t i = 0; i < count; i++) {
        st = delete_single_target(&targets[i], root, err, err_sz);
        if (st != EDIT_OK) {
            free_edit_paths(targets);
            return st;
        }
    }

    free_edit_paths(targets);
    return EDIT_OK;
}
