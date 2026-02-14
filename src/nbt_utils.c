#include <stdio.h>
#include <string.h>
#include "nbt_utils.h"

void print_indent(int depth) {
    for (int i = 0; i < depth; i++) printf("  ");
}

static int fail_reader(NBTReader* reader, const char* msg) {
    if (!reader) return 0;
    if (!reader->failed) {
        snprintf(reader->error, sizeof(reader->error), "%s at byte offset %zu", msg, reader->offset);
    }
    reader->failed = 1;
    return 0;
}

void nbt_reader_init(NBTReader* reader, const unsigned char* data, size_t size) {
    if (!reader) return;
    reader->data = data;
    reader->size = size;
    reader->offset = 0;
    reader->failed = 0;
    reader->error[0] = '\0';
}

int nbt_reader_set_offset(NBTReader* reader, size_t offset) {
    if (!reader) return 0;
    if (offset > reader->size) return fail_reader(reader, "offset is out of bounds");
    reader->offset = offset;
    return 1;
}

size_t nbt_reader_offset(const NBTReader* reader) {
    return reader ? reader->offset : 0;
}

const char* nbt_reader_error(const NBTReader* reader) {
    if (!reader) return "invalid NBT reader";
    if (reader->error[0] == '\0') return "unknown reader error";
    return reader->error;
}

int nbt_reader_failed(const NBTReader* reader) {
    return reader ? reader->failed : 1;
}

int nbt_read_u8(NBTReader* reader, uint8_t* out) {
    if (!reader || !out) return 0;
    if (reader->failed) return 0;
    if (reader->offset + 1 > reader->size) return fail_reader(reader, "unexpected end of input while reading u8");
    *out = reader->data[reader->offset++];
    return 1;
}

int nbt_peek_u8(NBTReader* reader, uint8_t* out) {
    if (!reader || !out) return 0;
    if (reader->failed) return 0;
    if (reader->offset + 1 > reader->size) return fail_reader(reader, "unexpected end of input while peeking u8");
    *out = reader->data[reader->offset];
    return 1;
}

int nbt_read_u16(NBTReader* reader, uint16_t* out) {
    if (!reader || !out) return 0;
    if (reader->failed) return 0;
    if (reader->offset + 2 > reader->size) return fail_reader(reader, "unexpected end of input while reading u16");
    *out = (uint16_t)((reader->data[reader->offset] << 8) | reader->data[reader->offset + 1]);
    reader->offset += 2;
    return 1;
}

int nbt_read_i32(NBTReader* reader, int32_t* out) {
    int32_t val = 0;
    if (!reader || !out) return 0;
    if (reader->failed) return 0;
    if (reader->offset + 4 > reader->size) return fail_reader(reader, "unexpected end of input while reading i32");
    for (int i = 0; i < 4; i++) {
        val = (val << 8) | reader->data[reader->offset++];
    }
    *out = val;
    return 1;
}

int nbt_read_i64(NBTReader* reader, int64_t* out) {
    int64_t val = 0;
    if (!reader || !out) return 0;
    if (reader->failed) return 0;
    if (reader->offset + 8 > reader->size) return fail_reader(reader, "unexpected end of input while reading i64");
    for (int i = 0; i < 8; i++) {
        val = (val << 8) | reader->data[reader->offset++];
    }
    *out = val;
    return 1;
}

int nbt_read_bytes(NBTReader* reader, unsigned char* out, size_t len) {
    if (!reader) return 0;
    if (reader->failed) return 0;
    if (len > reader->size - reader->offset) return fail_reader(reader, "unexpected end of input while reading byte block");
    if (out && len > 0) {
        memcpy(out, reader->data + reader->offset, len);
    }
    reader->offset += len;
    return 1;
}

int nbt_skip_bytes(NBTReader* reader, size_t len) {
    return nbt_read_bytes(reader, NULL, len);
}
