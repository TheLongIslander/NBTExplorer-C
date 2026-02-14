#ifndef NBT_PARSER_H
#define NBT_PARSER_H

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

struct NBTTag;

typedef struct {
    int count;
    struct NBTTag** items;
} Compound;

typedef struct {
    int count;
    TagType element_type;
    struct NBTTag** items;
} List;

typedef struct {
    int32_t length;
    uint8_t* data;
} ByteArray;

typedef struct {
    int32_t length;
    int32_t* data;
} IntArray;

typedef struct {
    int32_t length;
    int64_t* data;
} LongArray;

typedef union {
    int8_t byte_val;
    int16_t short_val;
    int32_t int_val;
    int64_t long_val;
    float float_val;
    double double_val;
    char* string_val;
    ByteArray byte_array;
    IntArray int_array;
    LongArray long_array;
    Compound compound;
    List list;
} TagValue;

typedef struct NBTTag {
    TagType type;
    char* name;
    TagValue value;
    int array_length;
} NBTTag;

void parse_nbt(const NBTTag* tag, int indent);

#endif
