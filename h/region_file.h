#ifndef REGION_FILE_H
#define REGION_FILE_H

#include <stddef.h>
#include <stdint.h>

#define REGION_SECTOR_BYTES 4096U
#define REGION_CUBIC_R2_SECTOR_BYTES 256U
/* The two 1024-entry tables remain 4096 bytes each in both layouts. */
#define REGION_LOCATION_TABLE_BYTES 4096U
#define REGION_TIMESTAMP_TABLE_BYTES 4096U
#define REGION_HEADER_BYTES (REGION_LOCATION_TABLE_BYTES + REGION_TIMESTAMP_TABLE_BYTES)
#define REGION_CHUNK_GRID 32
#define REGION_CHUNK_COUNT (REGION_CHUNK_GRID * REGION_CHUNK_GRID)

#define REGION_COMPRESSION_GZIP 1
#define REGION_COMPRESSION_ZLIB 2
#define REGION_COMPRESSION_NONE 3
#define REGION_COMPRESSION_LZ4 4
#define REGION_EXTERNAL_STREAM_FLAG 0x80U

typedef enum {
    REGION_LAYOUT_STANDARD = 0,
    REGION_LAYOUT_CUBIC_R2 = 1
} RegionLayout;

typedef struct {
    int present;
    uint32_t sector_offset;
    uint8_t sector_count;
    uint32_t timestamp;
    uint8_t compression_type;
    int external;
    uint32_t stored_length;
    size_t payload_size;
    unsigned char* payload;
} RegionChunkSlot;

typedef struct {
    RegionLayout layout;
    size_t file_size;
    uint32_t total_sectors;
    uint8_t* sector_used;
    RegionChunkSlot chunks[REGION_CHUNK_COUNT];
} RegionFile;

int region_chunk_index(int chunk_x, int chunk_z);
void region_chunk_coords(int index, int* out_chunk_x, int* out_chunk_z);

/* Recognizes standard and legacy cubic-r2 .mca/.mcr filename conventions. */
int region_path_has_extension(const char* filename);
int region_path_parse_coords(const char* filename, int* out_region_x, int* out_region_z);
int region_path_is_cubic_r2(const char* filename);
int region_path_parse_cubic_r2_coords(
    const char* filename,
    int* out_region_x,
    int* out_cube_y,
    int* out_region_z
);
char* region_external_chunk_path(const char* region_path, int chunk_x, int chunk_z);

uint32_t region_file_sector_bytes(const RegionFile* region);
uint32_t region_file_header_sectors(const RegionFile* region);

RegionFile* region_file_create(void);
void region_file_free(RegionFile* region);

const RegionChunkSlot* region_file_get_chunk(const RegionFile* region, int chunk_x, int chunk_z);
RegionChunkSlot* region_file_get_chunk_mut(RegionFile* region, int chunk_x, int chunk_z);

#endif
