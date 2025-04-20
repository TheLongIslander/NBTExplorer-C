#ifndef NBT_H
#define NBT_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    TAG_End = 0,
    TAG_Byte = 1,
    TAG_Short = 2,
    TAG_Int = 3,
    TAG_Long = 4,
    TAG_Float = 5,
    TAG_Double = 6,
    TAG_Byte_Array = 7,
    TAG_String = 8,
    TAG_List = 9,
    TAG_Compound = 10,
    TAG_Int_Array = 11,
    TAG_Long_Array = 12
} TagType;

void parse_nbt(const unsigned char* data, size_t* offset, int indent);

uint8_t read_u8(const unsigned char* data, size_t* offset);
uint16_t read_u16(const unsigned char* data, size_t* offset);
int32_t read_i32(const unsigned char* data, size_t* offset);
int64_t read_i64(const unsigned char* data, size_t* offset);

#endif
