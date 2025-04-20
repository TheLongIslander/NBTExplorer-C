#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include "io.h"

#define CHUNK 16384

unsigned char* decompress_gzip(const char* filename, size_t* out_size) {
    gzFile file = gzopen(filename, "rb");
    if (!file) {
        perror("gzopen");
        return NULL;
    }

    unsigned char* buffer = malloc(CHUNK);
    if (!buffer) return NULL;

    size_t size = 0;
    int bytes_read;
    while ((bytes_read = gzread(file, buffer + size, CHUNK)) > 0) {
        size += bytes_read;
        buffer = realloc(buffer, size + CHUNK);
    }

    *out_size = size;
    gzclose(file);
    return buffer;
}
