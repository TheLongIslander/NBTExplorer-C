#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>
#include "edit_save.h"
#include "platform.h"
#include "region_lz4.h"
#include "region_write.h"

#define WRITE_CHUNK 16384U

static void set_err(char* err, size_t err_sz, const char* msg) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", msg);
    }
}

static void write_be_u32(unsigned char* p, uint32_t v) {
    p[0] = (unsigned char)((v >> 24) & 0xFF);
    p[1] = (unsigned char)((v >> 16) & 0xFF);
    p[2] = (unsigned char)((v >> 8) & 0xFF);
    p[3] = (unsigned char)(v & 0xFF);
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

static unsigned char* deflate_buffer(const unsigned char* input, size_t input_size, int window_bits, size_t* out_size) {
    z_stream zs;
    unsigned char* out = NULL;
    size_t capacity;
    size_t produced = 0;
    int ret;

    if (!input || !out_size) return NULL;
    if (input_size > (size_t)UINT_MAX) return NULL;

    memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, window_bits, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return NULL;
    }

    capacity = WRITE_CHUNK;
    out = malloc(capacity);
    if (!out) {
        deflateEnd(&zs);
        return NULL;
    }

    zs.next_in = (Bytef*)input;
    zs.avail_in = (uInt)input_size;

    while (1) {
        uInt avail_out;
        size_t written;
        int flush = (zs.avail_in == 0) ? Z_FINISH : Z_NO_FLUSH;

        if (produced == capacity) {
            size_t new_capacity;
            unsigned char* grown;
            if (capacity > SIZE_MAX - WRITE_CHUNK) {
                free(out);
                deflateEnd(&zs);
                return NULL;
            }
            new_capacity = capacity + WRITE_CHUNK;
            grown = realloc(out, new_capacity);
            if (!grown) {
                free(out);
                deflateEnd(&zs);
                return NULL;
            }
            out = grown;
            capacity = new_capacity;
        }

        avail_out = (uInt)(((capacity - produced) > (size_t)UINT_MAX) ? (size_t)UINT_MAX : (capacity - produced));
        zs.next_out = out + produced;
        zs.avail_out = avail_out;

        ret = deflate(&zs, flush);
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
        deflateEnd(&zs);
        return NULL;
    }

    deflateEnd(&zs);
    *out_size = produced;
    return out;
}

static unsigned char* compress_nbt_payload(const unsigned char* raw, size_t raw_size, uint8_t compression_type, size_t* out_size, char* err, size_t err_sz) {
    unsigned char* out;

    if (!raw) {
        set_err(err, err_sz, "missing raw NBT payload");
        return NULL;
    }

    switch (compression_type) {
        case REGION_COMPRESSION_GZIP:
            out = deflate_buffer(raw, raw_size, 16 + MAX_WBITS, out_size);
            if (!out) {
                set_err(err, err_sz, "failed to gzip-compress NBT payload");
                return NULL;
            }
            return out;
        case REGION_COMPRESSION_ZLIB:
            out = deflate_buffer(raw, raw_size, MAX_WBITS, out_size);
            if (!out) {
                set_err(err, err_sz, "failed to zlib-compress NBT payload");
                return NULL;
            }
            return out;
        case REGION_COMPRESSION_NONE:
            out = copy_bytes(raw, raw_size);
            if (!out && raw_size > 0) {
                set_err(err, err_sz, "out of memory");
                return NULL;
            }
            if (out_size) *out_size = raw_size;
            return out;
        case REGION_COMPRESSION_LZ4:
            return region_lz4_encode(raw, raw_size, out_size, err, err_sz);
        default:
            set_err(err, err_sz, "unsupported region compression type");
            return NULL;
    }
}

static uint8_t pick_compression(const RegionChunkSlot* slot, int compression_override) {
    if (compression_override == -1) {
        if (slot && slot->present &&
            (slot->compression_type == REGION_COMPRESSION_GZIP ||
             slot->compression_type == REGION_COMPRESSION_ZLIB ||
             slot->compression_type == REGION_COMPRESSION_NONE ||
             slot->compression_type == REGION_COMPRESSION_LZ4)) {
            return slot->compression_type;
        }
        return REGION_COMPRESSION_ZLIB;
    }

    if (compression_override == REGION_COMPRESSION_GZIP ||
        compression_override == REGION_COMPRESSION_ZLIB ||
        compression_override == REGION_COMPRESSION_NONE ||
        compression_override == REGION_COMPRESSION_LZ4) {
        return (uint8_t)compression_override;
    }

    return 0;
}

static uint32_t unix_time_now_u32(void) {
    time_t now = time(NULL);
    if (now < 0) return 0;
    if ((unsigned long long)now > 0xFFFFFFFFULL) return 0xFFFFFFFFU;
    return (uint32_t)now;
}

int region_file_update_chunk_from_nbt(
    RegionFile* region,
    int chunk_x,
    int chunk_z,
    const NBTTag* root,
    int compression_override,
    char* err,
    size_t err_sz
) {
    RegionChunkSlot* slot;
    unsigned char* raw = NULL;
    unsigned char* compressed = NULL;
    size_t raw_size = 0;
    size_t compressed_size = 0;
    uint8_t compression_type;

    if (!region || !root) {
        set_err(err, err_sz, "invalid region update arguments");
        return 0;
    }

    slot = region_file_get_chunk_mut(region, chunk_x, chunk_z);
    if (!slot) {
        set_err(err, err_sz, "chunk coordinates must be within 0..31");
        return 0;
    }
    if (!slot->present) {
        set_err(err, err_sz, "target chunk does not exist in region");
        return 0;
    }

    compression_type = pick_compression(slot, compression_override);
    if (compression_type == 0) {
        set_err(err, err_sz, "invalid compression override");
        return 0;
    }

    if (!serialize_tag_to_nbt_bytes(root, &raw, &raw_size, err, err_sz)) {
        return 0;
    }

    compressed = compress_nbt_payload(raw, raw_size, compression_type, &compressed_size, err, err_sz);
    free(raw);
    if (!compressed) {
        return 0;
    }

    if (region->layout == REGION_LAYOUT_CUBIC_R2 &&
        (compressed_size > (size_t)UINT32_MAX - 1U ||
         compressed_size > (size_t)255U * region_file_sector_bytes(region) - 5U)) {
        free(compressed);
        set_err(err, err_sz, "cube payload is too large for the legacy cubic r2 region format");
        return 0;
    }

    free(slot->payload);
    slot->payload = compressed;
    slot->payload_size = compressed_size;
    slot->compression_type = compression_type;
    if (compressed_size > (size_t)UINT32_MAX - 1U ||
        compressed_size > (size_t)255U * region_file_sector_bytes(region) - 5U) {
        slot->external = 1;
        slot->stored_length = 1U;
    } else {
        if (region->layout == REGION_LAYOUT_CUBIC_R2) slot->external = 0;
        slot->stored_length = (uint32_t)(compressed_size + 1U);
    }
    slot->timestamp = unix_time_now_u32();
    slot->present = 1;

    return 1;
}

static int valid_compression_type(uint8_t compression_type) {
    return compression_type == REGION_COMPRESSION_GZIP ||
           compression_type == REGION_COMPRESSION_ZLIB ||
           compression_type == REGION_COMPRESSION_NONE ||
           compression_type == REGION_COMPRESSION_LZ4;
}

static int chunk_uses_external_storage(const RegionFile* region, const RegionChunkSlot* slot) {
    size_t internal_limit = (size_t)255U * region_file_sector_bytes(region);

    if (!slot || !slot->present) return 0;
    if (slot->external) return 1;
    if (slot->payload_size > (size_t)UINT32_MAX - 1U) return 1;
    return slot->payload_size > internal_limit - 5U;
}

static int build_region_bytes(
    const RegionFile* region,
    const char* output_path,
    unsigned char** out_data,
    size_t* out_size,
    char* err,
    size_t err_sz
) {
    uint32_t locations[REGION_CHUNK_COUNT];
    uint32_t timestamps[REGION_CHUNK_COUNT];
    uint32_t sector_bytes;
    uint32_t next_sector;
    unsigned char* file_data = NULL;
    size_t file_size;
    int i;

    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;
    if (!region || !output_path || !out_data || !out_size) {
        set_err(err, err_sz, "invalid region write arguments");
        return 0;
    }
    if (region->layout != REGION_LAYOUT_STANDARD && region->layout != REGION_LAYOUT_CUBIC_R2) {
        set_err(err, err_sz, "invalid region layout in region model");
        return 0;
    }
    if (region->layout == REGION_LAYOUT_CUBIC_R2 && !region_path_is_cubic_r2(output_path)) {
        set_err(err, err_sz, "cubic r2 output requires an r2.<x>.<y>.<z>.mca/.mcr filename");
        return 0;
    }
    if (region->layout == REGION_LAYOUT_STANDARD && region_path_is_cubic_r2(output_path)) {
        set_err(err, err_sz, "standard region data cannot be written with a cubic r2 filename");
        return 0;
    }

    sector_bytes = region_file_sector_bytes(region);
    next_sector = region_file_header_sectors(region);

    memset(locations, 0, sizeof(locations));
    memset(timestamps, 0, sizeof(timestamps));

    for (i = 0; i < REGION_CHUNK_COUNT; i++) {
        const RegionChunkSlot* slot = &region->chunks[i];
        uint32_t sectors_needed;
        uint64_t chunk_total;
        int external;

        if (!slot->present) {
            timestamps[i] = 0;
            continue;
        }

        if (!valid_compression_type(slot->compression_type)) {
            set_err(err, err_sz, "invalid chunk compression type in region model");
            return 0;
        }

        if (!slot->payload && slot->payload_size > 0) {
            set_err(err, err_sz, "missing chunk payload data");
            return 0;
        }

        external = chunk_uses_external_storage(region, slot);
        if (external) {
            char* external_path;
            if (region->layout == REGION_LAYOUT_CUBIC_R2) {
                set_err(err, err_sz, "legacy cubic r2 regions do not support external chunk storage");
                return 0;
            }
            external_path = region_external_chunk_path(
                output_path,
                i % REGION_CHUNK_GRID,
                i / REGION_CHUNK_GRID
            );
            if (!external_path) {
                set_err(err, err_sz, "external chunk output requires a conventional r.<x>.<z>.mca/.mcr filename");
                return 0;
            }
            free(external_path);
            sectors_needed = 1U;
        } else {
            chunk_total = 4ULL + 1ULL + (uint64_t)slot->payload_size;
            sectors_needed = (uint32_t)((chunk_total + (sector_bytes - 1U)) / sector_bytes);
            if (sectors_needed == 0 || sectors_needed > 255U) {
                set_err(err, err_sz, "chunk is too large for internal region storage");
                return 0;
            }
        }

        if (next_sector > 0x00FFFFFFU || sectors_needed > 0x00FFFFFFU - next_sector + 1U) {
            set_err(err, err_sz, "region file exceeds 24-bit sector offset limit");
            return 0;
        }

        locations[i] = (next_sector << 8) | sectors_needed;
        timestamps[i] = slot->timestamp;
        next_sector += sectors_needed;
    }

    if ((size_t)next_sector > SIZE_MAX / sector_bytes) {
        set_err(err, err_sz, "region output is too large for this platform");
        return 0;
    }
    file_size = (size_t)next_sector * sector_bytes;
    file_data = calloc(1, file_size);
    if (!file_data) {
        set_err(err, err_sz, "out of memory while building region output");
        return 0;
    }

    for (i = 0; i < REGION_CHUNK_COUNT; i++) {
        write_be_u32(file_data + (size_t)i * 4U, locations[i]);
        write_be_u32(file_data + REGION_LOCATION_TABLE_BYTES + (size_t)i * 4U, timestamps[i]);
    }

    for (i = 0; i < REGION_CHUNK_COUNT; i++) {
        const RegionChunkSlot* slot = &region->chunks[i];
        uint32_t loc = locations[i];
        uint32_t sector_offset;
        size_t chunk_start;
        int external;

        if (!slot->present || loc == 0) continue;

        sector_offset = (loc >> 8) & 0x00FFFFFFU;
        chunk_start = (size_t)sector_offset * sector_bytes;

        external = chunk_uses_external_storage(region, slot);
        if (external) {
            write_be_u32(file_data + chunk_start, 1U);
            file_data[chunk_start + 4U] =
                (unsigned char)(slot->compression_type | REGION_EXTERNAL_STREAM_FLAG);
        } else {
            write_be_u32(file_data + chunk_start, (uint32_t)(slot->payload_size + 1U));
            file_data[chunk_start + 4U] = slot->compression_type;
            if (slot->payload_size > 0) {
                memcpy(file_data + chunk_start + 5U, slot->payload, slot->payload_size);
            }
        }
    }

    *out_data = file_data;
    *out_size = file_size;
    return 1;
}

static int write_bytes_direct(
    const char* output_path,
    const unsigned char* data,
    size_t size,
    char* err,
    size_t err_sz
) {
    FILE* out = nbt_fopen(output_path, "wb");

    if (!out) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "fopen(%s) failed: %s", output_path, strerror(errno));
        }
        return 0;
    }

    if (size > 0 && fwrite(data, 1, size, out) != size) {
        set_err(err, err_sz, "failed to write region output file");
        fclose(out);
        nbt_remove_file(output_path);
        return 0;
    }

    if (fclose(out) != 0) {
        set_err(err, err_sz, "failed to close region output file");
        nbt_remove_file(output_path);
        return 0;
    }

    return 1;
}

static int write_bytes_atomic(
    const char* output_path,
    const unsigned char* data,
    size_t size,
    const char* temp_prefix,
    char* err,
    size_t err_sz
) {
    char* temp_path = NULL;
    int fd;

    fd = nbt_open_temp_file(output_path, temp_prefix, &temp_path, err, err_sz);
    if (fd < 0) return 0;

    if (nbt_close_fd(fd) != 0) {
        set_err(err, err_sz, "failed to close temporary output file");
        nbt_remove_file(temp_path);
        free(temp_path);
        return 0;
    }

    if (!write_bytes_direct(temp_path, data, size, err, err_sz)) {
        nbt_remove_file(temp_path);
        free(temp_path);
        return 0;
    }

    if (!nbt_replace_file(temp_path, output_path, err, err_sz)) {
        nbt_remove_file(temp_path);
        free(temp_path);
        return 0;
    }

    free(temp_path);
    return 1;
}

static int write_external_chunks(
    const RegionFile* region,
    const char* output_path,
    char* err,
    size_t err_sz
) {
    int i;

    for (i = 0; i < REGION_CHUNK_COUNT; i++) {
        const RegionChunkSlot* slot = &region->chunks[i];
        char* external_path;

        if (!slot->present || !chunk_uses_external_storage(region, slot)) continue;
        external_path = region_external_chunk_path(
            output_path,
            i % REGION_CHUNK_GRID,
            i / REGION_CHUNK_GRID
        );
        if (!external_path) {
            set_err(err, err_sz, "failed to derive external .mcc chunk path");
            return 0;
        }

        if (!write_bytes_atomic(
                external_path,
                slot->payload,
                slot->payload_size,
                "chunk",
                err,
                err_sz)) {
            free(external_path);
            return 0;
        }
        free(external_path);
    }

    return 1;
}

int region_file_write(const RegionFile* region, const char* output_path, char* err, size_t err_sz) {
    unsigned char* file_data = NULL;
    size_t file_size = 0;
    int ok;

    if (!build_region_bytes(region, output_path, &file_data, &file_size, err, err_sz)) return 0;
    if (!write_external_chunks(region, output_path, err, err_sz)) {
        free(file_data);
        return 0;
    }

    ok = write_bytes_direct(output_path, file_data, file_size, err, err_sz);
    free(file_data);
    return ok;
}

int region_file_write_atomic(const RegionFile* region, const char* output_path, char* err, size_t err_sz) {
    unsigned char* file_data = NULL;
    size_t file_size = 0;
    int ok;

    if (!build_region_bytes(region, output_path, &file_data, &file_size, err, err_sz)) return 0;
    if (!write_external_chunks(region, output_path, err, err_sz)) {
        free(file_data);
        return 0;
    }

    /* External sidecars are replaced first; the region header is the commit point. */
    ok = write_bytes_atomic(output_path, file_data, file_size, "region", err, err_sz);
    free(file_data);
    return ok;
}
