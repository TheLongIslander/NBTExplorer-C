#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "edit_value.h"
#include "jsmn.h"
#include "nbt_builder.h"

typedef struct {
    const char* text;
    jsmntok_t* tokens;
    int count;
} JsonDoc;

static void set_err(char* err, size_t err_sz, const char* msg) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", msg);
    }
}

static int token_span(const JsonDoc* doc, int index) {
    int next = index + 1;
    int end = doc->tokens[index].end;

    while (next < doc->count && doc->tokens[next].start < end) {
        next = token_span(doc, next);
    }

    return next;
}

static EditStatus parse_json_doc(const char* text, JsonDoc* out, char* err, size_t err_sz) {
    int cap = 128;
    jsmntok_t* tokens = NULL;

    if (!text || !out) {
        set_err(err, err_sz, "invalid json input");
        return EDIT_ERR_INVALID_JSON;
    }

    while (1) {
        int rc;
        jsmn_parser parser;

        tokens = malloc((size_t)cap * sizeof(jsmntok_t));
        if (!tokens) {
            set_err(err, err_sz, "out of memory");
            return EDIT_ERR_MEMORY;
        }

        jsmn_init(&parser);
        rc = jsmn_parse(&parser, text, strlen(text), tokens, (unsigned int)cap);

        if (rc == JSMN_ERROR_NOMEM) {
            free(tokens);
            tokens = NULL;
            cap *= 2;
            if (cap > 1 << 20) {
                set_err(err, err_sz, "json too large");
                return EDIT_ERR_INVALID_JSON;
            }
            continue;
        }

        if (rc < 0) {
            free(tokens);
            set_err(err, err_sz, "invalid JSON");
            return EDIT_ERR_INVALID_JSON;
        }

        if (rc == 0) {
            free(tokens);
            set_err(err, err_sz, "invalid JSON");
            return EDIT_ERR_INVALID_JSON;
        }

        out->text = text;
        out->tokens = tokens;
        out->count = rc;

        if (token_span(out, 0) != out->count) {
            free(tokens);
            set_err(err, err_sz, "invalid JSON: trailing tokens");
            return EDIT_ERR_INVALID_JSON;
        }

        return EDIT_OK;
    }
}

static void free_json_doc(JsonDoc* doc) {
    if (!doc) return;
    free(doc->tokens);
    doc->tokens = NULL;
    doc->count = 0;
    doc->text = NULL;
}

static int json_key_equals(const JsonDoc* doc, int tok_index, const char* name) {
    size_t len;
    jsmntok_t tok;

    if (!doc || !name || tok_index < 0 || tok_index >= doc->count) return 0;

    tok = doc->tokens[tok_index];
    if (tok.type != JSMN_STRING) return 0;

    len = (size_t)(tok.end - tok.start);
    if (strlen(name) != len) return 0;
    return strncmp(doc->text + tok.start, name, len) == 0;
}

static char* decode_json_string(const char* src, size_t len) {
    char* out;
    size_t oi = 0;

    out = malloc(len + 1);
    if (!out) return NULL;

    for (size_t i = 0; i < len; i++) {
        char c = src[i];

        if (c != '\\') {
            out[oi++] = c;
            continue;
        }

        if (i + 1 >= len) {
            free(out);
            return NULL;
        }

        i++;
        c = src[i];
        switch (c) {
            case '"':
            case '\\':
            case '/':
                out[oi++] = c;
                break;
            case 'b':
                out[oi++] = '\b';
                break;
            case 'f':
                out[oi++] = '\f';
                break;
            case 'n':
                out[oi++] = '\n';
                break;
            case 'r':
                out[oi++] = '\r';
                break;
            case 't':
                out[oi++] = '\t';
                break;
            case 'u': {
                unsigned int code = 0;
                for (int d = 0; d < 4; d++) {
                    if (i + 1 >= len) {
                        free(out);
                        return NULL;
                    }
                    i++;
                    c = src[i];
                    if (c >= '0' && c <= '9') code = (code << 4) | (unsigned int)(c - '0');
                    else if (c >= 'a' && c <= 'f') code = (code << 4) | (unsigned int)(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') code = (code << 4) | (unsigned int)(c - 'A' + 10);
                    else {
                        free(out);
                        return NULL;
                    }
                }
                out[oi++] = (code <= 0x7F) ? (char)code : '?';
                break;
            }
            default:
                free(out);
                return NULL;
        }
    }

    out[oi] = '\0';
    return out;
}

static EditStatus token_to_decoded_string(const JsonDoc* doc, int tok_index, char** out, char* err, size_t err_sz) {
    jsmntok_t tok;

    if (!doc || !out || tok_index < 0 || tok_index >= doc->count) {
        set_err(err, err_sz, "invalid JSON string token");
        return EDIT_ERR_INVALID_JSON;
    }

    tok = doc->tokens[tok_index];
    if (tok.type != JSMN_STRING) {
        set_err(err, err_sz, "type mismatch: expected JSON string");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    *out = decode_json_string(doc->text + tok.start, (size_t)(tok.end - tok.start));
    if (!*out) {
        set_err(err, err_sz, "invalid JSON string escaping");
        return EDIT_ERR_INVALID_JSON;
    }

    return EDIT_OK;
}

static char* token_to_text(const JsonDoc* doc, int tok_index) {
    jsmntok_t tok;
    size_t len;
    char* out;

    if (!doc || tok_index < 0 || tok_index >= doc->count) return NULL;

    tok = doc->tokens[tok_index];
    len = (size_t)(tok.end - tok.start);
    out = malloc(len + 1);
    if (!out) return NULL;

    memcpy(out, doc->text + tok.start, len);
    out[len] = '\0';
    return out;
}

static int is_primitive_bool_or_null(const char* s) {
    return strcmp(s, "true") == 0 || strcmp(s, "false") == 0 || strcmp(s, "null") == 0;
}

static EditStatus token_to_int64(const JsonDoc* doc, int tok_index, int64_t min_val, int64_t max_val, int64_t* out, char* err, size_t err_sz) {
    char* text;
    char* end;
    long long val;

    if (!doc || !out || tok_index < 0 || tok_index >= doc->count) {
        set_err(err, err_sz, "invalid JSON number token");
        return EDIT_ERR_INVALID_JSON;
    }

    if (doc->tokens[tok_index].type != JSMN_PRIMITIVE) {
        set_err(err, err_sz, "type mismatch: expected JSON number");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    text = token_to_text(doc, tok_index);
    if (!text) {
        set_err(err, err_sz, "out of memory");
        return EDIT_ERR_MEMORY;
    }

    if (is_primitive_bool_or_null(text) || strchr(text, '.') || strchr(text, 'e') || strchr(text, 'E')) {
        free(text);
        set_err(err, err_sz, "type mismatch: expected integer number");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    errno = 0;
    val = strtoll(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0') {
        free(text);
        set_err(err, err_sz, "numeric overflow");
        return EDIT_ERR_NUMERIC_RANGE;
    }

    if (val < min_val || val > max_val) {
        free(text);
        set_err(err, err_sz, "numeric overflow");
        return EDIT_ERR_NUMERIC_RANGE;
    }

    *out = (int64_t)val;
    free(text);
    return EDIT_OK;
}

static EditStatus token_to_double(const JsonDoc* doc, int tok_index, double* out, char* err, size_t err_sz) {
    char* text;
    char* end;
    double val;

    if (!doc || !out || tok_index < 0 || tok_index >= doc->count) {
        set_err(err, err_sz, "invalid JSON number token");
        return EDIT_ERR_INVALID_JSON;
    }

    if (doc->tokens[tok_index].type != JSMN_PRIMITIVE) {
        set_err(err, err_sz, "type mismatch: expected JSON number");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    text = token_to_text(doc, tok_index);
    if (!text) {
        set_err(err, err_sz, "out of memory");
        return EDIT_ERR_MEMORY;
    }

    if (is_primitive_bool_or_null(text)) {
        free(text);
        set_err(err, err_sz, "type mismatch: expected JSON number");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    errno = 0;
    val = strtod(text, &end);
    if (errno == ERANGE || end == text || *end != '\0' || !isfinite(val)) {
        free(text);
        set_err(err, err_sz, "numeric overflow");
        return EDIT_ERR_NUMERIC_RANGE;
    }

    *out = val;
    free(text);
    return EDIT_OK;
}

static EditStatus parse_legacy_int64(const char* text, int64_t min_val, int64_t max_val, int64_t* out, char* err, size_t err_sz) {
    const char* start = text;
    const char* endptr;
    char* num;
    char* parse_end;
    long long val;
    size_t len;

    if (!text || !out) {
        set_err(err, err_sz, "invalid numeric value");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    while (*start && isspace((unsigned char)*start)) start++;
    endptr = start + strlen(start);
    while (endptr > start && isspace((unsigned char)*(endptr - 1))) endptr--;

    len = (size_t)(endptr - start);
    if (len == 0) {
        set_err(err, err_sz, "invalid numeric value");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    num = malloc(len + 1);
    if (!num) {
        set_err(err, err_sz, "out of memory");
        return EDIT_ERR_MEMORY;
    }
    memcpy(num, start, len);
    num[len] = '\0';

    if (strchr(num, '.') || strchr(num, 'e') || strchr(num, 'E')) {
        free(num);
        set_err(err, err_sz, "type mismatch: expected integer number");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    errno = 0;
    val = strtoll(num, &parse_end, 10);
    if (errno == ERANGE || parse_end == num || *parse_end != '\0') {
        free(num);
        set_err(err, err_sz, "numeric overflow");
        return EDIT_ERR_NUMERIC_RANGE;
    }

    if (val < min_val || val > max_val) {
        free(num);
        set_err(err, err_sz, "numeric overflow");
        return EDIT_ERR_NUMERIC_RANGE;
    }

    *out = (int64_t)val;
    free(num);
    return EDIT_OK;
}

static EditStatus parse_legacy_double(const char* text, double* out, char* err, size_t err_sz) {
    const char* start = text;
    const char* endptr;
    char* num;
    char* parse_end;
    double val;
    size_t len;

    if (!text || !out) {
        set_err(err, err_sz, "invalid numeric value");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    while (*start && isspace((unsigned char)*start)) start++;
    endptr = start + strlen(start);
    while (endptr > start && isspace((unsigned char)*(endptr - 1))) endptr--;

    len = (size_t)(endptr - start);
    if (len == 0) {
        set_err(err, err_sz, "invalid numeric value");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    num = malloc(len + 1);
    if (!num) {
        set_err(err, err_sz, "out of memory");
        return EDIT_ERR_MEMORY;
    }
    memcpy(num, start, len);
    num[len] = '\0';

    errno = 0;
    val = strtod(num, &parse_end);
    if (errno == ERANGE || parse_end == num || *parse_end != '\0' || !isfinite(val)) {
        free(num);
        set_err(err, err_sz, "numeric overflow");
        return EDIT_ERR_NUMERIC_RANGE;
    }

    *out = val;
    free(num);
    return EDIT_OK;
}

static void free_list_items(NBTTag** items, int count) {
    if (!items) return;
    for (int i = 0; i < count; i++) {
        free_nbt_tree(items[i]);
    }
    free(items);
}

static NBTTag* create_list_element(TagType type) {
    NBTTag* tag = malloc(sizeof(NBTTag));
    if (!tag) return NULL;

    memset(tag, 0, sizeof(NBTTag));
    tag->type = type;
    tag->name = strdup("");
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

static EditStatus parse_token_into_tag(NBTTag* target, const JsonDoc* doc, int tok_index, char* err, size_t err_sz);

static EditStatus apply_object_patch_token(NBTTag* compound, const JsonDoc* doc, int obj_tok, char* err, size_t err_sz) {
    int child;
    jsmntok_t root;

    if (!compound || compound->type != TAG_Compound) {
        set_err(err, err_sz, "type mismatch: target is not a compound");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    root = doc->tokens[obj_tok];
    if (root.type != JSMN_OBJECT) {
        set_err(err, err_sz, "type mismatch: expected JSON object");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    child = obj_tok + 1;
    while (child < doc->count && doc->tokens[child].start < root.end) {
        int key_tok = child;
        int value_tok;
        NBTTag* matched = NULL;

        if (doc->tokens[key_tok].type != JSMN_STRING) {
            set_err(err, err_sz, "invalid JSON object key");
            return EDIT_ERR_INVALID_JSON;
        }

        child = token_span(doc, key_tok);
        if (child >= doc->count || doc->tokens[child].start >= root.end) {
            set_err(err, err_sz, "invalid JSON object: missing value");
            return EDIT_ERR_INVALID_JSON;
        }

        value_tok = child;
        child = token_span(doc, value_tok);

        for (int i = 0; i < compound->value.compound.count; i++) {
            NBTTag* c = compound->value.compound.items[i];
            if (!c || !c->name) continue;
            if (json_key_equals(doc, key_tok, c->name)) {
                matched = c;
                break;
            }
        }

        if (!matched) {
            set_err(err, err_sz, "unknown compound key in patch");
            return EDIT_ERR_TYPE_MISMATCH;
        }

        EditStatus st = parse_token_into_tag(matched, doc, value_tok, err, err_sz);
        if (st != EDIT_OK) return st;
    }

    return EDIT_OK;
}

static int count_array_elements(const JsonDoc* doc, int arr_tok) {
    int count = 0;
    int child = arr_tok + 1;
    int arr_end = doc->tokens[arr_tok].end;

    while (child < doc->count && doc->tokens[child].start < arr_end) {
        count++;
        child = token_span(doc, child);
    }

    return count;
}

static EditStatus parse_token_into_tag(NBTTag* target, const JsonDoc* doc, int tok_index, char* err, size_t err_sz) {
    int64_t i64;
    double d;

    if (!target || !doc || tok_index < 0 || tok_index >= doc->count) {
        set_err(err, err_sz, "invalid edit target");
        return EDIT_ERR_INVALID_JSON;
    }

    switch (target->type) {
        case TAG_Byte: {
            EditStatus st = token_to_int64(doc, tok_index, -128, 127, &i64, err, err_sz);
            if (st != EDIT_OK) return st;
            target->value.byte_val = (int8_t)i64;
            return EDIT_OK;
        }

        case TAG_Short: {
            EditStatus st = token_to_int64(doc, tok_index, -32768, 32767, &i64, err, err_sz);
            if (st != EDIT_OK) return st;
            target->value.short_val = (int16_t)i64;
            return EDIT_OK;
        }

        case TAG_Int: {
            EditStatus st = token_to_int64(doc, tok_index, INT32_MIN, INT32_MAX, &i64, err, err_sz);
            if (st != EDIT_OK) return st;
            target->value.int_val = (int32_t)i64;
            return EDIT_OK;
        }

        case TAG_Long: {
            EditStatus st = token_to_int64(doc, tok_index, INT64_MIN, INT64_MAX, &i64, err, err_sz);
            if (st != EDIT_OK) return st;
            target->value.long_val = (int64_t)i64;
            return EDIT_OK;
        }

        case TAG_Float: {
            EditStatus st = token_to_double(doc, tok_index, &d, err, err_sz);
            if (st != EDIT_OK) return st;
            if (d < -FLT_MAX || d > FLT_MAX) {
                set_err(err, err_sz, "numeric overflow");
                return EDIT_ERR_NUMERIC_RANGE;
            }
            target->value.float_val = (float)d;
            return EDIT_OK;
        }

        case TAG_Double: {
            EditStatus st = token_to_double(doc, tok_index, &d, err, err_sz);
            if (st != EDIT_OK) return st;
            target->value.double_val = d;
            return EDIT_OK;
        }

        case TAG_String: {
            char* str;
            EditStatus st = token_to_decoded_string(doc, tok_index, &str, err, err_sz);
            if (st != EDIT_OK) return st;
            free(target->value.string_val);
            target->value.string_val = str;
            return EDIT_OK;
        }

        case TAG_Byte_Array: {
            int child;
            int count;
            uint8_t* data;

            if (doc->tokens[tok_index].type != JSMN_ARRAY) {
                set_err(err, err_sz, "type mismatch: expected JSON array");
                return EDIT_ERR_TYPE_MISMATCH;
            }

            count = count_array_elements(doc, tok_index);
            data = NULL;
            if (count > 0) {
                data = malloc((size_t)count);
                if (!data) {
                    set_err(err, err_sz, "out of memory");
                    return EDIT_ERR_MEMORY;
                }
            }

            child = tok_index + 1;
            for (int i = 0; i < count; i++) {
                EditStatus st;
                st = token_to_int64(doc, child, -128, 127, &i64, err, err_sz);
                if (st != EDIT_OK) {
                    free(data);
                    return st;
                }
                data[i] = (uint8_t)((int8_t)i64);
                child = token_span(doc, child);
            }

            free(target->value.byte_array.data);
            target->value.byte_array.data = data;
            target->value.byte_array.length = count;
            return EDIT_OK;
        }

        case TAG_Int_Array: {
            int child;
            int count;
            int32_t* data;

            if (doc->tokens[tok_index].type != JSMN_ARRAY) {
                set_err(err, err_sz, "type mismatch: expected JSON array");
                return EDIT_ERR_TYPE_MISMATCH;
            }

            count = count_array_elements(doc, tok_index);
            data = NULL;
            if (count > 0) {
                data = malloc((size_t)count * sizeof(int32_t));
                if (!data) {
                    set_err(err, err_sz, "out of memory");
                    return EDIT_ERR_MEMORY;
                }
            }

            child = tok_index + 1;
            for (int i = 0; i < count; i++) {
                EditStatus st;
                st = token_to_int64(doc, child, INT32_MIN, INT32_MAX, &i64, err, err_sz);
                if (st != EDIT_OK) {
                    free(data);
                    return st;
                }
                data[i] = (int32_t)i64;
                child = token_span(doc, child);
            }

            free(target->value.int_array.data);
            target->value.int_array.data = data;
            target->value.int_array.length = count;
            return EDIT_OK;
        }

        case TAG_Long_Array: {
            int child;
            int count;
            int64_t* data;

            if (doc->tokens[tok_index].type != JSMN_ARRAY) {
                set_err(err, err_sz, "type mismatch: expected JSON array");
                return EDIT_ERR_TYPE_MISMATCH;
            }

            count = count_array_elements(doc, tok_index);
            data = NULL;
            if (count > 0) {
                data = malloc((size_t)count * sizeof(int64_t));
                if (!data) {
                    set_err(err, err_sz, "out of memory");
                    return EDIT_ERR_MEMORY;
                }
            }

            child = tok_index + 1;
            for (int i = 0; i < count; i++) {
                EditStatus st;
                st = token_to_int64(doc, child, INT64_MIN, INT64_MAX, &i64, err, err_sz);
                if (st != EDIT_OK) {
                    free(data);
                    return st;
                }
                data[i] = i64;
                child = token_span(doc, child);
            }

            free(target->value.long_array.data);
            target->value.long_array.data = data;
            target->value.long_array.length = count;
            return EDIT_OK;
        }

        case TAG_List: {
            int count;
            int child;
            NBTTag** new_items;

            if (doc->tokens[tok_index].type != JSMN_ARRAY) {
                set_err(err, err_sz, "type mismatch: expected JSON array");
                return EDIT_ERR_TYPE_MISMATCH;
            }

            if (target->value.list.element_type == TAG_End) {
                set_err(err, err_sz, "unsupported operation: cannot infer element type for empty TAG_End list");
                return EDIT_ERR_UNSUPPORTED;
            }

            if (target->value.list.element_type == TAG_Compound || target->value.list.element_type == TAG_List) {
                set_err(err, err_sz, "unsupported operation: whole replace for compound/list element lists is not supported");
                return EDIT_ERR_UNSUPPORTED;
            }

            count = count_array_elements(doc, tok_index);
            new_items = NULL;
            if (count > 0) {
                new_items = calloc((size_t)count, sizeof(NBTTag*));
                if (!new_items) {
                    set_err(err, err_sz, "out of memory");
                    return EDIT_ERR_MEMORY;
                }
            }

            child = tok_index + 1;
            for (int i = 0; i < count; i++) {
                EditStatus st;
                NBTTag* elem = create_list_element(target->value.list.element_type);
                if (!elem) {
                    free_list_items(new_items, count);
                    set_err(err, err_sz, "out of memory");
                    return EDIT_ERR_MEMORY;
                }

                st = parse_token_into_tag(elem, doc, child, err, err_sz);
                if (st != EDIT_OK) {
                    free_nbt_tree(elem);
                    free_list_items(new_items, count);
                    return st;
                }

                new_items[i] = elem;
                child = token_span(doc, child);
            }

            free_list_items(target->value.list.items, target->value.list.count);
            target->value.list.items = new_items;
            target->value.list.count = count;
            return EDIT_OK;
        }

        case TAG_Compound:
            return apply_object_patch_token(target, doc, tok_index, err, err_sz);

        default:
            set_err(err, err_sz, "editing not supported for this tag type");
            return EDIT_ERR_UNSUPPORTED;
    }
}

static int is_numeric_scalar_type(TagType type) {
    return type == TAG_Byte || type == TAG_Short || type == TAG_Int || type == TAG_Long || type == TAG_Float || type == TAG_Double;
}

static EditStatus apply_legacy_scalar_edit(NBTTag* target, const char* value_expr, char* err, size_t err_sz) {
    int64_t i64;
    double d;

    switch (target->type) {
        case TAG_Byte: {
            EditStatus st = parse_legacy_int64(value_expr, -128, 127, &i64, err, err_sz);
            if (st != EDIT_OK) return st;
            target->value.byte_val = (int8_t)i64;
            return EDIT_OK;
        }
        case TAG_Short: {
            EditStatus st = parse_legacy_int64(value_expr, -32768, 32767, &i64, err, err_sz);
            if (st != EDIT_OK) return st;
            target->value.short_val = (int16_t)i64;
            return EDIT_OK;
        }
        case TAG_Int: {
            EditStatus st = parse_legacy_int64(value_expr, INT32_MIN, INT32_MAX, &i64, err, err_sz);
            if (st != EDIT_OK) return st;
            target->value.int_val = (int32_t)i64;
            return EDIT_OK;
        }
        case TAG_Long: {
            EditStatus st = parse_legacy_int64(value_expr, INT64_MIN, INT64_MAX, &i64, err, err_sz);
            if (st != EDIT_OK) return st;
            target->value.long_val = i64;
            return EDIT_OK;
        }
        case TAG_Float: {
            EditStatus st = parse_legacy_double(value_expr, &d, err, err_sz);
            if (st != EDIT_OK) return st;
            if (d < -FLT_MAX || d > FLT_MAX) {
                set_err(err, err_sz, "numeric overflow");
                return EDIT_ERR_NUMERIC_RANGE;
            }
            target->value.float_val = (float)d;
            return EDIT_OK;
        }
        case TAG_Double: {
            EditStatus st = parse_legacy_double(value_expr, &d, err, err_sz);
            if (st != EDIT_OK) return st;
            target->value.double_val = d;
            return EDIT_OK;
        }
        default:
            set_err(err, err_sz, "legacy scalar parsing not supported for this type");
            return EDIT_ERR_TYPE_MISMATCH;
    }
}

EditStatus parse_json_for_tag_type(NBTTag* target, const char* value_expr, char* err, size_t err_sz) {
    EditStatus st;
    JsonDoc doc;

    if (!target || !value_expr) {
        set_err(err, err_sz, "invalid edit arguments");
        return EDIT_ERR_PATH_SYNTAX;
    }

    if (target->type == TAG_Compound) {
        return apply_json_patch_to_compound(target, value_expr, err, err_sz);
    }

    st = parse_json_doc(value_expr, &doc, err, err_sz);
    if (st != EDIT_OK) {
        if (is_numeric_scalar_type(target->type)) {
            return apply_legacy_scalar_edit(target, value_expr, err, err_sz);
        }
        return st;
    }

    st = parse_token_into_tag(target, &doc, 0, err, err_sz);
    free_json_doc(&doc);
    return st;
}

EditStatus apply_json_patch_to_compound(NBTTag* compound, const char* json_object, char* err, size_t err_sz) {
    EditStatus st;
    JsonDoc doc;

    if (!compound || compound->type != TAG_Compound) {
        set_err(err, err_sz, "type mismatch: target is not a compound");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    st = parse_json_doc(json_object, &doc, err, err_sz);
    if (st != EDIT_OK) return st;

    st = apply_object_patch_token(compound, &doc, 0, err, err_sz);
    free_json_doc(&doc);
    return st;
}

EditStatus parse_json_for_list_element(NBTTag* list_tag, int index, const char* value_expr, char* err, size_t err_sz) {
    EditStatus st;
    JsonDoc doc;
    NBTTag* item;

    if (!list_tag || list_tag->type != TAG_List) {
        set_err(err, err_sz, "type mismatch: target is not a list");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    if (index < 0 || index >= list_tag->value.list.count) {
        set_err(err, err_sz, "index out of bounds");
        return EDIT_ERR_INDEX_BOUNDS;
    }

    if (list_tag->value.list.element_type == TAG_End) {
        set_err(err, err_sz, "unsupported operation: cannot infer element type for empty TAG_End list");
        return EDIT_ERR_UNSUPPORTED;
    }

    item = list_tag->value.list.items[index];
    if (!item || item->type != list_tag->value.list.element_type) {
        if (item) free_nbt_tree(item);
        item = create_list_element(list_tag->value.list.element_type);
        if (!item) {
            set_err(err, err_sz, "out of memory");
            return EDIT_ERR_MEMORY;
        }
        list_tag->value.list.items[index] = item;
    }

    st = parse_json_doc(value_expr, &doc, err, err_sz);
    if (st != EDIT_OK) {
        if (is_numeric_scalar_type(item->type)) {
            return apply_legacy_scalar_edit(item, value_expr, err, err_sz);
        }
        return st;
    }

    st = parse_token_into_tag(item, &doc, 0, err, err_sz);
    free_json_doc(&doc);
    return st;
}

EditStatus parse_json_for_array_element(NBTTag* array_tag, int index, const char* value_expr, char* err, size_t err_sz) {
    NBTTag temp;
    EditStatus st;

    if (!array_tag) {
        set_err(err, err_sz, "invalid array target");
        return EDIT_ERR_TYPE_MISMATCH;
    }

    memset(&temp, 0, sizeof(temp));

    switch (array_tag->type) {
        case TAG_Byte_Array:
            if (index < 0 || index >= array_tag->value.byte_array.length) {
                set_err(err, err_sz, "index out of bounds");
                return EDIT_ERR_INDEX_BOUNDS;
            }
            temp.type = TAG_Byte;
            st = parse_json_for_tag_type(&temp, value_expr, err, err_sz);
            if (st != EDIT_OK) return st;
            array_tag->value.byte_array.data[index] = (uint8_t)temp.value.byte_val;
            return EDIT_OK;

        case TAG_Int_Array:
            if (index < 0 || index >= array_tag->value.int_array.length) {
                set_err(err, err_sz, "index out of bounds");
                return EDIT_ERR_INDEX_BOUNDS;
            }
            temp.type = TAG_Int;
            st = parse_json_for_tag_type(&temp, value_expr, err, err_sz);
            if (st != EDIT_OK) return st;
            array_tag->value.int_array.data[index] = temp.value.int_val;
            return EDIT_OK;

        case TAG_Long_Array:
            if (index < 0 || index >= array_tag->value.long_array.length) {
                set_err(err, err_sz, "index out of bounds");
                return EDIT_ERR_INDEX_BOUNDS;
            }
            temp.type = TAG_Long;
            st = parse_json_for_tag_type(&temp, value_expr, err, err_sz);
            if (st != EDIT_OK) return st;
            array_tag->value.long_array.data[index] = temp.value.long_val;
            return EDIT_OK;

        default:
            set_err(err, err_sz, "type mismatch: target is not an editable array");
            return EDIT_ERR_TYPE_MISMATCH;
    }
}
