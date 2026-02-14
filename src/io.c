#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <zlib.h>
#include "io.h"

#define CHUNK 16384

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

unsigned char* load_nbt_data_auto(const char* filename, size_t* out_size, NBTInputFormat* out_format, char* err, size_t err_sz) {
    unsigned char* input = NULL;
    unsigned char* decoded = NULL;
    size_t input_size = 0;
    size_t decoded_size = 0;

    if (out_size) *out_size = 0;
    if (out_format) *out_format = NBT_INPUT_FORMAT_UNKNOWN;

    input = read_file_bytes(filename, &input_size, err, err_sz);
    if (!input) {
        return NULL;
    }

    if (input_size == 0) {
        set_err(err, err_sz, "input file is empty");
        free(input);
        return NULL;
    }

    if (looks_like_gzip(input, input_size)) {
        decoded = inflate_buffer(input, input_size, 16 + MAX_WBITS, &decoded_size);
        if (!decoded) {
            set_err(err, err_sz, "failed to decompress gzip input");
            free(input);
            return NULL;
        }
        free(input);
        if (out_size) *out_size = decoded_size;
        if (out_format) *out_format = NBT_INPUT_FORMAT_GZIP;
        return decoded;
    }

    if (looks_like_zlib(input, input_size)) {
        decoded = inflate_buffer(input, input_size, MAX_WBITS, &decoded_size);
        if (!decoded) {
            set_err(err, err_sz, "failed to decompress zlib input");
            free(input);
            return NULL;
        }
        free(input);
        if (out_size) *out_size = decoded_size;
        if (out_format) *out_format = NBT_INPUT_FORMAT_ZLIB;
        return decoded;
    }

    if (out_size) *out_size = input_size;
    if (out_format) *out_format = NBT_INPUT_FORMAT_RAW;
    return input;
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
