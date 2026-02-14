#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include "io.h"
#include "nbt_builder.h"
#include "nbt_utils.h"
#include "edit_save.h"
#include "region_read.h"
#include "region_write.h"
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
    printf("  %s <file.dat|file.mca> [--chunk x z] [--edit path newValue] [--output out.dat | --in-place [--backup[=suffix]]]\n", prog);
    printf("  %s <file.dat|file.mca> [--chunk x z] [--set path newValue] [--output out.dat | --in-place [--backup[=suffix]]]\n", prog);
    printf("  %s <file.dat|file.mca> [--chunk x z] [--delete path] [--output out.dat | --in-place [--backup[=suffix]]]\n", prog);
    printf("  %s <file.dat|file.mca> [--chunk x z] [--dump output.txt]\n", prog);
    printf("  --chunk x z selects a local chunk from .mca (0..31 each). If omitted, first populated chunk is used.\n");
    printf("  For .mca edits, --output out.mca rewrites the full region safely; --in-place requires --chunk.\n");
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

static int parse_int_arg(const char* text, int* out_value) {
    char* end = NULL;
    long value;

    if (!text || !out_value) return 0;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }
    if (value < INT_MIN || value > INT_MAX) {
        return 0;
    }
    *out_value = (int)value;
    return 1;
}

static int has_mca_extension(const char* filename) {
    const char* dot;

    if (!filename) return 0;
    dot = strrchr(filename, '.');
    if (!dot) return 0;
    return (dot[1] == 'm' || dot[1] == 'M') &&
           (dot[2] == 'c' || dot[2] == 'C') &&
           (dot[3] == 'a' || dot[3] == 'A') &&
           dot[4] == '\0';
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
    NBTLoadOptions load_opts = {0};

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

        if (strcmp(arg, "--chunk") == 0) {
            int chunk_x = 0;
            int chunk_z = 0;
            if (load_opts.has_chunk_coords || i + 2 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            if (!parse_int_arg(argv[++i], &chunk_x) || !parse_int_arg(argv[++i], &chunk_z)) {
                fprintf(stderr, "Invalid --chunk coordinates (expected integers)\n");
                return 1;
            }
            if (chunk_x < 0 || chunk_x > 31 || chunk_z < 0 || chunk_z > 31) {
                fprintf(stderr, "--chunk coordinates must be in range 0..31\n");
                return 1;
            }
            load_opts.has_chunk_coords = 1;
            load_opts.chunk_x = chunk_x;
            load_opts.chunk_z = chunk_z;
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

    if ((mode == MODE_EDIT || mode == MODE_SET || mode == MODE_DELETE) &&
        output_path && has_mca_extension(output_path) && !has_mca_extension(input_path)) {
        fprintf(stderr, "--output .mca requires .mca input\n");
        return 1;
    }

    size_t size;
    char load_err[256] = {0};
    NBTLoadInfo load_info;
    unsigned char* data = load_nbt_data(input_path, &size, &load_opts, &load_info, load_err, sizeof(load_err));
    if (!data) {
        fprintf(stderr, "Failed to load file: %s\n", load_err[0] ? load_err : "unknown error");
        return 1;
    }

    if ((mode == MODE_EDIT || mode == MODE_SET || mode == MODE_DELETE) &&
        load_info.source_type == NBT_SOURCE_REGION_CHUNK && in_place && !load_opts.has_chunk_coords) {
        fprintf(stderr, "--in-place with .mca requires explicit --chunk x z\n");
        free(data);
        return 1;
    }

    printf("Detected source: %s\n", nbt_source_type_name(load_info.source_type));
    printf("Detected input format: %s\n", nbt_input_format_name(load_info.input_format));
    if (load_info.source_type == NBT_SOURCE_REGION_CHUNK) {
        printf("Using region chunk (%d, %d)\n", load_info.chunk_x, load_info.chunk_z);
    }

    size_t offset = 0;
    char parse_err[256] = {0};

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    NBTTag* root = build_nbt_tree(data, size, &offset, parse_err, sizeof(parse_err));

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                        (end.tv_nsec - start.tv_nsec) / 1e6;

    printf("Parsed in %.2f ms\n", elapsed_ms);

    if (!root) {
        fprintf(stderr, "Failed to parse NBT root: %s\n", parse_err[0] ? parse_err : "corrupt or truncated input");
        free(data);
        return 1;
    }

    if (offset < size) {
        fprintf(stderr, "Warning: trailing %zu bytes after parsed root tag\n", size - offset);
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
        int write_region = 0;
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

        if (load_info.source_type == NBT_SOURCE_REGION_CHUNK) {
            if (in_place || (output_path && has_mca_extension(output_path))) {
                write_region = 1;
            }
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

        if (write_region) {
            RegionFile* region = region_file_read(input_path, io_err, sizeof(io_err));
            if (!region) {
                fprintf(stderr, "Failed to load region for save: %s\n", io_err[0] ? io_err : "unknown error");
                free(backup_path);
                free(data);
                free_nbt_tree(root);
                return 1;
            }

            if (!region_file_update_chunk_from_nbt(region, load_info.chunk_x, load_info.chunk_z, root, -1, io_err, sizeof(io_err))) {
                fprintf(stderr, "Failed to update region chunk (%d, %d): %s\n", load_info.chunk_x, load_info.chunk_z, io_err[0] ? io_err : "unknown error");
                region_file_free(region);
                free(backup_path);
                free(data);
                free_nbt_tree(root);
                return 1;
            }

            if (!region_file_write_atomic(region, write_path, io_err, sizeof(io_err))) {
                fprintf(stderr, "Failed to save edited region: %s\n", io_err[0] ? io_err : "unknown error");
                region_file_free(region);
                free(backup_path);
                free(data);
                free_nbt_tree(root);
                return 1;
            }

            region_file_free(region);
        } else {
            if (!write_nbt_atomically(write_path, root, io_err, sizeof(io_err))) {
                fprintf(stderr, "Failed to save edited NBT: %s\n", io_err[0] ? io_err : "unknown error");
                free(backup_path);
                free(data);
                free_nbt_tree(root);
                return 1;
            }
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

        parse_nbt(root, 0); // Write parsed tree into the dump file

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
        parse_nbt(root, 0);
        printf("Parsed and printed in %.2f ms\n", elapsed_ms);
    }

    free(data);
    free_nbt_tree(root);
    return 0;
}
