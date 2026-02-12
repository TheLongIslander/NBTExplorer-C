#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "edit_path.h"

static void set_err(char* err, size_t err_sz, const char* msg) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", msg);
    }
}

static NBTTag* find_child_by_name(NBTTag* compound, const char* name) {
    if (!compound || compound->type != TAG_Compound) return NULL;

    for (int i = 0; i < compound->value.compound.count; i++) {
        NBTTag* child = compound->value.compound.items[i];
        if (child && child->name && strcmp(child->name, name) == 0) {
            return child;
        }
    }

    return NULL;
}

static EditStatus parse_segment(char* seg, char** key, int* has_index, int* index, char* err, size_t err_sz) {
    char* open;
    char* close;

    if (!seg || seg[0] == '\0') {
        set_err(err, err_sz, "invalid path syntax: empty segment");
        return EDIT_ERR_PATH_SYNTAX;
    }

    *has_index = 0;
    *index = -1;
    *key = seg;

    open = strchr(seg, '[');
    if (!open) return EDIT_OK;

    close = strchr(open, ']');
    if (!close || close[1] != '\0' || strchr(open + 1, '[') || strchr(close + 1, ']')) {
        set_err(err, err_sz, "invalid path syntax: malformed brackets");
        return EDIT_ERR_PATH_SYNTAX;
    }

    if (open == seg && close == open + 1) {
        set_err(err, err_sz, "invalid path syntax: empty index");
        return EDIT_ERR_PATH_SYNTAX;
    }

    *open = '\0';

    {
        char* p = open + 1;
        long value;

        if (*p == '\0' || p == close) {
            set_err(err, err_sz, "invalid path syntax: empty index");
            return EDIT_ERR_PATH_SYNTAX;
        }

        while (p < close) {
            if (!isdigit((unsigned char)*p)) {
                set_err(err, err_sz, "invalid path syntax: non-numeric index");
                return EDIT_ERR_PATH_SYNTAX;
            }
            p++;
        }

        *close = '\0';
        errno = 0;
        value = strtol(open + 1, NULL, 10);
        if (errno || value < 0 || value > INT_MAX) {
            set_err(err, err_sz, "invalid path syntax: index out of range");
            return EDIT_ERR_PATH_SYNTAX;
        }

        *has_index = 1;
        *index = (int)value;
    }

    return EDIT_OK;
}

EditStatus resolve_edit_path(NBTTag* root, const char* path, PathTarget* out, char* err, size_t err_sz) {
    char* copy;
    char* save;
    char* tok;
    char* segments[256];
    int count = 0;
    int start = 0;
    NBTTag* current;

    if (!root || !out || !path) {
        set_err(err, err_sz, "invalid argument");
        return EDIT_ERR_PATH_SYNTAX;
    }

    copy = strdup(path);
    if (!copy) {
        set_err(err, err_sz, "out of memory");
        return EDIT_ERR_MEMORY;
    }

    for (tok = strtok_r(copy, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        if (tok[0] == '\0') continue;
        if (count >= (int)(sizeof(segments) / sizeof(segments[0]))) {
            free(copy);
            set_err(err, err_sz, "invalid path syntax: too many segments");
            return EDIT_ERR_PATH_SYNTAX;
        }
        segments[count++] = tok;
    }

    current = root;

    if (count == 0) {
        out->kind = PATH_TARGET_TAG;
        out->tag = root;
        out->index = -1;
        free(copy);
        return EDIT_OK;
    }

    if (root->name && root->name[0] != '\0' && strcmp(segments[0], root->name) == 0) {
        start = 1;
    }

    if (start >= count) {
        out->kind = PATH_TARGET_TAG;
        out->tag = root;
        out->index = -1;
        free(copy);
        return EDIT_OK;
    }

    for (int i = start; i < count; i++) {
        int has_index = 0;
        int index = -1;
        int is_last = (i == count - 1);
        char* key = NULL;
        EditStatus st = parse_segment(segments[i], &key, &has_index, &index, err, err_sz);
        if (st != EDIT_OK) {
            free(copy);
            return st;
        }

        if (key && key[0] != '\0') {
            current = find_child_by_name(current, key);
            if (!current) {
                free(copy);
                set_err(err, err_sz, "path not found");
                return EDIT_ERR_PATH_NOT_FOUND;
            }
        }

        if (has_index) {
            if (current->type == TAG_List) {
                if (index < 0 || index >= current->value.list.count) {
                    free(copy);
                    set_err(err, err_sz, "index out of bounds");
                    return EDIT_ERR_INDEX_BOUNDS;
                }

                if (is_last) {
                    out->kind = PATH_TARGET_LIST_ELEMENT;
                    out->tag = current;
                    out->index = index;
                    free(copy);
                    return EDIT_OK;
                }

                if (!current->value.list.items[index]) {
                    free(copy);
                    set_err(err, err_sz, "path not found");
                    return EDIT_ERR_PATH_NOT_FOUND;
                }
                current = current->value.list.items[index];
                continue;
            }

            if (is_last && current->type == TAG_Byte_Array) {
                if (index < 0 || index >= current->value.byte_array.length) {
                    free(copy);
                    set_err(err, err_sz, "index out of bounds");
                    return EDIT_ERR_INDEX_BOUNDS;
                }
                out->kind = PATH_TARGET_BYTE_ARRAY_ELEMENT;
                out->tag = current;
                out->index = index;
                free(copy);
                return EDIT_OK;
            }

            if (is_last && current->type == TAG_Int_Array) {
                if (index < 0 || index >= current->value.int_array.length) {
                    free(copy);
                    set_err(err, err_sz, "index out of bounds");
                    return EDIT_ERR_INDEX_BOUNDS;
                }
                out->kind = PATH_TARGET_INT_ARRAY_ELEMENT;
                out->tag = current;
                out->index = index;
                free(copy);
                return EDIT_OK;
            }

            if (is_last && current->type == TAG_Long_Array) {
                if (index < 0 || index >= current->value.long_array.length) {
                    free(copy);
                    set_err(err, err_sz, "index out of bounds");
                    return EDIT_ERR_INDEX_BOUNDS;
                }
                out->kind = PATH_TARGET_LONG_ARRAY_ELEMENT;
                out->tag = current;
                out->index = index;
                free(copy);
                return EDIT_OK;
            }

            free(copy);
            set_err(err, err_sz, "type mismatch: indexing is only supported for list/array tags");
            return EDIT_ERR_TYPE_MISMATCH;
        }

        if (is_last) {
            out->kind = PATH_TARGET_TAG;
            out->tag = current;
            out->index = -1;
            free(copy);
            return EDIT_OK;
        }
    }

    free(copy);
    set_err(err, err_sz, "invalid path syntax");
    return EDIT_ERR_PATH_SYNTAX;
}
