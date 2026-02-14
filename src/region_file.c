#include <stdlib.h>
#include <string.h>
#include "region_file.h"

int region_chunk_index(int chunk_x, int chunk_z) {
    if (chunk_x < 0 || chunk_x >= REGION_CHUNK_GRID) return -1;
    if (chunk_z < 0 || chunk_z >= REGION_CHUNK_GRID) return -1;
    return chunk_z * REGION_CHUNK_GRID + chunk_x;
}

void region_chunk_coords(int index, int* out_chunk_x, int* out_chunk_z) {
    if (out_chunk_x) *out_chunk_x = -1;
    if (out_chunk_z) *out_chunk_z = -1;
    if (index < 0 || index >= REGION_CHUNK_COUNT) return;
    if (out_chunk_x) *out_chunk_x = index % REGION_CHUNK_GRID;
    if (out_chunk_z) *out_chunk_z = index / REGION_CHUNK_GRID;
}

RegionFile* region_file_create(void) {
    RegionFile* region = calloc(1, sizeof(RegionFile));
    return region;
}

void region_file_free(RegionFile* region) {
    int i;

    if (!region) return;

    for (i = 0; i < REGION_CHUNK_COUNT; i++) {
        free(region->chunks[i].payload);
    }

    free(region->sector_used);
    free(region);
}

const RegionChunkSlot* region_file_get_chunk(const RegionFile* region, int chunk_x, int chunk_z) {
    int idx;
    if (!region) return NULL;
    idx = region_chunk_index(chunk_x, chunk_z);
    if (idx < 0) return NULL;
    return &region->chunks[idx];
}

RegionChunkSlot* region_file_get_chunk_mut(RegionFile* region, int chunk_x, int chunk_z) {
    int idx;
    if (!region) return NULL;
    idx = region_chunk_index(chunk_x, chunk_z);
    if (idx < 0) return NULL;
    return &region->chunks[idx];
}
