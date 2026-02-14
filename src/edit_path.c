#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "edit_path.h"

typedef enum {
    INDEX_NONE = 0,
    INDEX_EXACT,
    INDEX_WILDCARD
} IndexMode;

typedef struct {
    char* key;
    IndexMode index_mode;
    int index;
} ParsedSegment;

typedef struct {
    NBTTag* tag;
    NBTTag* parent;
    int parent_index;
} Cursor;

static void set_err(char* err, size_t err_sz, const char* msg) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", msg);
    }
}

static void free_parsed_segments(ParsedSegment* segs, int count) {
    if (!segs) return;
    for (int i = 0; i < count; i++) {
        free(segs[i].key);
    }
    free(segs);
}

static char* dup_slice(const char* s, size_t len) {
    char* out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

static EditStatus decode_quoted_key(const char* src, size_t len, char** out, char* err, size_t err_sz) {
    char* key;
    size_t oi = 0;
    size_t i = 0;

    key = malloc(len + 1);
    if (!key) {
        set_err(err, err_sz, "out of memory");
        return EDIT_ERR_MEMORY;
    }

    while (i < len) {
        char c = src[i++];
        if (c != '\\') {
            key[oi++] = c;
            continue;
        }

        if (i >= len) {
            free(key);
            set_err(err, err_sz, "invalid path syntax: invalid quoted key escape");
            return EDIT_ERR_PATH_SYNTAX;
        }

        c = src[i++];
        switch (c) {
            case '"':
            case '\\':
            case '/':
                key[oi++] = c;
                break;
            case 'n':
                key[oi++] = '\n';
                break;
            case 'r':
                key[oi++] = '\r';
                break;
            case 't':
                key[oi++] = '\t';
                break;
            default:
                free(key);
                set_err(err, err_sz, "invalid path syntax: unsupported quoted key escape");
                return EDIT_ERR_PATH_SYNTAX;
        }
    }

    key[oi] = '\0';
    *out = key;
    return EDIT_OK;
}

static EditStatus parse_segment_text(const char* text, ParsedSegment* out, char* err, size_t err_sz) {
    size_t len;
    size_t pos = 0;

    if (!text || !out) {
        set_err(err, err_sz, "invalid path syntax");
        return EDIT_ERR_PATH_SYNTAX;
    }

    len = strlen(text);
    if (len == 0) {
        set_err(err, err_sz, "invalid path syntax: empty segment");
        return EDIT_ERR_PATH_SYNTAX;
    }

    out->key = NULL;
    out->index_mode = INDEX_NONE;
    out->index = -1;

    if (text[pos] == '"') {
        size_t start;
        size_t qlen;
        int closed = 0;

        pos++;
        start = pos;
        while (pos < len) {
            if (text[pos] == '\\') {
                pos += 2;
                continue;
            }
            if (text[pos] == '"') {
                closed = 1;
                break;
            }
            pos++;
        }

        if (!closed || pos >= len) {
            set_err(err, err_sz, "invalid path syntax: unterminated quoted key");
            return EDIT_ERR_PATH_SYNTAX;
        }

        qlen = pos - start;
        {
            EditStatus st = decode_quoted_key(text + start, qlen, &out->key, err, err_sz);
            if (st != EDIT_OK) return st;
        }
        pos++;
    } else {
        size_t key_start = 0;
        while (pos < len && text[pos] != '[') pos++;
        out->key = dup_slice(text + key_start, pos - key_start);
        if (!out->key) {
            set_err(err, err_sz, "out of memory");
            return EDIT_ERR_MEMORY;
        }
    }

    if (pos == len) {
        if (out->key[0] == '\0') {
            free(out->key);
            out->key = NULL;
            set_err(err, err_sz, "invalid path syntax: empty segment");
            return EDIT_ERR_PATH_SYNTAX;
        }
        return EDIT_OK;
    }

    if (text[pos] != '[' || text[len - 1] != ']') {
        free(out->key);
        out->key = NULL;
        set_err(err, err_sz, "invalid path syntax: malformed brackets");
        return EDIT_ERR_PATH_SYNTAX;
    }

    {
        const char* inner_start = text + pos + 1;
        size_t inner_len = len - pos - 2;

        if (inner_len == 0) {
            free(out->key);
            out->key = NULL;
            set_err(err, err_sz, "invalid path syntax: empty index");
            return EDIT_ERR_PATH_SYNTAX;
        }

        if (inner_len == 1 && inner_start[0] == '*') {
            out->index_mode = INDEX_WILDCARD;
            return EDIT_OK;
        }

        for (size_t i = 0; i < inner_len; i++) {
            if (!isdigit((unsigned char)inner_start[i])) {
                free(out->key);
                out->key = NULL;
                set_err(err, err_sz, "invalid path syntax: non-numeric index");
                return EDIT_ERR_PATH_SYNTAX;
            }
        }

        {
            long value;
            char* idx_text = dup_slice(inner_start, inner_len);
            if (!idx_text) {
                free(out->key);
                out->key = NULL;
                set_err(err, err_sz, "out of memory");
                return EDIT_ERR_MEMORY;
            }
            errno = 0;
            value = strtol(idx_text, NULL, 10);
            free(idx_text);
            if (errno || value < 0 || value > INT_MAX) {
                free(out->key);
                out->key = NULL;
                set_err(err, err_sz, "invalid path syntax: index out of range");
                return EDIT_ERR_PATH_SYNTAX;
            }
            out->index_mode = INDEX_EXACT;
            out->index = (int)value;
        }
    }

    return EDIT_OK;
}

static EditStatus parse_path_segments(const char* path, ParsedSegment** out_segs, int* out_count, char* err, size_t err_sz) {
    ParsedSegment* segs;
    int count = 0;
    size_t len;
    char* token;
    size_t tlen = 0;
    int in_quotes = 0;
    int escape = 0;

    if (!path || !out_segs || !out_count) {
        set_err(err, err_sz, "invalid path syntax");
        return EDIT_ERR_PATH_SYNTAX;
    }

    segs = calloc(256, sizeof(ParsedSegment));
    if (!segs) {
        set_err(err, err_sz, "out of memory");
        return EDIT_ERR_MEMORY;
    }

    len = strlen(path);
    token = malloc(len + 1);
    if (!token) {
        free(segs);
        set_err(err, err_sz, "out of memory");
        return EDIT_ERR_MEMORY;
    }

    for (size_t i = 0; i < len; i++) {
        char c = path[i];
        if (c == '/' && !in_quotes) {
            if (tlen > 0) {
                EditStatus st;
                token[tlen] = '\0';
                if (count >= 256) {
                    free(token);
                    free_parsed_segments(segs, count);
                    set_err(err, err_sz, "invalid path syntax: too many segments");
                    return EDIT_ERR_PATH_SYNTAX;
                }
                st = parse_segment_text(token, &segs[count], err, err_sz);
                if (st != EDIT_OK) {
                    free(token);
                    free_parsed_segments(segs, count);
                    return st;
                }
                count++;
                tlen = 0;
            }
            continue;
        }

        token[tlen++] = c;

        if (in_quotes) {
            if (escape) {
                escape = 0;
            } else if (c == '\\') {
                escape = 1;
            } else if (c == '"') {
                in_quotes = 0;
            }
        } else if (c == '"') {
            in_quotes = 1;
        }
    }

    if (in_quotes) {
        free(token);
        free_parsed_segments(segs, count);
        set_err(err, err_sz, "invalid path syntax: unterminated quoted key");
        return EDIT_ERR_PATH_SYNTAX;
    }

    if (tlen > 0) {
        EditStatus st;
        token[tlen] = '\0';
        if (count >= 256) {
            free(token);
            free_parsed_segments(segs, count);
            set_err(err, err_sz, "invalid path syntax: too many segments");
            return EDIT_ERR_PATH_SYNTAX;
        }
        st = parse_segment_text(token, &segs[count], err, err_sz);
        if (st != EDIT_OK) {
            free(token);
            free_parsed_segments(segs, count);
            return st;
        }
        count++;
    }

    free(token);
    *out_segs = segs;
    *out_count = count;
    return EDIT_OK;
}

static int is_root_name_segment(NBTTag* root, const ParsedSegment* seg) {
    if (!root || !seg || seg->index_mode != INDEX_NONE) return 0;
    if (!root->name || root->name[0] == '\0' || !seg->key) return 0;
    return strcmp(seg->key, root->name) == 0;
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

static int push_cursor(Cursor** arr, size_t* count, size_t* cap, Cursor value) {
    if (*count >= *cap) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap * 2);
        Cursor* next = realloc(*arr, new_cap * sizeof(Cursor));
        if (!next) return 0;
        *arr = next;
        *cap = new_cap;
    }
    (*arr)[(*count)++] = value;
    return 1;
}

static int push_target(PathTarget** arr, size_t* count, size_t* cap, PathTarget value) {
    if (*count >= *cap) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap * 2);
        PathTarget* next = realloc(*arr, new_cap * sizeof(PathTarget));
        if (!next) return 0;
        *arr = next;
        *cap = new_cap;
    }
    (*arr)[(*count)++] = value;
    return 1;
}

EditStatus resolve_edit_paths(NBTTag* root, const char* path, PathTarget** out_targets, size_t* out_count, char* err, size_t err_sz) {
    ParsedSegment* segs = NULL;
    int seg_count = 0;
    int start = 0;
    EditStatus st;
    Cursor* cursors = NULL;
    size_t cur_count = 0;
    size_t cur_cap = 0;
    PathTarget* targets = NULL;
    size_t target_count = 0;
    size_t target_cap = 0;
    int saw_type_mismatch = 0;
    int saw_index_bounds = 0;

    if (!root || !path || !out_targets || !out_count) {
        set_err(err, err_sz, "invalid argument");
        return EDIT_ERR_PATH_SYNTAX;
    }

    *out_targets = NULL;
    *out_count = 0;

    st = parse_path_segments(path, &segs, &seg_count, err, err_sz);
    if (st != EDIT_OK) return st;

    if (!push_cursor(&cursors, &cur_count, &cur_cap, (Cursor){ .tag = root, .parent = NULL, .parent_index = -1 })) {
        free_parsed_segments(segs, seg_count);
        set_err(err, err_sz, "out of memory");
        return EDIT_ERR_MEMORY;
    }

    if (seg_count == 0) {
        if (!push_target(&targets, &target_count, &target_cap, (PathTarget){ .kind = PATH_TARGET_TAG, .tag = root, .parent = NULL, .index = -1 })) {
            free(cursors);
            free_parsed_segments(segs, seg_count);
            set_err(err, err_sz, "out of memory");
            return EDIT_ERR_MEMORY;
        }
        free(cursors);
        free_parsed_segments(segs, seg_count);
        *out_targets = targets;
        *out_count = target_count;
        return EDIT_OK;
    }

    if (is_root_name_segment(root, &segs[0])) {
        start = 1;
    }

    if (start >= seg_count) {
        if (!push_target(&targets, &target_count, &target_cap, (PathTarget){ .kind = PATH_TARGET_TAG, .tag = root, .parent = NULL, .index = -1 })) {
            free(cursors);
            free_parsed_segments(segs, seg_count);
            set_err(err, err_sz, "out of memory");
            return EDIT_ERR_MEMORY;
        }
        free(cursors);
        free_parsed_segments(segs, seg_count);
        *out_targets = targets;
        *out_count = target_count;
        return EDIT_OK;
    }

    for (int si = start; si < seg_count; si++) {
        int is_last = (si == seg_count - 1);
        ParsedSegment* seg = &segs[si];
        Cursor* next = NULL;
        size_t next_count = 0;
        size_t next_cap = 0;

        for (size_t ci = 0; ci < cur_count; ci++) {
            Cursor cur = cursors[ci];
            NBTTag* node = cur.tag;
            NBTTag* node_parent = cur.parent;
            int node_parent_index = cur.parent_index;

            if (seg->key && seg->key[0] != '\0') {
                int child_index = -1;
                NBTTag* child;

                if (!node || node->type != TAG_Compound) {
                    saw_type_mismatch = 1;
                    continue;
                }

                child = find_child_by_name(node, seg->key, &child_index);
                if (!child) continue;
                node_parent = node;
                node_parent_index = child_index;
                node = child;
            }

            if (seg->index_mode != INDEX_NONE) {
                if (node->type == TAG_List) {
                    if (seg->index_mode == INDEX_EXACT) {
                        int idx = seg->index;
                        if (idx < 0 || idx >= node->value.list.count) {
                            saw_index_bounds = 1;
                            continue;
                        }

                        if (is_last) {
                            PathTarget t = { .kind = PATH_TARGET_LIST_ELEMENT, .tag = node, .parent = NULL, .index = idx };
                            if (!push_target(&targets, &target_count, &target_cap, t)) {
                                free(next);
                                free(cursors);
                                free_parsed_segments(segs, seg_count);
                                free(targets);
                                set_err(err, err_sz, "out of memory");
                                return EDIT_ERR_MEMORY;
                            }
                        } else if (node->value.list.items[idx]) {
                            Cursor nx = { .tag = node->value.list.items[idx], .parent = node, .parent_index = idx };
                            if (!push_cursor(&next, &next_count, &next_cap, nx)) {
                                free(next);
                                free(cursors);
                                free_parsed_segments(segs, seg_count);
                                free(targets);
                                set_err(err, err_sz, "out of memory");
                                return EDIT_ERR_MEMORY;
                            }
                        }
                    } else {
                        for (int idx = 0; idx < node->value.list.count; idx++) {
                            if (is_last) {
                                PathTarget t = { .kind = PATH_TARGET_LIST_ELEMENT, .tag = node, .parent = NULL, .index = idx };
                                if (!push_target(&targets, &target_count, &target_cap, t)) {
                                    free(next);
                                    free(cursors);
                                    free_parsed_segments(segs, seg_count);
                                    free(targets);
                                    set_err(err, err_sz, "out of memory");
                                    return EDIT_ERR_MEMORY;
                                }
                            } else if (node->value.list.items[idx]) {
                                Cursor nx = { .tag = node->value.list.items[idx], .parent = node, .parent_index = idx };
                                if (!push_cursor(&next, &next_count, &next_cap, nx)) {
                                    free(next);
                                    free(cursors);
                                    free_parsed_segments(segs, seg_count);
                                    free(targets);
                                    set_err(err, err_sz, "out of memory");
                                    return EDIT_ERR_MEMORY;
                                }
                            }
                        }
                    }
                    continue;
                }

                if (!is_last) {
                    saw_type_mismatch = 1;
                    continue;
                }

                if (node->type == TAG_Byte_Array || node->type == TAG_Int_Array || node->type == TAG_Long_Array) {
                    int len = 0;
                    PathTargetKind kind = PATH_TARGET_BYTE_ARRAY_ELEMENT;
                    if (node->type == TAG_Byte_Array) {
                        len = node->value.byte_array.length;
                        kind = PATH_TARGET_BYTE_ARRAY_ELEMENT;
                    } else if (node->type == TAG_Int_Array) {
                        len = node->value.int_array.length;
                        kind = PATH_TARGET_INT_ARRAY_ELEMENT;
                    } else {
                        len = node->value.long_array.length;
                        kind = PATH_TARGET_LONG_ARRAY_ELEMENT;
                    }

                    if (seg->index_mode == INDEX_EXACT) {
                        int idx = seg->index;
                        if (idx < 0 || idx >= len) {
                            saw_index_bounds = 1;
                            continue;
                        }
                        {
                            PathTarget t = { .kind = kind, .tag = node, .parent = NULL, .index = idx };
                            if (!push_target(&targets, &target_count, &target_cap, t)) {
                                free(next);
                                free(cursors);
                                free_parsed_segments(segs, seg_count);
                                free(targets);
                                set_err(err, err_sz, "out of memory");
                                return EDIT_ERR_MEMORY;
                            }
                        }
                    } else {
                        for (int idx = 0; idx < len; idx++) {
                            PathTarget t = { .kind = kind, .tag = node, .parent = NULL, .index = idx };
                            if (!push_target(&targets, &target_count, &target_cap, t)) {
                                free(next);
                                free(cursors);
                                free_parsed_segments(segs, seg_count);
                                free(targets);
                                set_err(err, err_sz, "out of memory");
                                return EDIT_ERR_MEMORY;
                            }
                        }
                    }
                    continue;
                }

                saw_type_mismatch = 1;
                continue;
            }

            if (is_last) {
                PathTarget t = { .kind = PATH_TARGET_TAG, .tag = node, .parent = node_parent, .index = node_parent_index };
                if (!push_target(&targets, &target_count, &target_cap, t)) {
                    free(next);
                    free(cursors);
                    free_parsed_segments(segs, seg_count);
                    free(targets);
                    set_err(err, err_sz, "out of memory");
                    return EDIT_ERR_MEMORY;
                }
            } else {
                Cursor nx = { .tag = node, .parent = node_parent, .parent_index = node_parent_index };
                if (!push_cursor(&next, &next_count, &next_cap, nx)) {
                    free(next);
                    free(cursors);
                    free_parsed_segments(segs, seg_count);
                    free(targets);
                    set_err(err, err_sz, "out of memory");
                    return EDIT_ERR_MEMORY;
                }
            }
        }

        free(cursors);
        cursors = next;
        cur_count = next_count;
        cur_cap = next_cap;
    }

    free(cursors);
    free_parsed_segments(segs, seg_count);

    if (target_count == 0) {
        free(targets);
        if (saw_index_bounds) {
            set_err(err, err_sz, "index out of bounds");
            return EDIT_ERR_INDEX_BOUNDS;
        }
        if (saw_type_mismatch) {
            set_err(err, err_sz, "type mismatch: indexing is only supported for list/array tags");
            return EDIT_ERR_TYPE_MISMATCH;
        }
        set_err(err, err_sz, "path not found");
        return EDIT_ERR_PATH_NOT_FOUND;
    }

    *out_targets = targets;
    *out_count = target_count;
    return EDIT_OK;
}

void free_edit_paths(PathTarget* targets) {
    free(targets);
}

EditStatus resolve_edit_path(NBTTag* root, const char* path, PathTarget* out, char* err, size_t err_sz) {
    PathTarget* targets = NULL;
    size_t count = 0;
    EditStatus st;

    if (!out) {
        set_err(err, err_sz, "invalid argument");
        return EDIT_ERR_PATH_SYNTAX;
    }

    st = resolve_edit_paths(root, path, &targets, &count, err, err_sz);
    if (st != EDIT_OK) return st;

    if (count != 1) {
        free_edit_paths(targets);
        set_err(err, err_sz, "unsupported operation: path resolves to multiple targets");
        return EDIT_ERR_UNSUPPORTED;
    }

    *out = targets[0];
    free_edit_paths(targets);
    return EDIT_OK;
}

EditStatus resolve_set_parent_and_key(NBTTag* root, const char* path, NBTTag** out_parent, char** out_key, char* err, size_t err_sz) {
    ParsedSegment* segs = NULL;
    int seg_count = 0;
    int start = 0;
    EditStatus st;
    NBTTag* current;

    if (!root || !path || !out_parent || !out_key) {
        set_err(err, err_sz, "invalid path argument");
        return EDIT_ERR_PATH_SYNTAX;
    }

    *out_parent = NULL;
    *out_key = NULL;

    st = parse_path_segments(path, &segs, &seg_count, err, err_sz);
    if (st != EDIT_OK) return st;

    if (seg_count == 0) {
        free_parsed_segments(segs, seg_count);
        set_err(err, err_sz, "invalid path syntax");
        return EDIT_ERR_PATH_SYNTAX;
    }

    if (is_root_name_segment(root, &segs[0])) start = 1;
    if (start >= seg_count) {
        free_parsed_segments(segs, seg_count);
        set_err(err, err_sz, "unsupported operation: cannot target root path");
        return EDIT_ERR_UNSUPPORTED;
    }

    if (segs[seg_count - 1].index_mode != INDEX_NONE) {
        free_parsed_segments(segs, seg_count);
        set_err(err, err_sz, "unsupported operation: set-create path must end with a key");
        return EDIT_ERR_UNSUPPORTED;
    }

    current = root;
    for (int i = start; i < seg_count - 1; i++) {
        ParsedSegment* seg = &segs[i];
        NBTTag* node = current;

        if (seg->index_mode == INDEX_WILDCARD) {
            free_parsed_segments(segs, seg_count);
            set_err(err, err_sz, "unsupported operation: wildcard is not allowed in set-create path");
            return EDIT_ERR_UNSUPPORTED;
        }

        if (seg->key && seg->key[0] != '\0') {
            if (!node || node->type != TAG_Compound) {
                free_parsed_segments(segs, seg_count);
                set_err(err, err_sz, "type mismatch: parent path is not a compound");
                return EDIT_ERR_TYPE_MISMATCH;
            }
            node = find_child_by_name(node, seg->key, NULL);
            if (!node) {
                free_parsed_segments(segs, seg_count);
                set_err(err, err_sz, "path not found");
                return EDIT_ERR_PATH_NOT_FOUND;
            }
        }

        if (seg->index_mode == INDEX_EXACT) {
            int idx = seg->index;
            if (node->type != TAG_List) {
                free_parsed_segments(segs, seg_count);
                set_err(err, err_sz, "type mismatch: indexing is only supported for list/array tags");
                return EDIT_ERR_TYPE_MISMATCH;
            }
            if (idx < 0 || idx >= node->value.list.count) {
                free_parsed_segments(segs, seg_count);
                set_err(err, err_sz, "index out of bounds");
                return EDIT_ERR_INDEX_BOUNDS;
            }
            node = node->value.list.items[idx];
            if (!node) {
                free_parsed_segments(segs, seg_count);
                set_err(err, err_sz, "path not found");
                return EDIT_ERR_PATH_NOT_FOUND;
            }
        }

        current = node;
    }

    if (!current || current->type != TAG_Compound) {
        free_parsed_segments(segs, seg_count);
        set_err(err, err_sz, "type mismatch: parent path is not a compound");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    *out_parent = current;
    *out_key = strdup(segs[seg_count - 1].key ? segs[seg_count - 1].key : "");
    if (!*out_key) {
        free_parsed_segments(segs, seg_count);
        set_err(err, err_sz, "out of memory");
        return EDIT_ERR_MEMORY;
    }

    free_parsed_segments(segs, seg_count);
    return EDIT_OK;
}
