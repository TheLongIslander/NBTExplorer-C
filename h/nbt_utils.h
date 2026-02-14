#ifndef NBT_UTILS_H
#define NBT_UTILS_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    const unsigned char* data;
    size_t size;
    size_t offset;
    int failed;
    char error[128];
} NBTReader;

void print_indent(int depth);
void nbt_reader_init(NBTReader* reader, const unsigned char* data, size_t size);
int nbt_reader_set_offset(NBTReader* reader, size_t offset);
size_t nbt_reader_offset(const NBTReader* reader);
const char* nbt_reader_error(const NBTReader* reader);
int nbt_reader_failed(const NBTReader* reader);

int nbt_read_u8(NBTReader* reader, uint8_t* out);
int nbt_peek_u8(NBTReader* reader, uint8_t* out);
int nbt_read_u16(NBTReader* reader, uint16_t* out);
int nbt_read_i32(NBTReader* reader, int32_t* out);
int nbt_read_i64(NBTReader* reader, int64_t* out);
int nbt_read_bytes(NBTReader* reader, unsigned char* out, size_t len);
int nbt_skip_bytes(NBTReader* reader, size_t len);

#endif
