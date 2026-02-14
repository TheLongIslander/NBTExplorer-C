#ifndef IO_H
#define IO_H

#include <stddef.h>

typedef enum {
    NBT_INPUT_FORMAT_UNKNOWN = 0,
    NBT_INPUT_FORMAT_GZIP,
    NBT_INPUT_FORMAT_ZLIB,
    NBT_INPUT_FORMAT_RAW
} NBTInputFormat;

unsigned char* load_nbt_data_auto(const char* filename, size_t* out_size, NBTInputFormat* out_format, char* err, size_t err_sz);
const char* nbt_input_format_name(NBTInputFormat fmt);

#endif
