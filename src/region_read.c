#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "region_read.h"

#define READ_CHUNK 16384U

static void set_err(char* err, size_t err_sz, const char* msg) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", msg);
    }
}

static uint32_t read_be_u32(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static unsigned char* read_file_bytes(const char* filename, size_t* out_size, char* err, size_t err_sz) {
    FILE* file = NULL;
    unsigned char* buffer = NULL;
    size_t size = 0;
    size_t capacity = 0;

    if (out_size) *out_size = 0;

    file = fopen(filename, "rb");
    if (!file) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "fopen(%s) failed: %s", filename, strerror(errno));
        }
        return NULL;
    }

    capacity = READ_CHUNK;
    buffer = malloc(capacity);
    if (!buffer) {
        fclose(file);
        set_err(err, err_sz, "out of memory");
        return NULL;
    }

    while (1) {
        size_t read_count;

        if (size > SIZE_MAX - READ_CHUNK) {
            set_err(err, err_sz, "input file too large");
            free(buffer);
            fclose(file);
            return NULL;
        }

        if (size + READ_CHUNK > capacity) {
            size_t new_capacity = size + READ_CHUNK;
            unsigned char* grown = realloc(buffer, new_capacity);
            if (!grown) {
                set_err(err, err_sz, "out of memory");
                free(buffer);
                fclose(file);
                return NULL;
            }
            buffer = grown;
            capacity = new_capacity;
        }

        read_count = fread(buffer + size, 1, READ_CHUNK, file);
        if (read_count == 0) {
            if (ferror(file)) {
                set_err(err, err_sz, "failed to read input file");
                free(buffer);
                fclose(file);
                return NULL;
            }
            break;
        }

        size += read_count;
    }

    fclose(file);
    if (out_size) *out_size = size;
    return buffer;
}

static unsigned char* copy_bytes(const unsigned char* data, size_t size) {
    unsigned char* out;
    if (!data && size > 0) return NULL;
    out = malloc(size == 0 ? 1 : size);
    if (!out) return NULL;
    if (size > 0) {
        memcpy(out, data, size);
    }
    return out;
}

static unsigned char* inflate_buffer(const unsigned char* input, size_t input_size, int window_bits, size_t* out_size) {
    z_stream zs;
    unsigned char* out = NULL;
    size_t capacity;
    size_t produced = 0;
    int ret;

    if (!input || !out_size) return NULL;
    if (input_size > (size_t)UINT_MAX) return NULL;

    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, window_bits) != Z_OK) {
        return NULL;
    }

    capacity = READ_CHUNK;
    out = malloc(capacity);
    if (!out) {
        inflateEnd(&zs);
        return NULL;
    }

    zs.next_in = (Bytef*)input;
    zs.avail_in = (uInt)input_size;

    while (1) {
        uInt avail_out;
        size_t written;

        if (produced == capacity) {
            size_t new_capacity;
            unsigned char* grown;
            if (capacity > SIZE_MAX - READ_CHUNK) {
                free(out);
                inflateEnd(&zs);
                return NULL;
            }
            new_capacity = capacity + READ_CHUNK;
            grown = realloc(out, new_capacity);
            if (!grown) {
                free(out);
                inflateEnd(&zs);
                return NULL;
            }
            out = grown;
            capacity = new_capacity;
        }

        avail_out = (uInt)(((capacity - produced) > (size_t)UINT_MAX) ? (size_t)UINT_MAX : (capacity - produced));
        zs.next_out = out + produced;
        zs.avail_out = avail_out;

        ret = inflate(&zs, Z_NO_FLUSH);
        written = (size_t)(avail_out - zs.avail_out);
        produced += written;

        if (ret == Z_STREAM_END) {
            break;
        }
        if (ret == Z_OK) {
            continue;
        }
        if (ret == Z_BUF_ERROR && zs.avail_out == 0) {
            continue;
        }

        free(out);
        inflateEnd(&zs);
        return NULL;
    }

    inflateEnd(&zs);
    *out_size = produced;
    return out;
}

static int mark_sector_usage(RegionFile* region, uint32_t start_sector, uint32_t sector_count, char* err, size_t err_sz) {
    uint32_t s;

    if (!region || !region->sector_used) {
        set_err(err, err_sz, "invalid region sector map");
        return 0;
    }

    if (start_sector >= region->total_sectors || sector_count > region->total_sectors - start_sector) {
        set_err(err, err_sz, "corrupt .mca: sector range out of bounds");
        return 0;
    }

    for (s = 0; s < sector_count; s++) {
        uint32_t idx = start_sector + s;
        if (region->sector_used[idx]) {
            set_err(err, err_sz, "corrupt .mca: overlapping chunk sector allocation");
            return 0;
        }
        region->sector_used[idx] = 1;
    }

    return 1;
}

RegionFile* region_file_read(const char* filename, char* err, size_t err_sz) {
    unsigned char* file_data = NULL;
    size_t file_size = 0;
    RegionFile* region = NULL;
    int i;

    if (!filename) {
        set_err(err, err_sz, "missing filename");
        return NULL;
    }

    file_data = read_file_bytes(filename, &file_size, err, err_sz);
    if (!file_data) {
        return NULL;
    }

    if (file_size < REGION_HEADER_BYTES) {
        set_err(err, err_sz, "invalid .mca: expected at least 8192 bytes");
        free(file_data);
        return NULL;
    }

    region = region_file_create();
    if (!region) {
        set_err(err, err_sz, "out of memory");
        free(file_data);
        return NULL;
    }

    region->file_size = file_size;
    region->total_sectors = (uint32_t)((file_size + (REGION_SECTOR_BYTES - 1U)) / REGION_SECTOR_BYTES);
    if (region->total_sectors < 2U) {
        set_err(err, err_sz, "invalid .mca: missing header sectors");
        free(file_data);
        region_file_free(region);
        return NULL;
    }

    region->sector_used = calloc(region->total_sectors, sizeof(uint8_t));
    if (!region->sector_used) {
        set_err(err, err_sz, "out of memory");
        free(file_data);
        region_file_free(region);
        return NULL;
    }

    region->sector_used[0] = 1;
    region->sector_used[1] = 1;

    for (i = 0; i < REGION_CHUNK_COUNT; i++) {
        RegionChunkSlot* slot = &region->chunks[i];
        uint32_t location_entry = read_be_u32(file_data + (size_t)i * 4U);
        uint32_t timestamp = read_be_u32(file_data + REGION_SECTOR_BYTES + (size_t)i * 4U);
        uint32_t sector_offset = (location_entry >> 8) & 0x00FFFFFFU;
        uint32_t sector_count = location_entry & 0x000000FFU;

        slot->timestamp = timestamp;

        if (sector_offset == 0 && sector_count == 0) {
            continue;
        }

        if (sector_offset == 0 || sector_count == 0) {
            set_err(err, err_sz, "corrupt .mca: invalid zero location/count combination");
            free(file_data);
            region_file_free(region);
            return NULL;
        }

        if (sector_offset < 2U) {
            set_err(err, err_sz, "corrupt .mca: chunk points into header sectors");
            free(file_data);
            region_file_free(region);
            return NULL;
        }

        if (!mark_sector_usage(region, sector_offset, sector_count, err, err_sz)) {
            free(file_data);
            region_file_free(region);
            return NULL;
        }

        {
            size_t chunk_start = (size_t)sector_offset * REGION_SECTOR_BYTES;
            size_t chunk_span = (size_t)sector_count * REGION_SECTOR_BYTES;
            uint32_t length_field;
            uint8_t compression_type;
            size_t payload_size;

            if (chunk_start > file_size || chunk_span > file_size - chunk_start) {
                set_err(err, err_sz, "corrupt .mca: chunk data points outside file");
                free(file_data);
                region_file_free(region);
                return NULL;
            }

            if (chunk_span < 5U) {
                set_err(err, err_sz, "corrupt .mca: chunk data block too small");
                free(file_data);
                region_file_free(region);
                return NULL;
            }

            length_field = read_be_u32(file_data + chunk_start);
            if (length_field < 1U) {
                set_err(err, err_sz, "corrupt .mca: invalid chunk length field");
                free(file_data);
                region_file_free(region);
                return NULL;
            }

            if ((size_t)length_field + 4U > chunk_span) {
                set_err(err, err_sz, "corrupt .mca: chunk length exceeds allocated sectors");
                free(file_data);
                region_file_free(region);
                return NULL;
            }

            compression_type = file_data[chunk_start + 4U];
            if (compression_type != REGION_COMPRESSION_GZIP &&
                compression_type != REGION_COMPRESSION_ZLIB &&
                compression_type != REGION_COMPRESSION_NONE) {
                set_err(err, err_sz, "corrupt .mca: unsupported chunk compression type");
                free(file_data);
                region_file_free(region);
                return NULL;
            }

            payload_size = (size_t)length_field - 1U;
            if (payload_size > chunk_span - 5U) {
                set_err(err, err_sz, "corrupt .mca: invalid chunk payload size");
                free(file_data);
                region_file_free(region);
                return NULL;
            }

            slot->payload = copy_bytes(file_data + chunk_start + 5U, payload_size);
            if (!slot->payload && payload_size > 0) {
                set_err(err, err_sz, "out of memory");
                free(file_data);
                region_file_free(region);
                return NULL;
            }

            slot->present = 1;
            slot->sector_offset = sector_offset;
            slot->sector_count = (uint8_t)sector_count;
            slot->compression_type = compression_type;
            slot->stored_length = length_field;
            slot->payload_size = payload_size;
        }
    }

    free(file_data);
    return region;
}

int region_file_find_first_populated_chunk(const RegionFile* region, int* out_chunk_x, int* out_chunk_z) {
    int i;

    if (out_chunk_x) *out_chunk_x = -1;
    if (out_chunk_z) *out_chunk_z = -1;
    if (!region) return 0;

    for (i = 0; i < REGION_CHUNK_COUNT; i++) {
        if (region->chunks[i].present) {
            region_chunk_coords(i, out_chunk_x, out_chunk_z);
            return 1;
        }
    }

    return 0;
}

unsigned char* region_file_extract_chunk_nbt(
    const RegionFile* region,
    int chunk_x,
    int chunk_z,
    size_t* out_size,
    NBTInputFormat* out_format,
    char* err,
    size_t err_sz
) {
    const RegionChunkSlot* slot;
    unsigned char* decoded = NULL;
    size_t decoded_size = 0;

    if (out_size) *out_size = 0;
    if (out_format) *out_format = NBT_INPUT_FORMAT_UNKNOWN;

    if (!region) {
        set_err(err, err_sz, "missing region data");
        return NULL;
    }

    slot = region_file_get_chunk(region, chunk_x, chunk_z);
    if (!slot) {
        set_err(err, err_sz, "chunk coordinates must be within 0..31");
        return NULL;
    }
    if (!slot->present) {
        set_err(err, err_sz, "requested chunk is empty in this region");
        return NULL;
    }

    switch (slot->compression_type) {
        case REGION_COMPRESSION_GZIP:
            decoded = inflate_buffer(slot->payload, slot->payload_size, 16 + MAX_WBITS, &decoded_size);
            if (!decoded) {
                set_err(err, err_sz, "failed to decompress gzip .mca chunk payload");
                return NULL;
            }
            if (out_format) *out_format = NBT_INPUT_FORMAT_GZIP;
            break;
        case REGION_COMPRESSION_ZLIB:
            decoded = inflate_buffer(slot->payload, slot->payload_size, MAX_WBITS, &decoded_size);
            if (!decoded) {
                set_err(err, err_sz, "failed to decompress zlib .mca chunk payload");
                return NULL;
            }
            if (out_format) *out_format = NBT_INPUT_FORMAT_ZLIB;
            break;
        case REGION_COMPRESSION_NONE:
            decoded = copy_bytes(slot->payload, slot->payload_size);
            if (!decoded && slot->payload_size > 0) {
                set_err(err, err_sz, "out of memory");
                return NULL;
            }
            decoded_size = slot->payload_size;
            if (out_format) *out_format = NBT_INPUT_FORMAT_RAW;
            break;
        default:
            set_err(err, err_sz, "unsupported .mca chunk compression type");
            return NULL;
    }

    if (out_size) *out_size = decoded_size;
    return decoded;
}
