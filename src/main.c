#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "io.h"
#include "nbt_parser.h"
#include "nbt_utils.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <file.nbt>\n", argv[0]);
        return 1;
    }

    size_t size;
    unsigned char* data = decompress_gzip(argv[1], &size);
    if (!data) {
        fprintf(stderr, "Failed to load file\n");
        return 1;
    }

    size_t offset = 0;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    parse_nbt(data, &offset, 0);

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                        (end.tv_nsec - start.tv_nsec) / 1e6;

    printf("Parsed and printed in %.2f ms\n", elapsed_ms);

    free(data);
    return 0;
}
