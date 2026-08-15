#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "cli_support.h"
#include "nbt_binary.h"
#include "platform.h"
#include "region_file.h"
#include "region_read.h"
#include "snbt.h"

static void set_err(char* err, size_t err_sz, const char* message) {
    if (err && err_sz > 0) snprintf(err, err_sz, "%s", message ? message : "unknown error");
}

unsigned char* cli_read_file(const char* path, size_t* out_size, char* err, size_t err_sz) {
    FILE* file;
    unsigned char* data;
    size_t size = 0;
    size_t capacity = 16384;

    if (out_size) *out_size = 0;
    file = nbt_fopen(path, "rb");
    if (!file) {
        if (err && err_sz > 0) snprintf(err, err_sz, "fopen(%s): %s", path, strerror(errno));
        return NULL;
    }
    data = malloc(capacity + 1);
    if (!data) {
        fclose(file);
        set_err(err, err_sz, "out of memory");
        return NULL;
    }
    while (1) {
        size_t count;
        if (size == capacity) {
            unsigned char* grown;
            if (capacity > SIZE_MAX / 2 - 1) {
                free(data);
                fclose(file);
                set_err(err, err_sz, "input is too large");
                return NULL;
            }
            capacity *= 2;
            grown = realloc(data, capacity + 1);
            if (!grown) {
                free(data);
                fclose(file);
                set_err(err, err_sz, "out of memory");
                return NULL;
            }
            data = grown;
        }
        count = fread(data + size, 1, capacity - size, file);
        size += count;
        if (count == 0) {
            if (ferror(file)) {
                free(data);
                fclose(file);
                set_err(err, err_sz, "failed to read input file");
                return NULL;
            }
            break;
        }
    }
    fclose(file);
    data[size] = '\0';
    if (out_size) *out_size = size;
    return data;
}

char* cli_append_suffix(const char* path, const char* suffix) {
    size_t path_len;
    size_t suffix_len;
    char* output;
    if (!path || !suffix) return NULL;
    path_len = strlen(path);
    suffix_len = strlen(suffix);
    if (path_len > SIZE_MAX - suffix_len - 1) return NULL;
    output = malloc(path_len + suffix_len + 1);
    if (!output) return NULL;
    memcpy(output, path, path_len);
    memcpy(output + path_len, suffix, suffix_len + 1);
    return output;
}

int cli_copy_file(const char* source, const char* destination, char* err, size_t err_sz) {
    FILE* in = nbt_fopen(source, "rb");
    FILE* out = NULL;
    unsigned char buffer[16384];
    int ok = 0;
    if (!in) {
        if (err && err_sz > 0) snprintf(err, err_sz, "fopen(%s): %s", source, strerror(errno));
        return 0;
    }
    out = nbt_fopen(destination, "wb");
    if (!out) {
        if (err && err_sz > 0) snprintf(err, err_sz, "fopen(%s): %s", destination, strerror(errno));
        fclose(in);
        return 0;
    }
    while (1) {
        size_t count = fread(buffer, 1, sizeof(buffer), in);
        if (count && fwrite(buffer, 1, count, out) != count) {
            set_err(err, err_sz, "failed to write backup file");
            break;
        }
        if (!count) {
            if (ferror(in)) set_err(err, err_sz, "failed to read file while creating backup");
            else ok = 1;
            break;
        }
    }
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    if (!ok) nbt_remove_file(destination);
    return ok;
}

int cli_backup_region_sidecars(const char* region_path, const char* suffix, char* err, size_t err_sz) {
    RegionFile* region = region_file_read(region_path, err, err_sz);
    int index;
    if (!region) return 0;
    for (index = 0; index < REGION_CHUNK_COUNT; index++) {
        RegionChunkSlot* slot = &region->chunks[index];
        int chunk_x;
        int chunk_z;
        char* sidecar;
        char* backup;
        if (!slot->present || !slot->external) continue;
        region_chunk_coords(index, &chunk_x, &chunk_z);
        sidecar = region_external_chunk_path(region_path, chunk_x, chunk_z);
        backup = sidecar ? cli_append_suffix(sidecar, suffix) : NULL;
        if (!sidecar || !backup || !cli_copy_file(sidecar, backup, err, err_sz)) {
            free(sidecar);
            free(backup);
            region_file_free(region);
            if (!err || !err[0]) set_err(err, err_sz, "failed to back up an external chunk");
            return 0;
        }
        printf("Created external chunk backup: %s\n", backup);
        free(sidecar);
        free(backup);
    }
    region_file_free(region);
    return 1;
}

static int compress_bytes(
    const unsigned char* input,
    size_t input_size,
    NBTInputFormat compression,
    unsigned char** out_data,
    size_t* out_size,
    char* err,
    size_t err_sz
) {
    z_stream stream;
    unsigned char* output;
    uLong bound;
    int result;
    int window_bits;

    if (!input || !out_data || !out_size) return 0;
    *out_data = NULL;
    *out_size = 0;
    if (compression == NBT_INPUT_FORMAT_RAW) {
        output = malloc(input_size ? input_size : 1);
        if (!output) { set_err(err, err_sz, "out of memory"); return 0; }
        if (input_size) memcpy(output, input, input_size);
        *out_data = output;
        *out_size = input_size;
        return 1;
    }
    if (compression != NBT_INPUT_FORMAT_GZIP && compression != NBT_INPUT_FORMAT_ZLIB) {
        set_err(err, err_sz, "unsupported standalone compression format");
        return 0;
    }
    if (input_size > (size_t)UINT_MAX) {
        set_err(err, err_sz, "document is too large for zlib");
        return 0;
    }
    memset(&stream, 0, sizeof(stream));
    window_bits = compression == NBT_INPUT_FORMAT_GZIP ? 15 + 16 : 15;
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, window_bits, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        set_err(err, err_sz, "failed to initialize compression");
        return 0;
    }
    bound = deflateBound(&stream, (uLong)input_size);
    if (bound > (uLong)UINT_MAX) {
        deflateEnd(&stream);
        set_err(err, err_sz, "compressed document is too large");
        return 0;
    }
    output = malloc(bound ? (size_t)bound : 1);
    if (!output) {
        deflateEnd(&stream);
        set_err(err, err_sz, "out of memory");
        return 0;
    }
    stream.next_in = (Bytef*)input;
    stream.avail_in = (uInt)input_size;
    stream.next_out = output;
    stream.avail_out = (uInt)bound;
    result = deflate(&stream, Z_FINISH);
    if (result != Z_STREAM_END) {
        free(output);
        deflateEnd(&stream);
        set_err(err, err_sz, "failed to compress NBT output");
        return 0;
    }
    *out_size = (size_t)stream.total_out;
    *out_data = output;
    deflateEnd(&stream);
    return 1;
}

static int write_bytes_atomically(
    const char* target_path,
    const unsigned char* data,
    size_t size,
    char* err,
    size_t err_sz
) {
    char* temporary_path = NULL;
    FILE* output;
    int descriptor = nbt_open_temp_file(target_path, "nbt", &temporary_path, err, err_sz);
    int ok = 0;
    if (descriptor < 0) return 0;
    if (nbt_close_fd(descriptor) != 0) {
        nbt_remove_file(temporary_path);
        free(temporary_path);
        set_err(err, err_sz, "failed to close temporary file descriptor");
        return 0;
    }
    output = nbt_fopen(temporary_path, "wb");
    if (!output) {
        if (err && err_sz > 0) snprintf(err, err_sz, "fopen(%s): %s", temporary_path, strerror(errno));
        nbt_remove_file(temporary_path);
        free(temporary_path);
        return 0;
    }
    if (!size || fwrite(data, 1, size, output) == size) ok = 1;
    else set_err(err, err_sz, "failed to write temporary output file");
    if (fclose(output) != 0) { ok = 0; set_err(err, err_sz, "failed to finish output file"); }
    if (ok) ok = nbt_replace_file(temporary_path, target_path, err, err_sz);
    if (!ok) nbt_remove_file(temporary_path);
    free(temporary_path);
    return ok;
}

int cli_write_binary_document(
    const char* path,
    const NBTTag* root,
    const NBTBinaryInfo* binary_info,
    NBTInputFormat compression,
    char* err,
    size_t err_sz
) {
    unsigned char* raw = NULL;
    unsigned char* encoded = NULL;
    size_t raw_size = 0;
    size_t encoded_size = 0;
    NBTBinaryFormat format = binary_info ? binary_info->format : NBT_BINARY_JAVA;
    uint32_t storage_version = binary_info ? binary_info->bedrock_storage_version : 0;
    int ok;
    if (format == NBT_BINARY_AUTO) format = NBT_BINARY_JAVA;
    if (!nbt_binary_serialize(root, format, storage_version, &raw, &raw_size, err, err_sz)) return 0;
    if (!compress_bytes(raw, raw_size, compression, &encoded, &encoded_size, err, err_sz)) {
        free(raw);
        return 0;
    }
    ok = write_bytes_atomically(path, encoded, encoded_size, err, err_sz);
    free(raw);
    free(encoded);
    return ok;
}

int cli_write_snbt_document(const char* path, const NBTTag* root, char* err, size_t err_sz) {
    char* text = snbt_serialize(root, 1, err, err_sz);
    int ok;
    if (!text) return 0;
    ok = write_bytes_atomically(path, (const unsigned char*)text, strlen(text), err, err_sz);
    free(text);
    return ok;
}

int cli_dump_tree(const char* path, const NBTTag* root, char* err, size_t err_sz) {
    FILE* dump = nbt_fopen(path, "w");
    int saved_stdout;
    int result = 0;
    if (!dump) {
        if (err && err_sz > 0) snprintf(err, err_sz, "fopen(%s): %s", path, strerror(errno));
        return 0;
    }
    fflush(stdout);
    saved_stdout = nbt_dup_fd(nbt_fileno(stdout));
    if (saved_stdout < 0 || nbt_dup2_fd(nbt_fileno(dump), nbt_fileno(stdout)) < 0) {
        if (saved_stdout >= 0) nbt_close_fd(saved_stdout);
        fclose(dump);
        set_err(err, err_sz, "failed to redirect output");
        return 0;
    }
    parse_nbt(root, 0);
    fflush(stdout);
    if (nbt_dup2_fd(saved_stdout, nbt_fileno(stdout)) >= 0) result = 1;
    else set_err(err, err_sz, "failed to restore standard output");
    nbt_close_fd(saved_stdout);
    if (fclose(dump) != 0) result = 0;
    return result;
}

int cli_list_region_chunks(const char* path, char* err, size_t err_sz) {
    RegionFile* region = region_file_read(path, err, err_sz);
    int count = 0;
    int index;
    if (!region) return 0;
    printf("local_x\tlocal_z\tcompression\tstorage\tbytes\ttimestamp\n");
    for (index = 0; index < REGION_CHUNK_COUNT; index++) {
        RegionChunkSlot* slot = &region->chunks[index];
        int x;
        int z;
        const char* compression;
        if (!slot->present) continue;
        region_chunk_coords(index, &x, &z);
        switch (slot->compression_type) {
            case REGION_COMPRESSION_GZIP: compression = "gzip"; break;
            case REGION_COMPRESSION_ZLIB: compression = "zlib"; break;
            case REGION_COMPRESSION_NONE: compression = "raw"; break;
            case REGION_COMPRESSION_LZ4: compression = "lz4"; break;
            default: compression = "unknown"; break;
        }
        printf("%d\t%d\t%s\t%s\t%zu\t%u\n", x, z, compression,
               slot->external ? "external" : "inline", slot->payload_size, slot->timestamp);
        count++;
    }
    printf("%d populated chunk%s\n", count, count == 1 ? "" : "s");
    region_file_free(region);
    return 1;
}
