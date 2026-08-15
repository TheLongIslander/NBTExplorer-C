#ifndef CNBT_CLI_SUPPORT_H
#define CNBT_CLI_SUPPORT_H

#include <stddef.h>

#include "nbt_binary.h"
#include "nbt_io.h"
#include "nbt_parser.h"

unsigned char* cli_read_file(const char* path, size_t* out_size, char* err, size_t err_sz);
char* cli_append_suffix(const char* path, const char* suffix);
int cli_copy_file(const char* source, const char* destination, char* err, size_t err_sz);
int cli_backup_region_sidecars(const char* region_path, const char* suffix, char* err, size_t err_sz);

int cli_write_binary_document(
    const char* path,
    const NBTTag* root,
    const NBTBinaryInfo* binary_info,
    NBTInputFormat compression,
    char* err,
    size_t err_sz
);

int cli_write_snbt_document(const char* path, const NBTTag* root, char* err, size_t err_sz);
int cli_dump_tree(const char* path, const NBTTag* root, char* err, size_t err_sz);
int cli_list_region_chunks(const char* path, char* err, size_t err_sz);

#endif
