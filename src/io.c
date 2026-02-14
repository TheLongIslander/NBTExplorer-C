#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <zlib.h>
#include "io.h"

#define CHUNK 16384
#define REGION_SECTOR_BYTES 4096U
#define REGION_HEADER_BYTES (REGION_SECTOR_BYTES * 2U)
#define REGION_CHUNK_GRID 32
#define REGION_CHUNK_COUNT (REGION_CHUNK_GRID * REGION_CHUNK_GRID)

static void set_err(char* err, size_t err_sz, const char* msg) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", msg);
    }
}

static unsigned char* read_file_bytes(const char* filename, size_t* out_size, char* err, size_t err_sz) {
    FILE* file = fopen(filename, "rb");
    unsigned char* buffer = NULL;
    size_t size = 0;
    size_t capacity = 0;

    if (out_size) *out_size = 0;
    if (!file) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "fopen(%s) failed: %s", filename, strerror(errno));
        }
        return NULL;
    }

    capacity = CHUNK;
    buffer = malloc(capacity);
    if (!buffer) {
        fclose(file);
        set_err(err, err_sz, "out of memory");
        return NULL;
    }

    while (1) {
        size_t bytes_read;

        if (size > SIZE_MAX - CHUNK) {
            set_err(err, err_sz, "input too large");
            free(buffer);
            fclose(file);
            return NULL;
        }
        if (size + CHUNK > capacity) {
            size_t new_capacity = size + CHUNK;
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

        bytes_read = fread(buffer + size, 1, CHUNK, file);
        if (bytes_read == 0) {
            if (ferror(file)) {
                set_err(err, err_sz, "failed to read input file");
                free(buffer);
                fclose(file);
                return NULL;
            }
            break;
        }

        size += bytes_read;
    }

    if (out_size) *out_size = size;
    fclose(file);
    return buffer;
}

static int looks_like_gzip(const unsigned char* data, size_t size) {
    return data && size >= 2 && data[0] == 0x1f && data[1] == 0x8b;
}

static int looks_like_zlib(const unsigned char* data, size_t size) {
    unsigned int header;
    if (!data || size < 2) return 0;
    if ((data[0] & 0x0f) != 8) return 0;
    if ((data[0] >> 4) > 7) return 0;
    header = ((unsigned int)data[0] << 8) | (unsigned int)data[1];
    return (header % 31U) == 0;
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

    capacity = CHUNK;
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
            if (capacity > SIZE_MAX - CHUNK) {
                free(out);
                inflateEnd(&zs);
                return NULL;
            }
            new_capacity = capacity + CHUNK;
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

static int has_mca_extension(const char* filename) {
    const char* dot;

    if (!filename) return 0;
    dot = strrchr(filename, '.');
    if (!dot) return 0;
    return (dot[1] == 'm' || dot[1] == 'M') &&
           (dot[2] == 'c' || dot[2] == 'C') &&
           (dot[3] == 'a' || dot[3] == 'A') &&
           dot[4] == '\0';
}

static uint32_t read_be_u32(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
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

static unsigned char* decode_nbt_payload(
    const unsigned char* input,
    size_t input_size,
    NBTInputFormat* out_format,
    size_t* out_size,
    char* err,
    size_t err_sz
) {
    unsigned char* decoded = NULL;
    size_t decoded_size = 0;

    if (out_size) *out_size = 0;
    if (out_format) *out_format = NBT_INPUT_FORMAT_UNKNOWN;

    if (!input) {
        set_err(err, err_sz, "invalid input buffer");
        return NULL;
    }
    if (input_size == 0) {
        set_err(err, err_sz, "input file is empty");
        return NULL;
    }

    if (looks_like_gzip(input, input_size)) {
        decoded = inflate_buffer(input, input_size, 16 + MAX_WBITS, &decoded_size);
        if (!decoded) {
            set_err(err, err_sz, "failed to decompress gzip input");
            return NULL;
        }
        if (out_size) *out_size = decoded_size;
        if (out_format) *out_format = NBT_INPUT_FORMAT_GZIP;
        return decoded;
    }

    if (looks_like_zlib(input, input_size)) {
        decoded = inflate_buffer(input, input_size, MAX_WBITS, &decoded_size);
        if (!decoded) {
            set_err(err, err_sz, "failed to decompress zlib input");
            return NULL;
        }
        if (out_size) *out_size = decoded_size;
        if (out_format) *out_format = NBT_INPUT_FORMAT_ZLIB;
        return decoded;
    }

    decoded = copy_bytes(input, input_size);
    if (!decoded) {
        set_err(err, err_sz, "out of memory");
        return NULL;
    }
    if (out_size) *out_size = input_size;
    if (out_format) *out_format = NBT_INPUT_FORMAT_RAW;
    return decoded;
}

static int load_region_chunk_entry(
    const unsigned char* region_data,
    size_t region_size,
    int chunk_x,
    int chunk_z,
    size_t* out_chunk_start,
    size_t* out_chunk_span,
    char* err,
    size_t err_sz
) {
    size_t idx;
    uint32_t entry;
    uint32_t sector_offset;
    uint32_t sector_count;
    size_t chunk_start;
    size_t chunk_span;

    if (!region_data || region_size < REGION_HEADER_BYTES) {
        set_err(err, err_sz, "invalid .mca file: missing region header");
        return 0;
    }
    if (chunk_x < 0 || chunk_x >= REGION_CHUNK_GRID || chunk_z < 0 || chunk_z >= REGION_CHUNK_GRID) {
        set_err(err, err_sz, "chunk coordinates must be within 0..31");
        return 0;
    }

    idx = (size_t)chunk_z * REGION_CHUNK_GRID + (size_t)chunk_x;
    entry = read_be_u32(region_data + idx * 4U);
    sector_offset = (entry >> 8) & 0x00FFFFFFU;
    sector_count = entry & 0x000000FFU;

    if (sector_offset == 0 || sector_count == 0) {
        set_err(err, err_sz, "requested chunk is empty in this region");
        return 0;
    }
    if ((uint64_t)sector_offset * REGION_SECTOR_BYTES > (uint64_t)SIZE_MAX ||
        (uint64_t)sector_count * REGION_SECTOR_BYTES > (uint64_t)SIZE_MAX) {
        set_err(err, err_sz, "corrupt .mca chunk location overflow");
        return 0;
    }

    chunk_start = (size_t)sector_offset * REGION_SECTOR_BYTES;
    chunk_span = (size_t)sector_count * REGION_SECTOR_BYTES;

    if (chunk_start > region_size || chunk_span > region_size - chunk_start) {
        set_err(err, err_sz, "corrupt .mca chunk location points outside file");
        return 0;
    }
    if (out_chunk_start) *out_chunk_start = chunk_start;
    if (out_chunk_span) *out_chunk_span = chunk_span;
    return 1;
}

static int find_first_region_chunk(
    const unsigned char* region_data,
    size_t region_size,
    int* out_chunk_x,
    int* out_chunk_z,
    size_t* out_chunk_start,
    size_t* out_chunk_span,
    char* err,
    size_t err_sz
) {
    int idx;

    if (!region_data || region_size < REGION_HEADER_BYTES) {
        set_err(err, err_sz, "invalid .mca file: missing region header");
        return 0;
    }

    for (idx = 0; idx < REGION_CHUNK_COUNT; idx++) {
        uint32_t entry = read_be_u32(region_data + (size_t)idx * 4U);
        uint32_t sector_offset = (entry >> 8) & 0x00FFFFFFU;
        uint32_t sector_count = entry & 0x000000FFU;
        if (sector_offset != 0 && sector_count != 0) {
            int chunk_x = idx % REGION_CHUNK_GRID;
            int chunk_z = idx / REGION_CHUNK_GRID;
            if (!load_region_chunk_entry(region_data, region_size, chunk_x, chunk_z, out_chunk_start, out_chunk_span, err, err_sz)) {
                return 0;
            }
            if (out_chunk_x) *out_chunk_x = chunk_x;
            if (out_chunk_z) *out_chunk_z = chunk_z;
            return 1;
        }
    }

    set_err(err, err_sz, "no populated chunks found in .mca file");
    return 0;
}

static unsigned char* decode_region_chunk_payload(
    const unsigned char* region_data,
    size_t region_size,
    size_t chunk_start,
    size_t chunk_span,
    NBTInputFormat* out_format,
    size_t* out_size,
    char* err,
    size_t err_sz
) {
    uint32_t length_field;
    uint8_t compression_type;
    size_t payload_size;
    const unsigned char* payload;
    unsigned char* out;

    if (out_size) *out_size = 0;
    if (out_format) *out_format = NBT_INPUT_FORMAT_UNKNOWN;

    if (!region_data) {
        set_err(err, err_sz, "invalid .mca buffer");
        return NULL;
    }
    if (chunk_start > region_size || chunk_span > region_size - chunk_start) {
        set_err(err, err_sz, "corrupt .mca chunk bounds");
        return NULL;
    }
    if (chunk_span < 5) {
        set_err(err, err_sz, "corrupt .mca chunk is too small");
        return NULL;
    }

    length_field = read_be_u32(region_data + chunk_start);
    if (length_field == 0) {
        set_err(err, err_sz, "corrupt .mca chunk has zero length");
        return NULL;
    }
    if ((size_t)length_field + 4U > chunk_span) {
        set_err(err, err_sz, "corrupt .mca chunk length exceeds allocated sectors");
        return NULL;
    }

    compression_type = region_data[chunk_start + 4U];
    payload_size = (size_t)length_field - 1U;
    payload = region_data + chunk_start + 5U;

    if (payload_size > chunk_span - 5U) {
        set_err(err, err_sz, "corrupt .mca payload length");
        return NULL;
    }

    switch (compression_type) {
        case 1:
            out = inflate_buffer(payload, payload_size, 16 + MAX_WBITS, out_size);
            if (!out) {
                set_err(err, err_sz, "failed to decompress gzip .mca chunk payload");
                return NULL;
            }
            if (out_format) *out_format = NBT_INPUT_FORMAT_GZIP;
            return out;
        case 2:
            out = inflate_buffer(payload, payload_size, MAX_WBITS, out_size);
            if (!out) {
                set_err(err, err_sz, "failed to decompress zlib .mca chunk payload");
                return NULL;
            }
            if (out_format) *out_format = NBT_INPUT_FORMAT_ZLIB;
            return out;
        case 3:
            out = copy_bytes(payload, payload_size);
            if (!out) {
                set_err(err, err_sz, "out of memory");
                return NULL;
            }
            if (out_size) *out_size = payload_size;
            if (out_format) *out_format = NBT_INPUT_FORMAT_RAW;
            return out;
        default:
            if (err && err_sz > 0) {
                snprintf(err, err_sz, "unsupported .mca compression type %u", (unsigned int)compression_type);
            }
            return NULL;
    }
}

static unsigned char* load_nbt_from_region_file(
    const char* filename,
    size_t* out_size,
    const NBTLoadOptions* opts,
    NBTLoadInfo* out_info,
    char* err,
    size_t err_sz
) {
    unsigned char* region_data = NULL;
    unsigned char* decoded = NULL;
    size_t region_size = 0;
    size_t chunk_start = 0;
    size_t chunk_span = 0;
    int chunk_x = 0;
    int chunk_z = 0;
    int have_chunk = 0;

    region_data = read_file_bytes(filename, &region_size, err, err_sz);
    if (!region_data) {
        return NULL;
    }
    if (region_size < REGION_HEADER_BYTES) {
        set_err(err, err_sz, "invalid .mca file: expected at least 8192-byte header");
        free(region_data);
        return NULL;
    }

    if (opts && opts->has_chunk_coords) {
        chunk_x = opts->chunk_x;
        chunk_z = opts->chunk_z;
        if (!load_region_chunk_entry(region_data, region_size, chunk_x, chunk_z, &chunk_start, &chunk_span, err, err_sz)) {
            free(region_data);
            return NULL;
        }
        have_chunk = 1;
    } else {
        if (!find_first_region_chunk(region_data, region_size, &chunk_x, &chunk_z, &chunk_start, &chunk_span, err, err_sz)) {
            free(region_data);
            return NULL;
        }
        have_chunk = 1;
    }

    if (!have_chunk) {
        set_err(err, err_sz, "failed to resolve .mca chunk");
        free(region_data);
        return NULL;
    }

    decoded = decode_region_chunk_payload(region_data, region_size, chunk_start, chunk_span, out_info ? &out_info->input_format : NULL, out_size, err, err_sz);
    free(region_data);
    if (!decoded) {
        return NULL;
    }

    if (out_info) {
        out_info->source_type = NBT_SOURCE_REGION_CHUNK;
        out_info->chunk_x = chunk_x;
        out_info->chunk_z = chunk_z;
    }

    return decoded;
}

unsigned char* load_nbt_data(const char* filename, size_t* out_size, const NBTLoadOptions* opts, NBTLoadInfo* out_info, char* err, size_t err_sz) {
    unsigned char* input = NULL;
    unsigned char* decoded = NULL;
    size_t input_size = 0;

    if (out_size) *out_size = 0;
    if (out_info) {
        out_info->input_format = NBT_INPUT_FORMAT_UNKNOWN;
        out_info->source_type = NBT_SOURCE_STANDALONE;
        out_info->chunk_x = -1;
        out_info->chunk_z = -1;
    }

    if (!filename) {
        set_err(err, err_sz, "missing input filename");
        return NULL;
    }

    if (has_mca_extension(filename)) {
        return load_nbt_from_region_file(filename, out_size, opts, out_info, err, err_sz);
    }

    if (opts && opts->has_chunk_coords) {
        set_err(err, err_sz, "--chunk is only valid with .mca region files");
        return NULL;
    }

    input = read_file_bytes(filename, &input_size, err, err_sz);
    if (!input) {
        return NULL;
    }

    decoded = decode_nbt_payload(input, input_size, out_info ? &out_info->input_format : NULL, out_size, err, err_sz);
    free(input);
    return decoded;
}

unsigned char* load_nbt_data_auto(const char* filename, size_t* out_size, NBTInputFormat* out_format, char* err, size_t err_sz) {
    NBTLoadInfo info;
    unsigned char* data = load_nbt_data(filename, out_size, NULL, &info, err, err_sz);
    if (out_format) {
        *out_format = info.input_format;
    }
    return data;
}

const char* nbt_input_format_name(NBTInputFormat fmt) {
    switch (fmt) {
        case NBT_INPUT_FORMAT_GZIP:
            return "gzip";
        case NBT_INPUT_FORMAT_ZLIB:
            return "zlib";
        case NBT_INPUT_FORMAT_RAW:
            return "raw";
        default:
            return "unknown";
    }
}

const char* nbt_source_type_name(NBTSourceType source_type) {
    switch (source_type) {
        case NBT_SOURCE_STANDALONE:
            return "standalone_nbt";
        case NBT_SOURCE_REGION_CHUNK:
            return "mca_chunk";
        default:
            return "unknown";
    }
}
