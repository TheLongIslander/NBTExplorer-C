#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nbt_builder.h"
#include "snbt.h"

#define SNBT_MAX_DEPTH 512u

typedef struct {
    const char* text;
    size_t length;
    size_t pos;
    size_t depth;
    size_t nodes;
    char* err;
    size_t err_sz;
    int failed;
} SnbtParser;

typedef struct {
    char* data;
    size_t length;
    size_t capacity;
    size_t depth;
    int pretty;
    char* err;
    size_t err_sz;
    int failed;
} StringWriter;

static char* copy_n(const char* value, size_t length) {
    char* copy = malloc(length + 1);
    if (!copy) return NULL;
    if (length > 0) memcpy(copy, value, length);
    copy[length] = '\0';
    return copy;
}

static void parser_fail(SnbtParser* p, const char* message) {
    size_t line = 1;
    size_t column = 1;
    if (!p || p->failed) return;
    for (size_t i = 0; i < p->pos && i < p->length; ++i) {
        if (p->text[i] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    if (p->err && p->err_sz > 0) {
        snprintf(p->err, p->err_sz, "%s at line %zu, column %zu",
                 message, line, column);
    }
    p->failed = 1;
}

static void skip_space(SnbtParser* p) {
    while (p->pos < p->length && isspace((unsigned char)p->text[p->pos])) ++p->pos;
}

static int consume(SnbtParser* p, char expected) {
    skip_space(p);
    if (p->pos >= p->length || p->text[p->pos] != expected) return 0;
    ++p->pos;
    return 1;
}

static NBTTag* new_tag(SnbtParser* p, TagType type, const char* name) {
    NBTTag* tag;
    size_t name_length = name ? strlen(name) : 0;
    if (++p->nodes > p->length + 1) {
        parser_fail(p, "SNBT contains too many values");
        return NULL;
    }
    tag = calloc(1, sizeof(*tag));
    if (!tag) {
        parser_fail(p, "out of memory while parsing SNBT");
        return NULL;
    }
    tag->type = type;
    tag->name = copy_n(name ? name : "", name_length);
    if (!tag->name) {
        free(tag);
        parser_fail(p, "out of memory while parsing SNBT");
        return NULL;
    }
    return tag;
}

static int append_byte(char** data, size_t* length, size_t* capacity, unsigned char value) {
    char* grown;
    size_t next;
    if (*length == SIZE_MAX) return 0;
    next = *length + 1;
    if (next > *capacity) {
        size_t new_capacity = *capacity ? *capacity * 2 : 32;
        if (new_capacity < next || new_capacity < *capacity) new_capacity = next;
        grown = realloc(*data, new_capacity);
        if (!grown) return 0;
        *data = grown;
        *capacity = new_capacity;
    }
    (*data)[(*length)++] = (char)value;
    return 1;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int read_hex4(SnbtParser* p, uint32_t* value) {
    uint32_t result = 0;
    if (p->length - p->pos < 4) return 0;
    for (int i = 0; i < 4; ++i) {
        int digit = hex_value(p->text[p->pos++]);
        if (digit < 0) return 0;
        result = (result << 4) | (uint32_t)digit;
    }
    *value = result;
    return 1;
}

static int append_utf8(char** data, size_t* length, size_t* capacity, uint32_t cp) {
    if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    if (cp <= 0x7f) return append_byte(data, length, capacity, (unsigned char)cp);
    if (cp <= 0x7ff) {
        return append_byte(data, length, capacity, (unsigned char)(0xc0 | (cp >> 6))) &&
               append_byte(data, length, capacity, (unsigned char)(0x80 | (cp & 0x3f)));
    }
    if (cp <= 0xffff) {
        return append_byte(data, length, capacity, (unsigned char)(0xe0 | (cp >> 12))) &&
               append_byte(data, length, capacity, (unsigned char)(0x80 | ((cp >> 6) & 0x3f))) &&
               append_byte(data, length, capacity, (unsigned char)(0x80 | (cp & 0x3f)));
    }
    return append_byte(data, length, capacity, (unsigned char)(0xf0 | (cp >> 18))) &&
           append_byte(data, length, capacity, (unsigned char)(0x80 | ((cp >> 12) & 0x3f))) &&
           append_byte(data, length, capacity, (unsigned char)(0x80 | ((cp >> 6) & 0x3f))) &&
           append_byte(data, length, capacity, (unsigned char)(0x80 | (cp & 0x3f)));
}

static char* parse_quoted_string(SnbtParser* p) {
    char quote;
    char* data = NULL;
    size_t length = 0;
    size_t capacity = 0;
    skip_space(p);
    if (p->pos >= p->length || (p->text[p->pos] != '\'' && p->text[p->pos] != '"')) {
        parser_fail(p, "expected quoted SNBT string");
        return NULL;
    }
    quote = p->text[p->pos++];
    while (p->pos < p->length) {
        unsigned char c = (unsigned char)p->text[p->pos++];
        if (c == (unsigned char)quote) {
            if (!append_byte(&data, &length, &capacity, '\0')) goto memory_fail;
            return data;
        }
        if (c < 0x20) {
            parser_fail(p, "unescaped control character in SNBT string");
            free(data);
            return NULL;
        }
        if (c != '\\') {
            if (!append_byte(&data, &length, &capacity, c)) goto memory_fail;
            continue;
        }
        if (p->pos >= p->length) {
            parser_fail(p, "unterminated escape in SNBT string");
            free(data);
            return NULL;
        }
        c = (unsigned char)p->text[p->pos++];
        switch (c) {
            case '\\': case '\'': case '"':
                if (!append_byte(&data, &length, &capacity, c)) goto memory_fail;
                break;
            case 'b': if (!append_byte(&data, &length, &capacity, '\b')) goto memory_fail; break;
            case 'f': if (!append_byte(&data, &length, &capacity, '\f')) goto memory_fail; break;
            case 'n': if (!append_byte(&data, &length, &capacity, '\n')) goto memory_fail; break;
            case 'r': if (!append_byte(&data, &length, &capacity, '\r')) goto memory_fail; break;
            case 't': if (!append_byte(&data, &length, &capacity, '\t')) goto memory_fail; break;
            case 'u': {
                uint32_t cp;
                if (!read_hex4(p, &cp)) {
                    parser_fail(p, "invalid Unicode escape in SNBT string");
                    free(data);
                    return NULL;
                }
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    uint32_t low;
                    if (p->length - p->pos < 6 || p->text[p->pos] != '\\' ||
                        p->text[p->pos + 1] != 'u') {
                        parser_fail(p, "missing low surrogate in SNBT string");
                        free(data);
                        return NULL;
                    }
                    p->pos += 2;
                    if (!read_hex4(p, &low) || low < 0xdc00 || low > 0xdfff) {
                        parser_fail(p, "invalid low surrogate in SNBT string");
                        free(data);
                        return NULL;
                    }
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                }
                if (!append_utf8(&data, &length, &capacity, cp)) {
                    parser_fail(p, cp == 0 ? "NUL is not representable by the NBT tree string model" :
                                           "invalid Unicode code point in SNBT string");
                    free(data);
                    return NULL;
                }
                break;
            }
            default:
                parser_fail(p, "invalid escape in SNBT string");
                free(data);
                return NULL;
        }
    }
    parser_fail(p, "unterminated SNBT string");
    free(data);
    return NULL;

memory_fail:
    parser_fail(p, "out of memory while parsing SNBT string");
    free(data);
    return NULL;
}

static int is_token_delimiter(char c) {
    return isspace((unsigned char)c) || c == ',' || c == ':' || c == ']' ||
           c == '}' || c == '[' || c == '{' || c == ';';
}

static char* parse_token(SnbtParser* p) {
    size_t start;
    skip_space(p);
    start = p->pos;
    while (p->pos < p->length && !is_token_delimiter(p->text[p->pos])) ++p->pos;
    if (p->pos == start) {
        parser_fail(p, "expected SNBT token");
        return NULL;
    }
    return copy_n(p->text + start, p->pos - start);
}

static int equal_case(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static int integer_syntax(const char* token, size_t length) {
    size_t pos = 0;
    if (pos < length && (token[pos] == '+' || token[pos] == '-')) ++pos;
    if (pos == length) return 0;
    for (; pos < length; ++pos) if (!isdigit((unsigned char)token[pos])) return 0;
    return 1;
}

static int float_syntax(const char* token, size_t length) {
    char* copy;
    char* end;
    double unused;
    if (length == 0) return 0;
    copy = copy_n(token, length);
    if (!copy) return 0;
    errno = 0;
    end = NULL;
    unused = strtod(copy, &end);
    (void)unused;
    if (end == copy || *end != '\0') {
        free(copy);
        return 0;
    }
    free(copy);
    return 1;
}

static NBTTag* parse_scalar_token(SnbtParser* p, const char* name, char* token) {
    size_t length = strlen(token);
    char suffix = length ? (char)tolower((unsigned char)token[length - 1]) : '\0';
    size_t numeric_length = length;
    TagType type = TAG_String;
    NBTTag* tag;

    if (equal_case(token, "true") || equal_case(token, "false")) {
        tag = new_tag(p, TAG_Byte, name);
        if (tag) tag->value.byte_val = equal_case(token, "true") ? 1 : 0;
        return tag;
    }

    if ((suffix == 'b' || suffix == 's' || suffix == 'l') &&
        integer_syntax(token, length - 1)) {
        intmax_t value;
        char* end;
        int valid;
        token[length - 1] = '\0';
        errno = 0;
        value = strtoimax(token, &end, 10);
        valid = errno != ERANGE && end && *end == '\0';
        token[length - 1] = suffix;
        if (!valid) {
            parser_fail(p, "SNBT integer is out of range");
            return NULL;
        }
        type = suffix == 'b' ? TAG_Byte : suffix == 's' ? TAG_Short : TAG_Long;
        if ((type == TAG_Byte && (value < INT8_MIN || value > INT8_MAX)) ||
            (type == TAG_Short && (value < INT16_MIN || value > INT16_MAX)) ||
            (type == TAG_Long && (value < INT64_MIN || value > INT64_MAX))) {
            parser_fail(p, "SNBT integer is out of range for its suffix");
            return NULL;
        }
        tag = new_tag(p, type, name);
        if (!tag) return NULL;
        if (type == TAG_Byte) tag->value.byte_val = (int8_t)value;
        else if (type == TAG_Short) tag->value.short_val = (int16_t)value;
        else tag->value.long_val = (int64_t)value;
        return tag;
    }

    if ((suffix == 'f' || suffix == 'd') && float_syntax(token, length - 1)) {
        double value;
        char* end;
        int valid;
        token[length - 1] = '\0';
        errno = 0;
        value = strtod(token, &end);
        valid = errno != ERANGE && end && *end == '\0';
        token[length - 1] = suffix;
        if (!valid) {
            parser_fail(p, "SNBT floating-point value is out of range");
            return NULL;
        }
        type = suffix == 'f' ? TAG_Float : TAG_Double;
        tag = new_tag(p, type, name);
        if (!tag) return NULL;
        if (type == TAG_Float) tag->value.float_val = (float)value;
        else tag->value.double_val = value;
        return tag;
    }

    if (integer_syntax(token, numeric_length)) {
        intmax_t value;
        char* end;
        errno = 0;
        value = strtoimax(token, &end, 10);
        if (errno == ERANGE || !end || *end != '\0' || value < INT32_MIN || value > INT32_MAX) {
            parser_fail(p, "unsuffixed SNBT integer is out of TAG_Int range");
            return NULL;
        }
        tag = new_tag(p, TAG_Int, name);
        if (tag) tag->value.int_val = (int32_t)value;
        return tag;
    }

    if (float_syntax(token, numeric_length) &&
        (strchr(token, '.') || strchr(token, 'e') || strchr(token, 'E') ||
         equal_case(token, "nan") || equal_case(token, "infinity") ||
         equal_case(token, "+infinity") || equal_case(token, "-infinity"))) {
        char* end;
        double value;
        errno = 0;
        value = strtod(token, &end);
        if (errno == ERANGE || !end || *end != '\0') {
            parser_fail(p, "SNBT floating-point value is out of range");
            return NULL;
        }
        tag = new_tag(p, TAG_Double, name);
        if (tag) tag->value.double_val = value;
        return tag;
    }

    tag = new_tag(p, TAG_String, name);
    if (!tag) return NULL;
    tag->value.string_val = copy_n(token, length);
    if (!tag->value.string_val) {
        parser_fail(p, "out of memory while parsing SNBT string");
        free_nbt_tree(tag);
        return NULL;
    }
    return tag;
}

static NBTTag* parse_value(SnbtParser* p, const char* name);

static int append_compound(NBTTag* compound, NBTTag* child, SnbtParser* p) {
    NBTTag** grown;
    if (compound->value.compound.count == INT_MAX) {
        parser_fail(p, "SNBT compound contains too many values");
        return 0;
    }
    grown = realloc(compound->value.compound.items,
                    (size_t)(compound->value.compound.count + 1) * sizeof(NBTTag*));
    if (!grown) {
        parser_fail(p, "out of memory while parsing SNBT compound");
        return 0;
    }
    compound->value.compound.items = grown;
    compound->value.compound.items[compound->value.compound.count++] = child;
    return 1;
}

static NBTTag* parse_compound(SnbtParser* p, const char* name) {
    NBTTag* compound = new_tag(p, TAG_Compound, name);
    if (!compound) return NULL;
    ++p->pos; /* { */
    skip_space(p);
    if (consume(p, '}')) return compound;
    while (!p->failed) {
        char* key;
        NBTTag* child;
        skip_space(p);
        if (p->pos < p->length && (p->text[p->pos] == '\'' || p->text[p->pos] == '"'))
            key = parse_quoted_string(p);
        else
            key = parse_token(p);
        if (!key) goto fail;
        if (!consume(p, ':')) {
            free(key);
            parser_fail(p, "expected ':' after SNBT compound key");
            goto fail;
        }
        child = parse_value(p, key);
        free(key);
        if (!child) goto fail;
        if (!append_compound(compound, child, p)) {
            free_nbt_tree(child);
            goto fail;
        }
        skip_space(p);
        if (consume(p, '}')) return compound;
        if (!consume(p, ',')) {
            parser_fail(p, "expected ',' or '}' in SNBT compound");
            goto fail;
        }
        skip_space(p);
        if (consume(p, '}')) return compound; /* accepted trailing comma */
    }

fail:
    free_nbt_tree(compound);
    return NULL;
}

static int append_list(NBTTag* list, NBTTag* item, SnbtParser* p) {
    NBTTag** grown;
    if (list->value.list.count == INT_MAX) {
        parser_fail(p, "SNBT list contains too many values");
        return 0;
    }
    grown = realloc(list->value.list.items,
                    (size_t)(list->value.list.count + 1) * sizeof(NBTTag*));
    if (!grown) {
        parser_fail(p, "out of memory while parsing SNBT list");
        return 0;
    }
    list->value.list.items = grown;
    list->value.list.items[list->value.list.count++] = item;
    return 1;
}

static NBTTag* parse_typed_array(SnbtParser* p, const char* name, char kind) {
    TagType array_type = kind == 'B' ? TAG_Byte_Array : kind == 'I' ? TAG_Int_Array : TAG_Long_Array;
    TagType item_type = kind == 'B' ? TAG_Byte : kind == 'I' ? TAG_Int : TAG_Long;
    NBTTag* array = new_tag(p, array_type, name);
    size_t count = 0;
    size_t capacity = 0;
    void* values = NULL;

    skip_space(p);
    if (consume(p, ']')) return array;
    while (!p->failed) {
        NBTTag* item = parse_value(p, "");
        size_t width = kind == 'B' ? sizeof(uint8_t) : kind == 'I' ? sizeof(int32_t) : sizeof(int64_t);
        if (!item) goto fail;
        if (item->type != item_type) {
            free_nbt_tree(item);
            parser_fail(p, "typed SNBT array element has the wrong numeric suffix");
            goto fail;
        }
        if (count == capacity) {
            size_t new_capacity = capacity ? capacity * 2 : 8;
            void* grown;
            if (new_capacity < capacity || new_capacity > (size_t)INT32_MAX ||
                new_capacity > SIZE_MAX / width) {
                free_nbt_tree(item);
                parser_fail(p, "typed SNBT array is too large");
                goto fail;
            }
            grown = realloc(values, new_capacity * width);
            if (!grown) {
                free_nbt_tree(item);
                parser_fail(p, "out of memory while parsing typed SNBT array");
                goto fail;
            }
            values = grown;
            capacity = new_capacity;
        }
        if (kind == 'B') ((uint8_t*)values)[count] = (uint8_t)item->value.byte_val;
        else if (kind == 'I') ((int32_t*)values)[count] = item->value.int_val;
        else ((int64_t*)values)[count] = item->value.long_val;
        ++count;
        free_nbt_tree(item);
        skip_space(p);
        if (consume(p, ']')) break;
        if (!consume(p, ',')) {
            parser_fail(p, "expected ',' or ']' in typed SNBT array");
            goto fail;
        }
        skip_space(p);
        if (consume(p, ']')) break;
    }
    if (array_type == TAG_Byte_Array) {
        array->value.byte_array.length = (int32_t)count;
        array->value.byte_array.data = values;
    } else if (array_type == TAG_Int_Array) {
        array->value.int_array.length = (int32_t)count;
        array->value.int_array.data = values;
    } else {
        array->value.long_array.length = (int32_t)count;
        array->value.long_array.data = values;
    }
    return array;

fail:
    free(values);
    free_nbt_tree(array);
    return NULL;
}

static NBTTag* parse_list_or_array(SnbtParser* p, const char* name) {
    NBTTag* list;
    ++p->pos; /* [ */
    skip_space(p);
    if (p->pos < p->length &&
        (p->text[p->pos] == 'B' || p->text[p->pos] == 'b' ||
         p->text[p->pos] == 'I' || p->text[p->pos] == 'i' ||
         p->text[p->pos] == 'L' || p->text[p->pos] == 'l')) {
        char kind = (char)toupper((unsigned char)p->text[p->pos]);
        size_t saved = p->pos;
        ++p->pos;
        skip_space(p);
        if (p->pos < p->length && p->text[p->pos] == ';') {
            ++p->pos;
            return parse_typed_array(p, name, kind);
        }
        p->pos = saved;
    }

    list = new_tag(p, TAG_List, name);
    if (!list) return NULL;
    list->value.list.element_type = TAG_End;
    if (consume(p, ']')) return list;
    while (!p->failed) {
        NBTTag* item = parse_value(p, "");
        if (!item) goto fail;
        if (list->value.list.count == 0) list->value.list.element_type = item->type;
        if (item->type != list->value.list.element_type) {
            free_nbt_tree(item);
            parser_fail(p, "SNBT lists must contain a single tag type");
            goto fail;
        }
        if (!append_list(list, item, p)) {
            free_nbt_tree(item);
            goto fail;
        }
        skip_space(p);
        if (consume(p, ']')) return list;
        if (!consume(p, ',')) {
            parser_fail(p, "expected ',' or ']' in SNBT list");
            goto fail;
        }
        skip_space(p);
        if (consume(p, ']')) return list;
    }

fail:
    free_nbt_tree(list);
    return NULL;
}

static NBTTag* parse_value(SnbtParser* p, const char* name) {
    NBTTag* value = NULL;
    if (++p->depth > SNBT_MAX_DEPTH) {
        --p->depth;
        parser_fail(p, "SNBT nesting depth limit exceeded");
        return NULL;
    }
    skip_space(p);
    if (p->pos >= p->length) {
        parser_fail(p, "expected SNBT value");
        goto done;
    }
    if (p->text[p->pos] == '{') {
        value = parse_compound(p, name);
    } else if (p->text[p->pos] == '[') {
        value = parse_list_or_array(p, name);
    } else if (p->text[p->pos] == '\'' || p->text[p->pos] == '"') {
        char* string = parse_quoted_string(p);
        if (string) {
            value = new_tag(p, TAG_String, name);
            if (value) value->value.string_val = string;
            else free(string);
        }
    } else {
        char* token = parse_token(p);
        if (token) {
            value = parse_scalar_token(p, name, token);
            free(token);
        }
    }

done:
    --p->depth;
    return value;
}

NBTTag* snbt_parse(const char* text, const char* root_name, char* err, size_t err_sz) {
    SnbtParser parser;
    NBTTag* root;
    if (err && err_sz > 0) err[0] = '\0';
    if (!text) {
        if (err && err_sz > 0) snprintf(err, err_sz, "SNBT input is null");
        return NULL;
    }
    memset(&parser, 0, sizeof(parser));
    parser.text = text;
    parser.length = strlen(text);
    parser.err = err;
    parser.err_sz = err_sz;
    root = parse_value(&parser, root_name ? root_name : "");
    if (!root) return NULL;
    skip_space(&parser);
    if (parser.pos != parser.length) {
        parser_fail(&parser, "trailing characters after SNBT value");
        free_nbt_tree(root);
        return NULL;
    }
    return root;
}

static int writer_fail(StringWriter* w, const char* message) {
    if (w && !w->failed && w->err && w->err_sz > 0)
        snprintf(w->err, w->err_sz, "%s", message);
    if (w) w->failed = 1;
    return 0;
}

static int string_reserve(StringWriter* w, size_t extra) {
    size_t needed;
    size_t capacity;
    char* grown;
    if (w->failed) return 0;
    if (extra > SIZE_MAX - w->length - 1) return writer_fail(w, "SNBT output is too large");
    needed = w->length + extra + 1;
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
    if (!grown) return writer_fail(w, "out of memory while serializing SNBT");
    w->data = grown;
    w->capacity = capacity;
    return 1;
}

static int string_write_n(StringWriter* w, const char* value, size_t length) {
    if (!string_reserve(w, length)) return 0;
    if (length > 0) memcpy(w->data + w->length, value, length);
    w->length += length;
    w->data[w->length] = '\0';
    return 1;
}

static int string_write(StringWriter* w, const char* value) {
    return string_write_n(w, value, strlen(value));
}

static int string_printf(StringWriter* w, const char* format, ...) {
    va_list args;
    va_list copy;
    int needed;
    va_start(args, format);
    va_copy(copy, args);
    needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (needed < 0 || !string_reserve(w, (size_t)needed)) {
        va_end(args);
        return writer_fail(w, "failed to format SNBT value");
    }
    vsnprintf(w->data + w->length, w->capacity - w->length, format, args);
    va_end(args);
    w->length += (size_t)needed;
    return 1;
}

static int write_indent(StringWriter* w, size_t depth) {
    if (!w->pretty) return 1;
    for (size_t i = 0; i < depth * 2; ++i)
        if (!string_write_n(w, " ", 1)) return 0;
    return 1;
}

static int write_quoted(StringWriter* w, const char* value) {
    const unsigned char* p = (const unsigned char*)(value ? value : "");
    if (!string_write_n(w, "\"", 1)) return 0;
    while (*p) {
        switch (*p) {
            case '"': if (!string_write(w, "\\\"")) return 0; break;
            case '\\': if (!string_write(w, "\\\\")) return 0; break;
            case '\b': if (!string_write(w, "\\b")) return 0; break;
            case '\f': if (!string_write(w, "\\f")) return 0; break;
            case '\n': if (!string_write(w, "\\n")) return 0; break;
            case '\r': if (!string_write(w, "\\r")) return 0; break;
            case '\t': if (!string_write(w, "\\t")) return 0; break;
            default:
                if (*p < 0x20) {
                    if (!string_printf(w, "\\u%04x", (unsigned int)*p)) return 0;
                } else if (!string_write_n(w, (const char*)p, 1)) return 0;
                break;
        }
        ++p;
    }
    return string_write_n(w, "\"", 1);
}

static int write_snbt_value(StringWriter* w, const NBTTag* tag);

static int write_separator(StringWriter* w, int index, size_t child_depth) {
    if (index > 0 && !string_write_n(w, ",", 1)) return 0;
    if (w->pretty) {
        if (!string_write_n(w, "\n", 1) || !write_indent(w, child_depth)) return 0;
    } else if (index > 0 && !string_write_n(w, " ", 1)) return 0;
    return 1;
}

static int valid_storage(StringWriter* w, int count, const void* data, const char* kind) {
    char message[128];
    if (count < 0 || (count > 0 && !data)) {
        snprintf(message, sizeof(message), "invalid %s storage", kind);
        return writer_fail(w, message);
    }
    return 1;
}

static int write_float(StringWriter* w, double value, int is_float) {
    if (isnan(value)) return string_write(w, is_float ? "NaNf" : "NaNd");
    if (isinf(value)) {
        if (value < 0 && !string_write_n(w, "-", 1)) return 0;
        return string_write(w, is_float ? "Infinityf" : "Infinityd");
    }
    return is_float ? string_printf(w, "%.9gf", value) : string_printf(w, "%.17gd", value);
}

static int write_snbt_value(StringWriter* w, const NBTTag* tag) {
    if (!tag) return writer_fail(w, "cannot serialize a null SNBT tag");
    if (++w->depth > SNBT_MAX_DEPTH) {
        --w->depth;
        return writer_fail(w, "SNBT nesting depth limit exceeded");
    }
    switch (tag->type) {
        case TAG_Byte:
            if (!string_printf(w, "%" PRId8 "b", tag->value.byte_val)) goto fail;
            break;
        case TAG_Short:
            if (!string_printf(w, "%" PRId16 "s", tag->value.short_val)) goto fail;
            break;
        case TAG_Int:
            if (!string_printf(w, "%" PRId32, tag->value.int_val)) goto fail;
            break;
        case TAG_Long:
            if (!string_printf(w, "%" PRId64 "L", tag->value.long_val)) goto fail;
            break;
        case TAG_Float:
            if (!write_float(w, tag->value.float_val, 1)) goto fail;
            break;
        case TAG_Double:
            if (!write_float(w, tag->value.double_val, 0)) goto fail;
            break;
        case TAG_String:
            if (!write_quoted(w, tag->value.string_val)) goto fail;
            break;
        case TAG_Byte_Array:
            if (!valid_storage(w, tag->value.byte_array.length,
                               tag->value.byte_array.data, "TAG_Byte_Array") ||
                !string_write(w, "[B;")) goto fail;
            for (int32_t i = 0; i < tag->value.byte_array.length; ++i) {
                if (i > 0 && !string_write(w, ", ")) goto fail;
                if (!string_printf(w, "%" PRId8 "b", (int8_t)tag->value.byte_array.data[i])) goto fail;
            }
            if (!string_write_n(w, "]", 1)) goto fail;
            break;
        case TAG_Int_Array:
            if (!valid_storage(w, tag->value.int_array.length,
                               tag->value.int_array.data, "TAG_Int_Array") ||
                !string_write(w, "[I;")) goto fail;
            for (int32_t i = 0; i < tag->value.int_array.length; ++i) {
                if (i > 0 && !string_write(w, ", ")) goto fail;
                if (!string_printf(w, "%" PRId32, tag->value.int_array.data[i])) goto fail;
            }
            if (!string_write_n(w, "]", 1)) goto fail;
            break;
        case TAG_Long_Array:
            if (!valid_storage(w, tag->value.long_array.length,
                               tag->value.long_array.data, "TAG_Long_Array") ||
                !string_write(w, "[L;")) goto fail;
            for (int32_t i = 0; i < tag->value.long_array.length; ++i) {
                if (i > 0 && !string_write(w, ", ")) goto fail;
                if (!string_printf(w, "%" PRId64 "L", tag->value.long_array.data[i])) goto fail;
            }
            if (!string_write_n(w, "]", 1)) goto fail;
            break;
        case TAG_List:
            if (!valid_storage(w, tag->value.list.count, tag->value.list.items, "TAG_List") ||
                !string_write_n(w, "[", 1)) goto fail;
            for (int i = 0; i < tag->value.list.count; ++i) {
                NBTTag* item = tag->value.list.items[i];
                if (!item || item->type != tag->value.list.element_type) {
                    writer_fail(w, "SNBT list contains a null or mismatched element");
                    goto fail;
                }
                if (!write_separator(w, i, w->depth) || !write_snbt_value(w, item)) goto fail;
            }
            if (tag->value.list.count > 0 && w->pretty &&
                (!string_write_n(w, "\n", 1) || !write_indent(w, w->depth - 1))) goto fail;
            if (!string_write_n(w, "]", 1)) goto fail;
            break;
        case TAG_Compound:
            if (!valid_storage(w, tag->value.compound.count,
                               tag->value.compound.items, "TAG_Compound") ||
                !string_write_n(w, "{", 1)) goto fail;
            for (int i = 0; i < tag->value.compound.count; ++i) {
                NBTTag* child = tag->value.compound.items[i];
                if (!child || child->type == TAG_End) {
                    writer_fail(w, "SNBT compound contains an invalid child");
                    goto fail;
                }
                if (!write_separator(w, i, w->depth) ||
                    !write_quoted(w, child->name ? child->name : "") ||
                    !string_write(w, w->pretty ? ": " : ":") ||
                    !write_snbt_value(w, child)) goto fail;
            }
            if (tag->value.compound.count > 0 && w->pretty &&
                (!string_write_n(w, "\n", 1) || !write_indent(w, w->depth - 1))) goto fail;
            if (!string_write_n(w, "}", 1)) goto fail;
            break;
        case TAG_End:
        default:
            writer_fail(w, "TAG_End cannot be represented as an SNBT value");
            goto fail;
    }
    --w->depth;
    return 1;

fail:
    --w->depth;
    return 0;
}

char* snbt_serialize(const NBTTag* root, int pretty, char* err, size_t err_sz) {
    StringWriter writer;
    if (err && err_sz > 0) err[0] = '\0';
    if (!root) {
        if (err && err_sz > 0) snprintf(err, err_sz, "SNBT root is null");
        return NULL;
    }
    memset(&writer, 0, sizeof(writer));
    writer.pretty = pretty != 0;
    writer.err = err;
    writer.err_sz = err_sz;
    if (!write_snbt_value(&writer, root)) {
        free(writer.data);
        return NULL;
    }
    if (!writer.data) {
        writer.data = copy_n("", 0);
        if (!writer.data && err && err_sz > 0) snprintf(err, err_sz, "out of memory");
    }
    return writer.data;
}
