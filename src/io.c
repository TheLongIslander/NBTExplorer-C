#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>
#include "io.h"

#define CHUNK 16384

unsigned char* decompress_gzip(const char* filename, size_t* out_size) {
    gzFile file = gzopen(filename, "rb");
    unsigned char* buffer = NULL;
    size_t size = 0;
    size_t capacity = 0;

    if (out_size) *out_size = 0;
    if (!file) {
        perror("gzopen");
        return NULL;
    }

    capacity = CHUNK;
    buffer = malloc(capacity);
    if (!buffer) {
        gzclose(file);
        return NULL;
    }

    while (1) {
        int bytes_read;

        if (size > SIZE_MAX - CHUNK) {
            fprintf(stderr, "Input too large to decompress safely\n");
            free(buffer);
            gzclose(file);
            return NULL;
        }
        if (size + CHUNK > capacity) {
            size_t new_capacity = size + CHUNK;
            unsigned char* grown = realloc(buffer, new_capacity);
            if (!grown) {
                fprintf(stderr, "Out of memory while growing decompression buffer\n");
                free(buffer);
                gzclose(file);
                return NULL;
            }
            buffer = grown;
            capacity = new_capacity;
        }

        bytes_read = gzread(file, buffer + size, CHUNK);
        if (bytes_read < 0) {
            int zerr = 0;
            const char* msg = gzerror(file, &zerr);
            fprintf(stderr, "gzread failed: %s\n", msg ? msg : "unknown zlib error");
            free(buffer);
            gzclose(file);
            return NULL;
        }
        if (bytes_read == 0) {
            break;
        }
        size += bytes_read;
    }

    if (out_size) *out_size = size;
    gzclose(file);
    return buffer;
}
