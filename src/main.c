#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cli_support.h"
#include "edit_save.h"
#include "nbt_binary.h"
#include "nbt_builder.h"
#include "nbt_io.h"
#include "nbt_json.h"
#include "nbt_parser.h"
#include "region_file.h"
#include "region_read.h"
#include "region_write.h"
#include "snbt.h"

#ifndef CNBT_VERSION
#define CNBT_VERSION "development"
#endif

typedef enum {
    MODE_PRINT = 0,
    MODE_EDIT,
    MODE_SET,
    MODE_DELETE,
    MODE_RENAME,
    MODE_DUMP,
    MODE_JSON,
    MODE_SNBT,
    MODE_LIST_CHUNKS,
    MODE_VALIDATE
} CliMode;

typedef enum {
    INPUT_AUTO = 0,
    INPUT_JAVA,
    INPUT_BEDROCK,
    INPUT_BEDROCK_LEVEL,
    INPUT_SNBT
} CliInputMode;

static void print_usage(const char* program) {
    printf("C-NBT Explorer %s\n\n", CNBT_VERSION);
    printf("Usage:\n");
    printf("  %s <file> [--chunk x z] [--format auto|java|bedrock|bedrock-level|snbt]\n", program);
    printf("  %s <file> [--chunk x z] --dump output.txt\n", program);
    printf("  %s <file> [--chunk x z] --json output.json\n", program);
    printf("  %s <file> [--chunk x z] --snbt output.snbt\n", program);
    printf("  %s <region.mca|region.mcr> --list-chunks\n", program);
    printf("  %s <file> --validate\n", program);
    printf("  %s <file> [--chunk x z] --edit path jsonValue [save options]\n", program);
    printf("  %s <file> [--chunk x z] --set path jsonValue [save options]\n", program);
    printf("  %s <file> [--chunk x z] --delete path [save options]\n", program);
    printf("  %s <file> [--chunk x z] --rename path newName [save options]\n", program);
    printf("\nSave options:\n");
    printf("  --output path       Write a new file.\n");
    printf("  --in-place         Atomically replace the input.\n");
    printf("  --backup[=suffix]  Back up an in-place edit (default: .bak).\n");
    printf("\nRegion coordinates are local (0..31). Input encoding and compression are preserved.\n");
}

static int parse_int_arg(const char* text, int* output) {
    char* end = NULL;
    long value;
    if (!text || !output) return 0;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno || end == text || *end || value < INT_MIN || value > INT_MAX) return 0;
    *output = (int)value;
    return 1;
}

static int has_extension(const char* path, const char* extension) {
    const char* dot;
    size_t index;
    if (!path || !extension || !(dot = strrchr(path, '.'))) return 0;
    for (index = 0; dot[index] && extension[index]; index++) {
        char a = dot[index];
        char b = extension[index];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return !dot[index] && !extension[index];
}

static int parse_input_mode(const char* text, CliInputMode* mode) {
    if (!strcmp(text, "auto")) *mode = INPUT_AUTO;
    else if (!strcmp(text, "java")) *mode = INPUT_JAVA;
    else if (!strcmp(text, "bedrock")) *mode = INPUT_BEDROCK;
    else if (!strcmp(text, "bedrock-level")) *mode = INPUT_BEDROCK_LEVEL;
    else if (!strcmp(text, "snbt")) *mode = INPUT_SNBT;
    else return 0;
    return 1;
}

static int is_mutation(CliMode mode) {
    return mode == MODE_EDIT || mode == MODE_SET || mode == MODE_DELETE || mode == MODE_RENAME;
}

int main(int argc, char* argv[]) {
    CliMode mode = MODE_PRINT;
    CliInputMode input_mode = INPUT_AUTO;
    const char* input_path;
    const char* operation_path = NULL;
    const char* operation_value = NULL;
    const char* result_path = NULL;
    const char* output_path = NULL;
    const char* backup_suffix = ".bak";
    int operation_seen = 0;
    int in_place = 0;
    int backup_enabled = 0;
    NBTLoadOptions load_options = {0};
    NBTLoadInfo load_info = {0};
    NBTBinaryInfo binary_info = {0};
    unsigned char* data = NULL;
    size_t data_size = 0;
    NBTTag* root = NULL;
    char error[512] = {0};
    clock_t started;
    double elapsed_ms = 0.0;
    int source_is_snbt;
    int exit_code = 1;
    int index;

    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        print_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        printf("C-NBT Explorer %s\n", CNBT_VERSION);
        return 0;
    }
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    input_path = argv[1];

#define CHOOSE_MODE(value) \
    do { \
        if (operation_seen) { print_usage(argv[0]); return 1; } \
        operation_seen = 1; \
        mode = (value); \
    } while (0)

    for (index = 2; index < argc; index++) {
        const char* argument = argv[index];
        if (!strcmp(argument, "--edit") || !strcmp(argument, "--set") || !strcmp(argument, "--rename")) {
            if (index + 2 >= argc) { print_usage(argv[0]); return 1; }
            CHOOSE_MODE(!strcmp(argument, "--edit") ? MODE_EDIT :
                        !strcmp(argument, "--set") ? MODE_SET : MODE_RENAME);
            operation_path = argv[++index];
            operation_value = argv[++index];
        } else if (!strcmp(argument, "--delete")) {
            if (index + 1 >= argc) { print_usage(argv[0]); return 1; }
            CHOOSE_MODE(MODE_DELETE);
            operation_path = argv[++index];
        } else if (!strcmp(argument, "--dump") || !strcmp(argument, "--json") || !strcmp(argument, "--snbt")) {
            if (index + 1 >= argc) { print_usage(argv[0]); return 1; }
            CHOOSE_MODE(!strcmp(argument, "--dump") ? MODE_DUMP :
                        !strcmp(argument, "--json") ? MODE_JSON : MODE_SNBT);
            result_path = argv[++index];
        } else if (!strcmp(argument, "--list-chunks")) {
            CHOOSE_MODE(MODE_LIST_CHUNKS);
        } else if (!strcmp(argument, "--validate")) {
            CHOOSE_MODE(MODE_VALIDATE);
        } else if (!strcmp(argument, "--output")) {
            if (index + 1 >= argc) { print_usage(argv[0]); return 1; }
            output_path = argv[++index];
        } else if (!strcmp(argument, "--in-place")) {
            in_place = 1;
        } else if (!strcmp(argument, "--backup")) {
            backup_enabled = 1;
            if (index + 1 < argc && argv[index + 1][0] != '-') backup_suffix = argv[++index];
        } else if (!strncmp(argument, "--backup=", 9)) {
            backup_enabled = 1;
            backup_suffix = argument + 9;
            if (!*backup_suffix) { fprintf(stderr, "Backup suffix cannot be empty\n"); return 1; }
        } else if (!strcmp(argument, "--chunk")) {
            int x;
            int z;
            if (load_options.has_chunk_coords || index + 2 >= argc ||
                !parse_int_arg(argv[++index], &x) || !parse_int_arg(argv[++index], &z) ||
                x < 0 || x > 31 || z < 0 || z > 31) {
                fprintf(stderr, "--chunk expects two coordinates in the range 0..31\n");
                return 1;
            }
            load_options.has_chunk_coords = 1;
            load_options.chunk_x = x;
            load_options.chunk_z = z;
        } else if (!strcmp(argument, "--format")) {
            if (index + 1 >= argc || !parse_input_mode(argv[++index], &input_mode)) {
                fprintf(stderr, "Unknown --format\n");
                return 1;
            }
        } else if (!strcmp(argument, "--help") || !strcmp(argument, "-h")) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argument);
            return 1;
        }
    }
#undef CHOOSE_MODE

    if (!is_mutation(mode) && (output_path || in_place || backup_enabled)) {
        fprintf(stderr, "Save options require an edit operation\n");
        return 1;
    }
    if (output_path && in_place) { fprintf(stderr, "Use --output or --in-place, not both\n"); return 1; }
    if (backup_enabled && !in_place) { fprintf(stderr, "--backup requires --in-place\n"); return 1; }
    if (mode == MODE_LIST_CHUNKS) {
        if (!region_path_has_extension(input_path)) {
            fprintf(stderr, "--list-chunks requires a .mca or .mcr file\n");
            return 1;
        }
        if (!cli_list_region_chunks(input_path, error, sizeof(error))) {
            fprintf(stderr, "Failed to list chunks: %s\n", error);
            return 1;
        }
        return 0;
    }

    source_is_snbt = input_mode == INPUT_SNBT ||
        (input_mode == INPUT_AUTO && has_extension(input_path, ".snbt"));
    started = clock();
    if (source_is_snbt) {
        data = cli_read_file(input_path, &data_size, error, sizeof(error));
        if (data) root = snbt_parse((const char*)data, "", error, sizeof(error));
        load_info.input_format = NBT_INPUT_FORMAT_RAW;
        load_info.source_type = NBT_SOURCE_STANDALONE;
    } else {
        NBTBinaryFormat requested = NBT_BINARY_AUTO;
        data = load_nbt_data(input_path, &data_size, &load_options, &load_info, error, sizeof(error));
        if (load_info.source_type == NBT_SOURCE_REGION_CHUNK) {
            if (input_mode != INPUT_AUTO && input_mode != INPUT_JAVA) {
                fprintf(stderr, "Region chunks require Java NBT encoding\n");
                goto done;
            }
            requested = NBT_BINARY_JAVA;
        } else if (input_mode == INPUT_JAVA) requested = NBT_BINARY_JAVA;
        else if (input_mode == INPUT_BEDROCK) requested = NBT_BINARY_BEDROCK;
        else if (input_mode == INPUT_BEDROCK_LEVEL) requested = NBT_BINARY_BEDROCK_LEVEL_DAT;
        if (data) root = nbt_binary_parse(data, data_size, requested, &binary_info, error, sizeof(error));
    }
    elapsed_ms = (double)(clock() - started) * 1000.0 / CLOCKS_PER_SEC;
    if (!data) {
        fprintf(stderr, "Failed to load file: %s\n", *error ? error : "unknown error");
        goto done;
    }
    if (!root) {
        fprintf(stderr, "Failed to parse NBT root: %s\n", *error ? error : "invalid data");
        goto done;
    }

    printf("Detected source: %s\n", nbt_source_type_name(load_info.source_type));
    printf("Detected input format: %s\n", source_is_snbt ? "snbt" : nbt_input_format_name(load_info.input_format));
    printf("Detected NBT encoding: %s\n", source_is_snbt ? "snbt" : nbt_binary_format_name(binary_info.format));
    if (load_info.source_type == NBT_SOURCE_REGION_CHUNK) {
        printf("Using region chunk (%d, %d)\n", load_info.chunk_x, load_info.chunk_z);
    }
    printf("Parsed in %.2f ms\n", elapsed_ms);
    printf("Root tag name: '%s' | type: %d\n", root->name ? root->name : "", root->type);

    if (mode == MODE_VALIDATE) {
        printf("Valid NBT document\n");
        exit_code = 0;
    } else if (mode == MODE_DUMP) {
        if (cli_dump_tree(result_path, root, error, sizeof(error))) {
            printf("Dumped parsed NBT to %s\n", result_path);
            exit_code = 0;
        }
    } else if (mode == MODE_JSON) {
        if (nbt_write_typed_json_file(result_path, root, 1, error, sizeof(error))) {
            printf("Exported typed JSON to %s\n", result_path);
            exit_code = 0;
        }
    } else if (mode == MODE_SNBT) {
        if (cli_write_snbt_document(result_path, root, error, sizeof(error))) {
            printf("Exported SNBT to %s\n", result_path);
            exit_code = 0;
        }
    } else if (is_mutation(mode)) {
        EditStatus status;
        const char* operation_name;
        const char* write_path = output_path ? output_path : in_place ? input_path : "modified_output.dat";
        int write_region = load_info.source_type == NBT_SOURCE_REGION_CHUNK &&
            (in_place || (output_path && region_path_has_extension(output_path)));

        if (load_info.source_type == NBT_SOURCE_REGION_CHUNK && in_place && !load_options.has_chunk_coords) {
            fprintf(stderr, "--in-place with a region requires explicit --chunk x z\n");
            goto done;
        }
        if (output_path && region_path_has_extension(output_path) &&
            load_info.source_type != NBT_SOURCE_REGION_CHUNK) {
            fprintf(stderr, "A region output requires a region input\n");
            goto done;
        }
        if (mode == MODE_EDIT) {
            operation_name = "edit";
            status = edit_tag_by_path(root, operation_path, operation_value, error, sizeof(error));
        } else if (mode == MODE_SET) {
            operation_name = "set";
            status = set_tag_by_path(root, operation_path, operation_value, error, sizeof(error));
        } else if (mode == MODE_DELETE) {
            operation_name = "delete";
            status = delete_tag_by_path(root, operation_path, error, sizeof(error));
        } else {
            operation_name = "rename";
            status = rename_tag_by_path(root, operation_path, operation_value, error, sizeof(error));
        }
        if (status != EDIT_OK) {
            fprintf(stderr, "Failed to %s '%s': %s (%s)\n", operation_name, operation_path,
                    *error ? error : "unknown error", edit_status_name(status));
            goto done;
        }

        if (in_place && backup_enabled) {
            char* backup_path = cli_append_suffix(input_path, backup_suffix);
            if (!backup_path || !cli_copy_file(input_path, backup_path, error, sizeof(error))) {
                fprintf(stderr, "Backup creation failed: %s\n", *error ? error : "out of memory");
                free(backup_path);
                goto done;
            }
            printf("Created backup: %s\n", backup_path);
            free(backup_path);
            if (region_path_has_extension(input_path) &&
                !cli_backup_region_sidecars(input_path, backup_suffix, error, sizeof(error))) {
                fprintf(stderr, "External chunk backup failed: %s\n", error);
                goto done;
            }
        }

        if (write_region) {
            RegionFile* region = region_file_read(input_path, error, sizeof(error));
            if (!region || !region_file_update_chunk_from_nbt(
                    region, load_info.chunk_x, load_info.chunk_z, root, -1, error, sizeof(error)) ||
                !region_file_write_atomic(region, write_path, error, sizeof(error))) {
                fprintf(stderr, "Failed to save region: %s\n", *error ? error : "unknown error");
                region_file_free(region);
                goto done;
            }
            region_file_free(region);
        } else if (has_extension(write_path, ".snbt") || (source_is_snbt && in_place)) {
            if (!cli_write_snbt_document(write_path, root, error, sizeof(error))) goto save_error;
        } else {
            NBTInputFormat compression = source_is_snbt || load_info.source_type == NBT_SOURCE_REGION_CHUNK
                ? NBT_INPUT_FORMAT_GZIP : load_info.input_format;
            NBTBinaryInfo output_info = binary_info;
            if (source_is_snbt || load_info.source_type == NBT_SOURCE_REGION_CHUNK) {
                memset(&output_info, 0, sizeof(output_info));
                output_info.format = NBT_BINARY_JAVA;
            }
            if (!cli_write_binary_document(
                    write_path, root, &output_info, compression, error, sizeof(error))) goto save_error;
        }
        printf("Saved modified NBT to %s\n", write_path);
        exit_code = 0;
        goto done;

save_error:
        fprintf(stderr, "Failed to save NBT: %s\n", *error ? error : "unknown error");
    } else {
        parse_nbt(root, 0);
        printf("Parsed and printed in %.2f ms\n", elapsed_ms);
        exit_code = 0;
    }

    if (exit_code && *error && mode != MODE_PRINT && !is_mutation(mode)) {
        fprintf(stderr, "Operation failed: %s\n", error);
    }

done:
    free(data);
    free_nbt_tree(root);
    return exit_code;
}
