#ifndef REGION_READ_H
#define REGION_READ_H

#include <stddef.h>
#include "io.h"
#include "region_file.h"

RegionFile* region_file_read(const char* filename, char* err, size_t err_sz);

int region_file_find_first_populated_chunk(const RegionFile* region, int* out_chunk_x, int* out_chunk_z);

unsigned char* region_file_extract_chunk_nbt(
    const RegionFile* region,
    int chunk_x,
    int chunk_z,
    size_t* out_size,
    NBTInputFormat* out_format,
    char* err,
    size_t err_sz
);

#endif
