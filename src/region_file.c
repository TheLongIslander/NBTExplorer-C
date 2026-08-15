#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "region_file.h"

static int ascii_equal_ci(char a, char b) {
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

static const char* path_basename(const char* path) {
    const char* slash;
    const char* backslash;

    if (!path) return NULL;
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
    return slash ? slash + 1 : path;
}

static int starts_region_integer(const char* text) {
    if (!text) return 0;
    if (*text == '-') text++;
    return *text >= '0' && *text <= '9';
}

static int parse_int_component(const char** cursor, long* out_value) {
    char* end = NULL;
    long value;

    if (!cursor || !*cursor || !out_value || !starts_region_integer(*cursor)) return 0;
    errno = 0;
    value = strtol(*cursor, &end, 10);
    if (errno == ERANGE || end == *cursor || value < INT_MIN || value > INT_MAX) return 0;
    *cursor = end;
    *out_value = value;
    return 1;
}

int region_path_has_extension(const char* filename) {
    const char* dot;

    if (!filename) return 0;
    dot = strrchr(filename, '.');
    if (!dot || strlen(dot) != 4U) return 0;
    return ascii_equal_ci(dot[1], 'm') &&
           ascii_equal_ci(dot[2], 'c') &&
           (ascii_equal_ci(dot[3], 'a') || ascii_equal_ci(dot[3], 'r'));
}

int region_path_parse_coords(const char* filename, int* out_region_x, int* out_region_z) {
    const char* base;
    const char* cursor;
    char* end = NULL;
    long region_x;
    long region_z;

    if (out_region_x) *out_region_x = 0;
    if (out_region_z) *out_region_z = 0;
    if (!filename || !region_path_has_extension(filename)) return 0;

    base = path_basename(filename);
    if (!base || !ascii_equal_ci(base[0], 'r') || base[1] != '.') return 0;

    cursor = base + 2;
    if (!starts_region_integer(cursor)) return 0;
    errno = 0;
    region_x = strtol(cursor, &end, 10);
    if (errno == ERANGE || end == cursor || *end != '.' || region_x < INT_MIN || region_x > INT_MAX) {
        return 0;
    }

    cursor = end + 1;
    if (!starts_region_integer(cursor)) return 0;
    errno = 0;
    region_z = strtol(cursor, &end, 10);
    if (errno == ERANGE || end == cursor || *end != '.' || region_z < INT_MIN || region_z > INT_MAX) {
        return 0;
    }

    if (strlen(end) != 4U || !region_path_has_extension(end)) return 0;
    if (out_region_x) *out_region_x = (int)region_x;
    if (out_region_z) *out_region_z = (int)region_z;
    return 1;
}

int region_path_parse_cubic_r2_coords(
    const char* filename,
    int* out_region_x,
    int* out_cube_y,
    int* out_region_z
) {
    const char* base;
    const char* cursor;
    long region_x;
    long cube_y;
    long region_z;

    if (out_region_x) *out_region_x = 0;
    if (out_cube_y) *out_cube_y = 0;
    if (out_region_z) *out_region_z = 0;
    if (!filename || !region_path_has_extension(filename)) return 0;

    base = path_basename(filename);
    if (!base || !ascii_equal_ci(base[0], 'r') || base[1] != '2' || base[2] != '.') return 0;

    cursor = base + 3;
    if (!parse_int_component(&cursor, &region_x) || *cursor != '.') return 0;
    cursor++;
    if (!parse_int_component(&cursor, &cube_y) || *cursor != '.') return 0;
    cursor++;
    if (!parse_int_component(&cursor, &region_z)) return 0;

    if (strlen(cursor) != 4U || !region_path_has_extension(cursor)) return 0;
    if (out_region_x) *out_region_x = (int)region_x;
    if (out_cube_y) *out_cube_y = (int)cube_y;
    if (out_region_z) *out_region_z = (int)region_z;
    return 1;
}

int region_path_is_cubic_r2(const char* filename) {
    return region_path_parse_cubic_r2_coords(filename, NULL, NULL, NULL);
}

char* region_external_chunk_path(const char* region_path, int chunk_x, int chunk_z) {
    const char* base;
    size_t dir_len;
    size_t capacity;
    int region_x;
    int region_z;
    long long global_x;
    long long global_z;
    char* out;
    int written;

    if (!region_path || region_chunk_index(chunk_x, chunk_z) < 0) return NULL;
    if (!region_path_parse_coords(region_path, &region_x, &region_z)) return NULL;

    global_x = (long long)region_x * REGION_CHUNK_GRID + chunk_x;
    global_z = (long long)region_z * REGION_CHUNK_GRID + chunk_z;
    if (global_x < INT_MIN || global_x > INT_MAX || global_z < INT_MIN || global_z > INT_MAX) {
        return NULL;
    }

    base = path_basename(region_path);
    if (!base) return NULL;
    dir_len = (size_t)(base - region_path);

    /* Two signed 32-bit values, punctuation, extension, and terminator. */
    if (dir_len > SIZE_MAX - 40U) return NULL;
    capacity = dir_len + 40U;
    out = malloc(capacity);
    if (!out) return NULL;

    if (dir_len > 0) memcpy(out, region_path, dir_len);
    written = snprintf(out + dir_len, capacity - dir_len, "c.%d.%d.mcc", (int)global_x, (int)global_z);
    if (written < 0 || (size_t)written >= capacity - dir_len) {
        free(out);
        return NULL;
    }

    return out;
}

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
    if (region) region->layout = REGION_LAYOUT_STANDARD;
    return region;
}

uint32_t region_file_sector_bytes(const RegionFile* region) {
    if (region && region->layout == REGION_LAYOUT_CUBIC_R2) {
        return REGION_CUBIC_R2_SECTOR_BYTES;
    }
    return REGION_SECTOR_BYTES;
}

uint32_t region_file_header_sectors(const RegionFile* region) {
    uint32_t sector_bytes = region_file_sector_bytes(region);
    return REGION_HEADER_BYTES / sector_bytes;
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
