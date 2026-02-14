#ifndef IO_H
#define IO_H

#include <stddef.h>

typedef enum {
    NBT_INPUT_FORMAT_UNKNOWN = 0,
    NBT_INPUT_FORMAT_GZIP,
    NBT_INPUT_FORMAT_ZLIB,
    NBT_INPUT_FORMAT_RAW
} NBTInputFormat;

typedef enum {
    NBT_SOURCE_STANDALONE = 0,
    NBT_SOURCE_REGION_CHUNK
} NBTSourceType;

typedef struct {
    int has_chunk_coords;
    int chunk_x;
    int chunk_z;
} NBTLoadOptions;

typedef struct {
    NBTInputFormat input_format;
    NBTSourceType source_type;
    int chunk_x;
    int chunk_z;
} NBTLoadInfo;

unsigned char* load_nbt_data(const char* filename, size_t* out_size, const NBTLoadOptions* opts, NBTLoadInfo* out_info, char* err, size_t err_sz);
unsigned char* load_nbt_data_auto(const char* filename, size_t* out_size, NBTInputFormat* out_format, char* err, size_t err_sz);
const char* nbt_input_format_name(NBTInputFormat fmt);
const char* nbt_source_type_name(NBTSourceType source_type);

#endif
