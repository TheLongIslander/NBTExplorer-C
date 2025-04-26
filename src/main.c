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
        printf("Usage:\n");
        printf("  %s <file.dat> [--edit path newValue]\n", argv[0]);
        printf("  %s <file.dat> [--dump output.txt]\n", argv[0]);
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
        // EDIT MODE
        NBTTag* target = find_tag_by_path(root, argv[3]);

        if (target) {
            if (target->type == TAG_Int) {
                int new_val = atoi(argv[4]);
                target->value.int_val = new_val;
                printf("Updated %s (Int) to %d\n", argv[3], new_val);
            } else if (target->type == TAG_Byte) {
                int new_val = atoi(argv[4]);
                target->value.byte_val = (int8_t)new_val;
                printf("Updated %s (Byte) to %d\n", argv[3], new_val);
            } else if (target->type == TAG_Short) {
                int new_val = atoi(argv[4]);
                target->value.short_val = (int16_t)new_val;
                printf("Updated %s (Short) to %d\n", argv[3], new_val);
            } else if (target->type == TAG_Long) {
                long long new_val = atoll(argv[4]);
                target->value.long_val = (int64_t)new_val;
                printf("Updated %s (Long) to %lld\n", argv[3], new_val);
            } else if (target->type == TAG_Float) {
                float new_val = atof(argv[4]);
                target->value.float_val = new_val;
                printf("Updated %s (Float) to %f\n", argv[3], new_val);
            } else if (target->type == TAG_Double) {
                double new_val = atof(argv[4]);
                target->value.double_val = new_val;
                printf("Updated %s (Double) to %lf\n", argv[3], new_val);
            } else {
                printf("Failed: editing not supported for tag type %d at path: %s\n", target->type, argv[3]);
                free(data);
                free_nbt_tree(root);
                return 1;
            }
        } else {
            printf("Failed to find tag at path: %s\n", argv[3]);
            free(data);
            free_nbt_tree(root);
            return 1;
        }
        

        gzFile out = gzopen("modified_output.dat", "wb");
        if (!out) {
            perror("gzopen");
            free(data);
            free_nbt_tree(root);
            return 1;
        }

        write_tag(out, root);
        gzclose(out);
        printf("Saved modified NBT to modified_output.dat\n");

    } else if (argc == 4 && strcmp(argv[2], "--dump") == 0) {
        // DUMP MODE
        FILE* dump_file = fopen(argv[3], "w");
        if (!dump_file) {
            perror("fopen");
            free(data);
            free_nbt_tree(root);
            return 1;
        }

        // Redirect stdout temporarily
        FILE* old_stdout = stdout;
        stdout = dump_file;

        offset = 0;
        parse_nbt(data, &offset, 0); // Parse and write into the dump file

        stdout = old_stdout;
        fclose(dump_file);

        printf("Dumped parsed NBT to %s\n", argv[3]);

    } else {
        // DEFAULT MODE (print to terminal)
        offset = 0;
        parse_nbt(data, &offset, 0); 
        printf("Parsed and printed in %.2f ms\n", elapsed_ms);
    }

    free(data);
    free_nbt_tree(root);
    return 0;
}
