#ifndef NBT_EXPLORER_PLATFORM_H
#define NBT_EXPLORER_PLATFORM_H

#include <stddef.h>
#include <stdio.h>

/* Small, explicit portability layer shared by the core, CLI, and GUI. */
char* nbt_strdup(const char* text);

/* Opens a UTF-8 path on every platform, including native Windows builds. */
FILE* nbt_fopen(const char* path, const char* mode);

/*
 * Creates a temporary file in the target file's directory and returns its
 * open descriptor. The caller owns both the descriptor and *out_path.
 */
int nbt_open_temp_file(
    const char* target_path,
    const char* prefix,
    char** out_path,
    char* err,
    size_t err_sz
);

int nbt_close_fd(int fd);
int nbt_remove_file(const char* path);

/* Replaces target_path with temp_path, including on Windows. */
int nbt_replace_file(const char* temp_path, const char* target_path, char* err, size_t err_sz);

/* File-descriptor helpers used when redirecting CLI output. */
int nbt_dup_fd(int fd);
int nbt_dup2_fd(int source_fd, int destination_fd);
int nbt_fileno(FILE* stream);

#endif
