#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nbt_json.h"
#include "platform.h"

typedef struct {
    FILE* out;
    int pretty;
    int failed;
} JsonWriter;

static char* duplicate_text(const char* text) {
    size_t len = strlen(text ? text : "");
    char* copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, text ? text : "", len + 1);
    return copy;
}

static void set_err(char* err, size_t err_sz, const char* message) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", message ? message : "unknown error");
    }
}

const char* nbt_tag_type_name(TagType type) {
    switch (type) {
        case TAG_End: return "End";
        case TAG_Byte: return "Byte";
        case TAG_Short: return "Short";
        case TAG_Int: return "Int";
        case TAG_Long: return "Long";
        case TAG_Float: return "Float";
        case TAG_Double: return "Double";
        case TAG_Byte_Array: return "Byte Array";
        case TAG_String: return "String";
        case TAG_List: return "List";
        case TAG_Compound: return "Compound";
        case TAG_Int_Array: return "Int Array";
        case TAG_Long_Array: return "Long Array";
        default: return "Unknown";
    }
}

static void jw_putc(JsonWriter* writer, int c) {
    if (!writer || writer->failed) return;
    if (fputc(c, writer->out) == EOF) writer->failed = 1;
}

static void jw_puts(JsonWriter* writer, const char* text) {
    if (!writer || writer->failed) return;
    if (fputs(text ? text : "", writer->out) == EOF) writer->failed = 1;
}

static void jw_printf(JsonWriter* writer, const char* format, ...) {
    va_list args;
    if (!writer || writer->failed) return;
    va_start(args, format);
    if (vfprintf(writer->out, format, args) < 0) writer->failed = 1;
    va_end(args);
}

static void jw_indent(JsonWriter* writer, int depth) {
    int i;
    if (!writer || !writer->pretty) return;
    jw_putc(writer, '\n');
    for (i = 0; i < depth * 2; i++) jw_putc(writer, ' ');
}

static void jw_string(JsonWriter* writer, const char* text) {
    const unsigned char* p = (const unsigned char*)(text ? text : "");

    jw_putc(writer, '"');
    while (*p) {
        unsigned char c = *p++;
        switch (c) {
            case '"': jw_puts(writer, "\\\""); break;
            case '\\': jw_puts(writer, "\\\\"); break;
            case '\b': jw_puts(writer, "\\b"); break;
            case '\f': jw_puts(writer, "\\f"); break;
            case '\n': jw_puts(writer, "\\n"); break;
            case '\r': jw_puts(writer, "\\r"); break;
            case '\t': jw_puts(writer, "\\t"); break;
            default:
                if (c < 0x20) jw_printf(writer, "\\u%04x", (unsigned int)c);
                else jw_putc(writer, c);
                break;
        }
    }
    jw_putc(writer, '"');
}

static int key_needs_quotes(const char* key) {
    const unsigned char* p = (const unsigned char*)(key ? key : "");
    if (*p == '\0') return 1;
    while (*p) {
        if (*p == '/' || *p == '[' || *p == ']' || *p == '"' || *p == '\\' ||
            *p == '\n' || *p == '\r' || *p == '\t') {
            return 1;
        }
        p++;
    }
    return 0;
}

static char* path_segment(const char* key) {
    const char* source = key ? key : "";
    size_t len = strlen(source);
    size_t capacity = len * 2 + 3;
    char* out = malloc(capacity);
    size_t used = 0;
    size_t i;

    if (!out) return NULL;
    if (!key_needs_quotes(source)) {
        memcpy(out, source, len + 1);
        return out;
    }

    out[used++] = '"';
    for (i = 0; i < len; i++) {
        char c = source[i];
        const char* escape = NULL;
        if (c == '"') escape = "\\\"";
        else if (c == '\\') escape = "\\\\";
        else if (c == '\n') escape = "\\n";
        else if (c == '\r') escape = "\\r";
        else if (c == '\t') escape = "\\t";

        if (escape) {
            size_t escape_len = strlen(escape);
            memcpy(out + used, escape, escape_len);
            used += escape_len;
        } else {
            out[used++] = c;
        }
    }
    out[used++] = '"';
    out[used] = '\0';
    return out;
}

static char* child_path(const char* parent_path, const char* child_name) {
    char* segment = path_segment(child_name);
    size_t parent_len = parent_path ? strlen(parent_path) : 0;
    size_t segment_len;
    char* out;

    if (!segment) return NULL;
    segment_len = strlen(segment);
    out = malloc(parent_len + (parent_len ? 1 : 0) + segment_len + 1);
    if (!out) {
        free(segment);
        return NULL;
    }

    if (parent_len) {
        memcpy(out, parent_path, parent_len);
        out[parent_len] = '/';
        memcpy(out + parent_len + 1, segment, segment_len + 1);
    } else {
        memcpy(out, segment, segment_len + 1);
    }
    free(segment);
    return out;
}

static char* indexed_path(const char* parent_path, int index) {
    int suffix_len = snprintf(NULL, 0, "[%d]", index);
    size_t parent_len = parent_path ? strlen(parent_path) : 0;
    char* out;

    if (suffix_len < 0) return NULL;
    out = malloc(parent_len + (size_t)suffix_len + 1);
    if (!out) return NULL;
    if (parent_len) memcpy(out, parent_path, parent_len);
    snprintf(out + parent_len, (size_t)suffix_len + 1, "[%d]", index);
    return out;
}

static void write_float(JsonWriter* writer, double value, int is_float) {
    if (isnan(value)) {
        jw_string(writer, "NaN");
    } else if (isinf(value)) {
        jw_string(writer, value < 0 ? "-Infinity" : "Infinity");
    } else if (is_float) {
        jw_printf(writer, "%.9g", value);
    } else {
        jw_printf(writer, "%.17g", value);
    }
}

static int write_tag(JsonWriter* writer, const NBTTag* tag, const char* path, int depth);

static int write_children(JsonWriter* writer, const NBTTag* tag, const char* path, int depth) {
    int i;
    int count = tag->type == TAG_Compound ? tag->value.compound.count : tag->value.list.count;

    jw_putc(writer, '[');
    for (i = 0; i < count; i++) {
        const NBTTag* child;
        char* next_path;
        if (i > 0) jw_putc(writer, ',');
        jw_indent(writer, depth + 1);

        if (tag->type == TAG_Compound) {
            child = tag->value.compound.items ? tag->value.compound.items[i] : NULL;
            next_path = child_path(path, child && child->name ? child->name : "");
        } else {
            child = tag->value.list.items ? tag->value.list.items[i] : NULL;
            next_path = indexed_path(path, i);
        }

        if (!child || !next_path) {
            free(next_path);
            return 0;
        }
        if (!write_tag(writer, child, next_path, depth + 1)) {
            free(next_path);
            return 0;
        }
        free(next_path);
    }
    if (count > 0) jw_indent(writer, depth);
    jw_putc(writer, ']');
    return !writer->failed;
}

static void write_byte_array(JsonWriter* writer, const ByteArray* array, int depth) {
    int32_t i;
    jw_putc(writer, '[');
    for (i = 0; i < array->length; i++) {
        if (i > 0) jw_putc(writer, ',');
        if (writer->pretty && i % 24 == 0) jw_indent(writer, depth + 1);
        else if (writer->pretty) jw_putc(writer, ' ');
        jw_printf(writer, "%d", (int)(int8_t)array->data[i]);
    }
    if (array->length > 0 && writer->pretty) jw_indent(writer, depth);
    jw_putc(writer, ']');
}

static void write_int_array(JsonWriter* writer, const IntArray* array, int depth) {
    int32_t i;
    jw_putc(writer, '[');
    for (i = 0; i < array->length; i++) {
        if (i > 0) jw_putc(writer, ',');
        if (writer->pretty && i % 12 == 0) jw_indent(writer, depth + 1);
        else if (writer->pretty) jw_putc(writer, ' ');
        jw_printf(writer, "%d", array->data[i]);
    }
    if (array->length > 0 && writer->pretty) jw_indent(writer, depth);
    jw_putc(writer, ']');
}

static void write_long_array(JsonWriter* writer, const LongArray* array, int depth) {
    int32_t i;
    jw_putc(writer, '[');
    for (i = 0; i < array->length; i++) {
        if (i > 0) jw_putc(writer, ',');
        if (writer->pretty && i % 8 == 0) jw_indent(writer, depth + 1);
        else if (writer->pretty) jw_putc(writer, ' ');
        jw_printf(writer, "%lld", (long long)array->data[i]);
    }
    if (array->length > 0 && writer->pretty) jw_indent(writer, depth);
    jw_putc(writer, ']');
}

static int write_tag(JsonWriter* writer, const NBTTag* tag, const char* path, int depth) {
    if (!writer || !tag) return 0;

    jw_putc(writer, '{');
    jw_indent(writer, depth + 1);
    jw_puts(writer, "\"name\":");
    if (writer->pretty) jw_putc(writer, ' ');
    jw_string(writer, tag->name);
    jw_putc(writer, ',');
    jw_indent(writer, depth + 1);
    jw_puts(writer, "\"path\":");
    if (writer->pretty) jw_putc(writer, ' ');
    jw_string(writer, path);
    jw_putc(writer, ',');
    jw_indent(writer, depth + 1);
    jw_puts(writer, "\"type\":");
    if (writer->pretty) jw_putc(writer, ' ');
    jw_printf(writer, "%d", (int)tag->type);
    jw_putc(writer, ',');
    jw_indent(writer, depth + 1);
    jw_puts(writer, "\"typeName\":");
    if (writer->pretty) jw_putc(writer, ' ');
    jw_string(writer, nbt_tag_type_name(tag->type));

    if (tag->type == TAG_Compound || tag->type == TAG_List) {
        jw_putc(writer, ',');
        jw_indent(writer, depth + 1);
        jw_puts(writer, "\"children\":");
        if (writer->pretty) jw_putc(writer, ' ');
        if (!write_children(writer, tag, path, depth + 1)) return 0;
        if (tag->type == TAG_List) {
            jw_putc(writer, ',');
            jw_indent(writer, depth + 1);
            jw_puts(writer, "\"elementType\":");
            if (writer->pretty) jw_putc(writer, ' ');
            jw_printf(writer, "%d", (int)tag->value.list.element_type);
        }
    } else {
        jw_putc(writer, ',');
        jw_indent(writer, depth + 1);
        jw_puts(writer, "\"value\":");
        if (writer->pretty) jw_putc(writer, ' ');
        switch (tag->type) {
            case TAG_End: jw_puts(writer, "null"); break;
            case TAG_Byte: jw_printf(writer, "%d", (int)tag->value.byte_val); break;
            case TAG_Short: jw_printf(writer, "%d", (int)tag->value.short_val); break;
            case TAG_Int: jw_printf(writer, "%d", tag->value.int_val); break;
            case TAG_Long: jw_printf(writer, "%lld", (long long)tag->value.long_val); break;
            case TAG_Float: write_float(writer, tag->value.float_val, 1); break;
            case TAG_Double: write_float(writer, tag->value.double_val, 0); break;
            case TAG_String: jw_string(writer, tag->value.string_val); break;
            case TAG_Byte_Array: write_byte_array(writer, &tag->value.byte_array, depth + 1); break;
            case TAG_Int_Array: write_int_array(writer, &tag->value.int_array, depth + 1); break;
            case TAG_Long_Array: write_long_array(writer, &tag->value.long_array, depth + 1); break;
            default: jw_puts(writer, "null"); break;
        }
    }

    jw_indent(writer, depth);
    jw_putc(writer, '}');
    return !writer->failed;
}

int nbt_write_typed_json(FILE* out, const NBTTag* root, int pretty, char* err, size_t err_sz) {
    JsonWriter writer;
    char* root_path;

    if (!out || !root) {
        set_err(err, err_sz, "invalid JSON export arguments");
        return 0;
    }

    writer.out = out;
    writer.pretty = pretty != 0;
    writer.failed = 0;
    root_path = (root->name && root->name[0]) ? path_segment(root->name) : duplicate_text("");
    if (!root_path) {
        set_err(err, err_sz, "out of memory");
        return 0;
    }

    jw_putc(&writer, '{');
    jw_indent(&writer, 1);
    jw_puts(&writer, "\"schema\":");
    if (writer.pretty) jw_putc(&writer, ' ');
    jw_string(&writer, "cnbt-tree-v1");
    jw_putc(&writer, ',');
    jw_indent(&writer, 1);
    jw_puts(&writer, "\"root\":");
    if (writer.pretty) jw_putc(&writer, ' ');
    if (!write_tag(&writer, root, root_path, 1)) writer.failed = 1;
    free(root_path);
    jw_indent(&writer, 0);
    jw_putc(&writer, '}');
    jw_putc(&writer, '\n');

    if (writer.failed || ferror(out)) {
        set_err(err, err_sz, "failed to write JSON output");
        return 0;
    }
    return 1;
}

int nbt_write_typed_json_file(const char* path, const NBTTag* root, int pretty, char* err, size_t err_sz) {
    FILE* out;
    int ok;

    if (!path) {
        set_err(err, err_sz, "missing JSON output path");
        return 0;
    }
    out = nbt_fopen(path, "wb");
    if (!out) {
        if (err && err_sz > 0) snprintf(err, err_sz, "fopen(%s): %s", path, strerror(errno));
        return 0;
    }
    ok = nbt_write_typed_json(out, root, pretty, err, err_sz);
    if (fclose(out) != 0 && ok) {
        set_err(err, err_sz, "failed to close JSON output");
        ok = 0;
    }
    return ok;
}
