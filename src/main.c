#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "io.h"
#include "nbt_builder.h"
#include "nbt_utils.h"
#include "edit_save.h"
#include <zlib.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <file.dat> [--edit path newValue]\n", argv[0]);
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

    NBTTag* root = build_nbt_tree(data, &offset);

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                        (end.tv_nsec - start.tv_nsec) / 1e6;

    printf("Parsed in %.2f ms\n", elapsed_ms);

    if (root) {
        printf("Root tag name: '%s' | type: %d\n", root->name ? root->name : "", root->type);
        if (root->type == TAG_Compound) {
            printf("Root has %d children:\n", root->value.compound.count);
            for (int i = 0; i < root->value.compound.count; i++) {
                NBTTag* child = root->value.compound.items[i];
                printf("  - %s (type %d)\n", child->name, child->type);
            }
        }
    }

    if (argc == 5 && strcmp(argv[2], "--edit") == 0) {
        NBTTag* target = find_tag_by_path(root, argv[3]);

        if (target && (target->type == TAG_Int || target->type == TAG_Byte)) {
            int new_val = atoi(argv[4]);
            if (target->type == TAG_Int) target->value.int_val = new_val;
            if (target->type == TAG_Byte) target->value.byte_val = (int8_t)new_val;
            printf("Updated %s to %d\n", argv[3], new_val);
        } else {
            printf("Failed to find editable int/byte tag at path: %s\n", argv[3]);
            free(data);
            return 1;
        }

        gzFile out = gzopen("modified_output.dat", "wb");
        if (!out) {
            perror("gzopen");
            free(data);
            return 1;
        }

        write_tag(out, root);
        gzclose(out);
        printf("Saved modified NBT to modified_output.dat\n");
    } else {
        offset = 0;
        parse_nbt(data, &offset, 0); // Print like normal
        printf("Parsed and printed in %.2f ms\n", elapsed_ms);
    }

    free(data);
    free_nbt_tree(root);
    return 0;
}
