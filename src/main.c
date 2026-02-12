#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "io.h"
#include "nbt_builder.h"
#include "nbt_utils.h"
#include "edit_save.h"
#include <zlib.h>
#include <unistd.h>

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
        char err[256] = {0};
        EditStatus st = edit_tag_by_path(root, argv[3], argv[4], err, sizeof(err));
        if (st != EDIT_OK) {
            if (err[0] != '\0') {
                printf("Failed to edit path '%s': %s (%s)\n", argv[3], err, edit_status_name(st));
            } else {
                printf("Failed to edit path '%s': %s\n", argv[3], edit_status_name(st));
            }
            free(data);
            free_nbt_tree(root);
            return 1;
        }
        printf("Updated %s successfully\n", argv[3]);

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

        fflush(stdout);
        int stdout_fd = dup(fileno(stdout));
        if (stdout_fd < 0) {
            perror("dup");
            fclose(dump_file);
            free(data);
            free_nbt_tree(root);
            return 1;
        }

        if (dup2(fileno(dump_file), fileno(stdout)) < 0) {
            perror("dup2");
            close(stdout_fd);
            fclose(dump_file);
            free(data);
            free_nbt_tree(root);
            return 1;
        }

        offset = 0;
        parse_nbt(data, &offset, 0); // Parse and write into the dump file

        fflush(stdout);
        if (dup2(stdout_fd, fileno(stdout)) < 0) {
            perror("dup2");
            close(stdout_fd);
            fclose(dump_file);
            free(data);
            free_nbt_tree(root);
            return 1;
        }
        close(stdout_fd);
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
