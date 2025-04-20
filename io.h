#ifndef IO_H
#define IO_H

#include <stddef.h>

unsigned char* decompress_gzip(const char* filename, size_t* out_size);

#endif
