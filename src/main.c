#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "io.h"
#include "nbt_builder.h"
#include "nbt_utils.h"
#include "edit_save.h"
#include <zlib.h>
#include <unistd.h>

typedef enum {
    MODE_DEFAULT = 0,
    MODE_EDIT,
    MODE_SET,
    MODE_DELETE,
    MODE_DUMP
} CliMode;

static void print_usage(const char* prog) {
    printf("Usage:\n");
    printf("  %s <file.dat> [--edit path newValue] [--output out.dat | --in-place [--backup[=suffix]]]\n", prog);
    printf("  %s <file.dat> [--set path newValue] [--output out.dat | --in-place [--backup[=suffix]]]\n", prog);
    printf("  %s <file.dat> [--delete path] [--output out.dat | --in-place [--backup[=suffix]]]\n", prog);
    printf("  %s <file.dat> [--dump output.txt]\n", prog);
}

static void set_err(char* err, size_t err_sz, const char* msg) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", msg);
    }
}

static int copy_file(const char* src, const char* dst, char* err, size_t err_sz) {
    FILE* in = NULL;
    FILE* out = NULL;
    unsigned char buf[8192];
    size_t n;
    int ok = 0;

    in = fopen(src, "rb");
    if (!in) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "fopen(%s): %s", src, strerror(errno));
        }
        return 0;
    }

    out = fopen(dst, "wb");
    if (!out) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "fopen(%s): %s", dst, strerror(errno));
        }
        fclose(in);
        return 0;
    }

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            set_err(err, err_sz, "failed to write backup file");
            goto done;
        }
    }

    if (ferror(in)) {
        set_err(err, err_sz, "failed to read input file while creating backup");
        goto done;
    }

    ok = 1;

done:
    fclose(in);
    if (fclose(out) != 0) {
        ok = 0;
    }
    if (!ok) {
        remove(dst);
    }
    return ok;
}

static char* make_backup_path(const char* input_path, const char* suffix) {
    size_t in_len = strlen(input_path);
    size_t suf_len = strlen(suffix);
    char* out = malloc(in_len + suf_len + 1);
    if (!out) return NULL;
    memcpy(out, input_path, in_len);
    memcpy(out + in_len, suffix, suf_len);
    out[in_len + suf_len] = '\0';
    return out;
}

static char* make_temp_template(const char* target_path) {
    const char* slash = strrchr(target_path, '/');
    const char* base = ".nbt_explorer_tmp_XXXXXX";
    size_t base_len = strlen(base);
    char* tmpl;

    if (!slash) {
        tmpl = malloc(base_len + 1);
        if (!tmpl) return NULL;
        memcpy(tmpl, base, base_len + 1);
        return tmpl;
    }

    size_t dir_len = (size_t)(slash - target_path + 1);
    tmpl = malloc(dir_len + base_len + 1);
    if (!tmpl) return NULL;

    memcpy(tmpl, target_path, dir_len);
    memcpy(tmpl + dir_len, base, base_len);
    tmpl[dir_len + base_len] = '\0';
    return tmpl;
}

static int write_nbt_atomically(const char* target_path, NBTTag* root, char* err, size_t err_sz) {
    char* tmp_template = make_temp_template(target_path);
    int fd;
    gzFile out;
    int zret;

    if (!tmp_template) {
        set_err(err, err_sz, "out of memory");
        return 0;
    }

    fd = mkstemp(tmp_template);
    if (fd < 0) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "mkstemp(%s): %s", tmp_template, strerror(errno));
        }
        free(tmp_template);
        return 0;
    }

    out = gzdopen(fd, "wb");
    if (!out) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "gzdopen: %s", strerror(errno));
        }
        close(fd);
        unlink(tmp_template);
        free(tmp_template);
        return 0;
    }

    write_tag(out, root);
    zret = gzclose(out);
    if (zret != Z_OK) {
        set_err(err, err_sz, "failed to finish compressed output write");
        unlink(tmp_template);
        free(tmp_template);
        return 0;
    }

    if (rename(tmp_template, target_path) != 0) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "rename(%s -> %s): %s", tmp_template, target_path, strerror(errno));
        }
        unlink(tmp_template);
        free(tmp_template);
        return 0;
    }

    free(tmp_template);
    return 1;
}

int main(int argc, char* argv[]) {
    CliMode mode = MODE_DEFAULT;
    const char* input_path;
    const char* op_path = NULL;
    const char* op_value = NULL;
    const char* dump_path = NULL;
    const char* output_path = NULL;
    const char* backup_suffix = ".bak";
    int in_place = 0;
    int backup_enabled = 0;

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    input_path = argv[1];

    for (int i = 2; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "--edit") == 0) {
            if (mode != MODE_DEFAULT || i + 2 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            mode = MODE_EDIT;
            op_path = argv[++i];
            op_value = argv[++i];
            continue;
        }

        if (strcmp(arg, "--set") == 0) {
            if (mode != MODE_DEFAULT || i + 2 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            mode = MODE_SET;
            op_path = argv[++i];
            op_value = argv[++i];
            continue;
        }

        if (strcmp(arg, "--delete") == 0) {
            if (mode != MODE_DEFAULT || i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            mode = MODE_DELETE;
            op_path = argv[++i];
            continue;
        }

        if (strcmp(arg, "--dump") == 0) {
            if (mode != MODE_DEFAULT || i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            mode = MODE_DUMP;
            dump_path = argv[++i];
            continue;
        }

        if (strcmp(arg, "--output") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            output_path = argv[++i];
            continue;
        }

        if (strcmp(arg, "--in-place") == 0) {
            in_place = 1;
            continue;
        }

        if (strcmp(arg, "--backup") == 0) {
            backup_enabled = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                backup_suffix = argv[++i];
            }
            continue;
        }

        if (strncmp(arg, "--backup=", 9) == 0) {
            backup_enabled = 1;
            backup_suffix = arg + 9;
            if (backup_suffix[0] == '\0') {
                fprintf(stderr, "Invalid --backup suffix\n");
                return 1;
            }
            continue;
        }

        print_usage(argv[0]);
        return 1;
    }

    if (mode != MODE_EDIT && mode != MODE_SET && mode != MODE_DELETE && (output_path || in_place || backup_enabled)) {
        fprintf(stderr, "--output/--in-place/--backup are only valid with --edit/--set/--delete\n");
        return 1;
    }

    if ((mode == MODE_EDIT || mode == MODE_SET || mode == MODE_DELETE) && output_path && in_place) {
        fprintf(stderr, "Use either --output or --in-place, not both\n");
        return 1;
    }

    if ((mode == MODE_EDIT || mode == MODE_SET || mode == MODE_DELETE) && backup_enabled && !in_place) {
        fprintf(stderr, "--backup is only valid with --in-place\n");
        return 1;
    }

    if ((mode == MODE_EDIT || mode == MODE_SET || mode == MODE_DELETE) && backup_enabled && backup_suffix[0] == '\0') {
        fprintf(stderr, "Backup suffix cannot be empty\n");
        return 1;
    }

    size_t size;
    unsigned char* data = decompress_gzip(input_path, &size);
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

    if (!root) {
        fprintf(stderr, "Failed to parse NBT root\n");
        free(data);
        return 1;
    }

    printf("Root tag name: '%s' | type: %d\n", root->name ? root->name : "", root->type);
    if (root->type == TAG_Compound) {
        printf("Root has %d children:\n", root->value.compound.count);
        for (int i = 0; i < root->value.compound.count; i++) {
            NBTTag* child = root->value.compound.items[i];
            printf("  - %s (type %d)\n", child->name, child->type);
        }
    }

    if (mode == MODE_EDIT || mode == MODE_SET || mode == MODE_DELETE) {
        char err[256] = {0};
        char io_err[256] = {0};
        char* backup_path = NULL;
        const char* write_path = "modified_output.dat";
        const char* op_name = NULL;
        EditStatus st;

        if (mode == MODE_EDIT) {
            op_name = "edit";
            st = edit_tag_by_path(root, op_path, op_value, err, sizeof(err));
        } else if (mode == MODE_SET) {
            op_name = "set";
            st = set_tag_by_path(root, op_path, op_value, err, sizeof(err));
        } else {
            op_name = "delete";
            st = delete_tag_by_path(root, op_path, err, sizeof(err));
        }

        if (st != EDIT_OK) {
            if (err[0] != '\0') {
                printf("Failed to %s path '%s': %s (%s)\n", op_name, op_path, err, edit_status_name(st));
            } else {
                printf("Failed to %s path '%s': %s\n", op_name, op_path, edit_status_name(st));
            }
            free(data);
            free_nbt_tree(root);
            return 1;
        }

        if (mode == MODE_DELETE) {
            printf("Deleted %s successfully\n", op_path);
        } else if (mode == MODE_SET) {
            printf("Set %s successfully\n", op_path);
        } else {
            printf("Updated %s successfully\n", op_path);
        }

        if (in_place) {
            write_path = input_path;
        } else if (output_path) {
            write_path = output_path;
        }

        if (in_place && backup_enabled) {
            backup_path = make_backup_path(input_path, backup_suffix);
            if (!backup_path) {
                fprintf(stderr, "Failed to allocate backup path\n");
                free(data);
                free_nbt_tree(root);
                return 1;
            }
            if (!copy_file(input_path, backup_path, io_err, sizeof(io_err))) {
                fprintf(stderr, "Backup creation failed: %s\n", io_err[0] ? io_err : "unknown error");
                free(backup_path);
                free(data);
                free_nbt_tree(root);
                return 1;
            }
            printf("Created backup: %s\n", backup_path);
        }

        if (!write_nbt_atomically(write_path, root, io_err, sizeof(io_err))) {
            fprintf(stderr, "Failed to save edited NBT: %s\n", io_err[0] ? io_err : "unknown error");
            free(backup_path);
            free(data);
            free_nbt_tree(root);
            return 1;
        }

        printf("Saved modified NBT to %s\n", write_path);
        free(backup_path);

    } else if (mode == MODE_DUMP) {
        // DUMP MODE
        FILE* dump_file = fopen(dump_path, "w");
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

        printf("Dumped parsed NBT to %s\n", dump_path);

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
