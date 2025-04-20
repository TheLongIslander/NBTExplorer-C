#include <stdio.h>
#include <stdlib.h>
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
    parse_nbt(data, &offset, 0);

    free(data);
    return 0;
}
