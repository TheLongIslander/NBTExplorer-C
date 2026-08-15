#ifndef BEDROCK_DB_H
#define BEDROCK_DB_H

#include <stddef.h>

#include "nbt_parser.h"

typedef struct BedrockDB BedrockDB;

typedef enum {
    BEDROCK_DB_LOGICAL_READ_ONLY = 0,
    BEDROCK_DB_READ_WRITE = 1
} BedrockDBOpenMode;

typedef enum {
    BEDROCK_DB_PUT = 1,
    BEDROCK_DB_DELETE = 2
} BedrockDBMutationType;

typedef struct {
    BedrockDBMutationType type;
    const unsigned char* key;
    size_t key_size;
    const unsigned char* value;
    size_t value_size;
} BedrockDBMutation;

/* Return nonzero to continue iteration, or zero to stop successfully. */
typedef int (*BedrockDBIterateFn)(
    const unsigned char* key,
    size_t key_size,
    const unsigned char* value,
    size_t value_size,
    void* user_data
);

/*
 * Opens a Bedrock world's `db` directory through the Amulet-Team LevelDB fork.
 * In self-contained desktop builds, library_path is ignored because the fork
 * is linked statically. In lightweight builds it must name that fork's shared
 * library, or may be NULL to use the NBT_EXPLORER_LEVELDB_LIBRARY environment
 * variable. The adapter deliberately does not load a generic system LevelDB:
 * stock LevelDB cannot decode Bedrock's raw-zlib blocks.
 *
 * Minecraft must be closed and the world should be backed up first. LevelDB has
 * no truly read-only open API: LOGICAL_READ_ONLY blocks writes through this
 * wrapper, but opening the engine still takes its database lock and may perform
 * recovery housekeeping.
 */
BedrockDB* bedrock_db_open(
    const char* db_directory,
    BedrockDBOpenMode mode,
    const char* library_path,
    char* err,
    size_t err_sz
);

void bedrock_db_close(BedrockDB* db);
int bedrock_db_is_writable(const BedrockDB* db);

/* The caller owns *out_value. A missing key is success with *out_found == 0. */
int bedrock_db_get(
    BedrockDB* db,
    const unsigned char* key,
    size_t key_size,
    unsigned char** out_value,
    size_t* out_value_size,
    int* out_found,
    char* err,
    size_t err_sz
);

int bedrock_db_iterate(
    BedrockDB* db,
    BedrockDBIterateFn callback,
    void* user_data,
    char* err,
    size_t err_sz
);

/* Applies all mutations in one synchronous, WAL-backed LevelDB write batch. */
int bedrock_db_apply_mutations(
    BedrockDB* db,
    const BedrockDBMutation* mutations,
    size_t mutation_count,
    char* err,
    size_t err_sz
);

/* Convenience helpers for values that are exactly one little-endian NBT root. */
NBTTag* bedrock_db_get_nbt(
    BedrockDB* db,
    const unsigned char* key,
    size_t key_size,
    int* out_found,
    char* err,
    size_t err_sz
);

int bedrock_db_put_nbt(
    BedrockDB* db,
    const unsigned char* key,
    size_t key_size,
    const NBTTag* root,
    char* err,
    size_t err_sz
);

#endif
