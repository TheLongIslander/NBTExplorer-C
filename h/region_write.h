#ifndef REGION_WRITE_H
#define REGION_WRITE_H

#include <stddef.h>
#include "nbt_parser.h"
#include "region_file.h"

/* compression_override: -1 preserve existing (or default zlib), otherwise 1/2/3 */
int region_file_update_chunk_from_nbt(
    RegionFile* region,
    int chunk_x,
    int chunk_z,
    const NBTTag* root,
    int compression_override,
    char* err,
    size_t err_sz
);

int region_file_write(const RegionFile* region, const char* output_path, char* err, size_t err_sz);
int region_file_write_atomic(const RegionFile* region, const char* output_path, char* err, size_t err_sz);

#endif
