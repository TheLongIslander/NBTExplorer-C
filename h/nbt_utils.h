#ifndef NBT_UTILS_H
#define NBT_UTILS_H

#include <stdint.h>
#include <stddef.h>

void print_indent(int depth);
uint8_t read_u8(const unsigned char* data, size_t* offset);
uint16_t read_u16(const unsigned char* data, size_t* offset);
int32_t read_i32(const unsigned char* data, size_t* offset);
int64_t read_i64(const unsigned char* data, size_t* offset);

#endif
