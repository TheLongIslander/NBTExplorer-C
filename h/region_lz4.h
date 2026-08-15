#ifndef REGION_LZ4_H
#define REGION_LZ4_H

#include <stddef.h>

/* Minecraft uses lz4-java's legacy LZ4Block stream, not the standard frame. */
unsigned char* region_lz4_decode(
    const unsigned char* input,
    size_t input_size,
    size_t* out_size,
    char* err,
    size_t err_sz
);

unsigned char* region_lz4_encode(
    const unsigned char* input,
    size_t input_size,
    size_t* out_size,
    char* err,
    size_t err_sz
);

#endif
