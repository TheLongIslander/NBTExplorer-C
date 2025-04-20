#include <stdio.h>
#include "nbt_utils.h"

void print_indent(int depth) {
    for (int i = 0; i < depth; i++) printf("  ");
}

uint8_t read_u8(const unsigned char* data, size_t* offset) {
    return data[(*offset)++];
}

uint16_t read_u16(const unsigned char* data, size_t* offset) {
    uint16_t val = (data[*offset] << 8) | data[*offset + 1];
    *offset += 2;
    return val;
}

int32_t read_i32(const unsigned char* data, size_t* offset) {
    int32_t val = 0;
    for (int i = 0; i < 4; i++) val = (val << 8) | data[(*offset)++];
    return val;
}

int64_t read_i64(const unsigned char* data, size_t* offset) {
    int64_t val = 0;
    for (int i = 0; i < 8; i++) val = (val << 8) | data[(*offset)++];
    return val;
}
