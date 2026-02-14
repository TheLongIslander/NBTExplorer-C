#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>
#include "edit_save.h"
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
             slot->compression_type == REGION_COMPRESSION_NONE)) {
            return slot->compression_type;
        }
        return REGION_COMPRESSION_ZLIB;
    }

    if (compression_override == REGION_COMPRESSION_GZIP ||
        compression_override == REGION_COMPRESSION_ZLIB ||
        compression_override == REGION_COMPRESSION_NONE) {
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

    if (compressed_size > (size_t)UINT32_MAX - 1U) {
        set_err(err, err_sz, "compressed chunk payload too large");
        free(compressed);
        return 0;
    }

    free(slot->payload);
    slot->payload = compressed;
    slot->payload_size = compressed_size;
    slot->compression_type = compression_type;
    slot->stored_length = (uint32_t)(compressed_size + 1U);
    slot->timestamp = unix_time_now_u32();
    slot->present = 1;

    return 1;
}

int region_file_write(const RegionFile* region, const char* output_path, char* err, size_t err_sz) {
    uint32_t locations[REGION_CHUNK_COUNT];
    uint32_t timestamps[REGION_CHUNK_COUNT];
    uint32_t next_sector = 2U;
    unsigned char* file_data = NULL;
    size_t file_size;
    FILE* out = NULL;
    int i;

    if (!region || !output_path) {
        set_err(err, err_sz, "invalid region write arguments");
        return 0;
    }

    memset(locations, 0, sizeof(locations));
    memset(timestamps, 0, sizeof(timestamps));

    for (i = 0; i < REGION_CHUNK_COUNT; i++) {
        const RegionChunkSlot* slot = &region->chunks[i];
        uint32_t sectors_needed;
        uint64_t chunk_total;

        if (!slot->present) {
            timestamps[i] = 0;
            continue;
        }

        if (slot->compression_type != REGION_COMPRESSION_GZIP &&
            slot->compression_type != REGION_COMPRESSION_ZLIB &&
            slot->compression_type != REGION_COMPRESSION_NONE) {
            set_err(err, err_sz, "invalid chunk compression type in region model");
            return 0;
        }

        if (!slot->payload && slot->payload_size > 0) {
            set_err(err, err_sz, "missing chunk payload data");
            return 0;
        }

        if (slot->payload_size > (size_t)UINT32_MAX - 1U) {
            set_err(err, err_sz, "chunk payload too large for .mca length field");
            return 0;
        }

        chunk_total = 4ULL + 1ULL + (uint64_t)slot->payload_size;
        sectors_needed = (uint32_t)((chunk_total + (REGION_SECTOR_BYTES - 1U)) / REGION_SECTOR_BYTES);

        if (sectors_needed == 0 || sectors_needed > 255U) {
            set_err(err, err_sz, "chunk is too large for .mca sector count field");
            return 0;
        }

        if (next_sector > 0x00FFFFFFU || sectors_needed > 0x00FFFFFFU - next_sector + 1U) {
            set_err(err, err_sz, "region file exceeds 24-bit sector offset limit");
            return 0;
        }

        locations[i] = (next_sector << 8) | sectors_needed;
        timestamps[i] = slot->timestamp;
        next_sector += sectors_needed;
    }

    file_size = (size_t)next_sector * REGION_SECTOR_BYTES;
    file_data = calloc(1, file_size);
    if (!file_data) {
        set_err(err, err_sz, "out of memory while building .mca output");
        return 0;
    }

    for (i = 0; i < REGION_CHUNK_COUNT; i++) {
        write_be_u32(file_data + (size_t)i * 4U, locations[i]);
        write_be_u32(file_data + REGION_SECTOR_BYTES + (size_t)i * 4U, timestamps[i]);
    }

    for (i = 0; i < REGION_CHUNK_COUNT; i++) {
        const RegionChunkSlot* slot = &region->chunks[i];
        uint32_t loc = locations[i];
        uint32_t sector_offset;
        size_t chunk_start;

        if (!slot->present || loc == 0) continue;

        sector_offset = (loc >> 8) & 0x00FFFFFFU;
        chunk_start = (size_t)sector_offset * REGION_SECTOR_BYTES;

        write_be_u32(file_data + chunk_start, (uint32_t)(slot->payload_size + 1U));
        file_data[chunk_start + 4U] = slot->compression_type;
        if (slot->payload_size > 0) {
            memcpy(file_data + chunk_start + 5U, slot->payload, slot->payload_size);
        }
    }

    out = fopen(output_path, "wb");
    if (!out) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "fopen(%s) failed: %s", output_path, strerror(errno));
        }
        free(file_data);
        return 0;
    }

    if (fwrite(file_data, 1, file_size, out) != file_size) {
        set_err(err, err_sz, "failed to write .mca output file");
        fclose(out);
        remove(output_path);
        free(file_data);
        return 0;
    }

    if (fclose(out) != 0) {
        set_err(err, err_sz, "failed to close .mca output file");
        remove(output_path);
        free(file_data);
        return 0;
    }

    free(file_data);
    return 1;
}

static char* make_temp_template(const char* target_path) {
    const char* slash = strrchr(target_path, '/');
    const char* base = ".region_tmp_XXXXXX";
    size_t base_len = strlen(base);
    char* tmpl;

    if (!slash) {
        tmpl = malloc(base_len + 1);
        if (!tmpl) return NULL;
        memcpy(tmpl, base, base_len + 1);
        return tmpl;
    }

    {
        size_t dir_len = (size_t)(slash - target_path + 1);
        tmpl = malloc(dir_len + base_len + 1);
        if (!tmpl) return NULL;
        memcpy(tmpl, target_path, dir_len);
        memcpy(tmpl + dir_len, base, base_len);
        tmpl[dir_len + base_len] = '\0';
    }

    return tmpl;
}

int region_file_write_atomic(const RegionFile* region, const char* output_path, char* err, size_t err_sz) {
    char* tmp_template;
    int fd;
    int ok;

    if (!region || !output_path) {
        set_err(err, err_sz, "invalid region write arguments");
        return 0;
    }

    tmp_template = make_temp_template(output_path);
    if (!tmp_template) {
        set_err(err, err_sz, "out of memory");
        return 0;
    }

    fd = mkstemp(tmp_template);
    if (fd < 0) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "mkstemp(%s) failed: %s", tmp_template, strerror(errno));
        }
        free(tmp_template);
        return 0;
    }

    close(fd);

    ok = region_file_write(region, tmp_template, err, err_sz);
    if (!ok) {
        unlink(tmp_template);
        free(tmp_template);
        return 0;
    }

    if (rename(tmp_template, output_path) != 0) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "rename(%s -> %s) failed: %s", tmp_template, output_path, strerror(errno));
        }
        unlink(tmp_template);
        free(tmp_template);
        return 0;
    }

    free(tmp_template);
    return 1;
}
