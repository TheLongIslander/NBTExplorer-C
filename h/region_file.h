#ifndef REGION_FILE_H
#define REGION_FILE_H

#include <stddef.h>
#include <stdint.h>

#define REGION_SECTOR_BYTES 4096U
#define REGION_HEADER_BYTES (REGION_SECTOR_BYTES * 2U)
#define REGION_CHUNK_GRID 32
#define REGION_CHUNK_COUNT (REGION_CHUNK_GRID * REGION_CHUNK_GRID)

#define REGION_COMPRESSION_GZIP 1
#define REGION_COMPRESSION_ZLIB 2
#define REGION_COMPRESSION_NONE 3

typedef struct {
    int present;
    uint32_t sector_offset;
    uint8_t sector_count;
    uint32_t timestamp;
    uint8_t compression_type;
    uint32_t stored_length;
    size_t payload_size;
    unsigned char* payload;
} RegionChunkSlot;

typedef struct {
    size_t file_size;
    uint32_t total_sectors;
    uint8_t* sector_used;
    RegionChunkSlot chunks[REGION_CHUNK_COUNT];
} RegionFile;

int region_chunk_index(int chunk_x, int chunk_z);
void region_chunk_coords(int index, int* out_chunk_x, int* out_chunk_z);

RegionFile* region_file_create(void);
void region_file_free(RegionFile* region);

const RegionChunkSlot* region_file_get_chunk(const RegionFile* region, int chunk_x, int chunk_z);
RegionChunkSlot* region_file_get_chunk_mut(RegionFile* region, int chunk_x, int chunk_z);

#endif
