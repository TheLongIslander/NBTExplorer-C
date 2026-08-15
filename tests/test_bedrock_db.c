#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "bedrock_db.h"
#include "nbt_builder.h"
#include "snbt.h"

#ifdef NBT_EXPLORER_TEST_BUNDLED_LEVELDB
#include <leveldb/c.h>
#endif

static int failures = 0;

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", (message), __LINE__); \
        ++failures; \
    } \
} while (0)

static int count_records(
    const unsigned char* key,
    size_t key_size,
    const unsigned char* value,
    size_t value_size,
    void* user_data
) {
    size_t* count = user_data;
    (void)key;
    (void)key_size;
    (void)value;
    (void)value_size;
    ++*count;
    return 1;
}

#ifdef NBT_EXPLORER_TEST_BUNDLED_LEVELDB
static unsigned long process_id(void) {
#ifdef _WIN32
    return (unsigned long)_getpid();
#else
    return (unsigned long)getpid();
#endif
}

static int create_fixture(const char* path, char* err, size_t err_sz) {
    leveldb_options_t* options = leveldb_options_create();
    leveldb_t* database;
    char* backend_error = NULL;
    if (!options) return 0;
    leveldb_options_set_create_if_missing(options, 1);
    leveldb_options_set_compression(options, leveldb_zlib_raw_compression);
    database = leveldb_open(options, path, &backend_error);
    if (backend_error || !database) {
        snprintf(err, err_sz, "fixture create failed: %s",
                 backend_error ? backend_error : "unknown LevelDB error");
        if (backend_error) leveldb_free(backend_error);
        leveldb_options_destroy(options);
        return 0;
    }
    leveldb_close(database);
    leveldb_options_destroy(options);
    return 1;
}

static int destroy_fixture(const char* path, char* err, size_t err_sz) {
    leveldb_options_t* options = leveldb_options_create();
    char* backend_error = NULL;
    if (!options) return 0;
    leveldb_destroy_db(options, path, &backend_error);
    leveldb_options_destroy(options);
    if (backend_error) {
        snprintf(err, err_sz, "fixture cleanup failed: %s", backend_error);
        leveldb_free(backend_error);
        return 0;
    }
    return 1;
}
#endif

int main(int argc, char** argv) {
    static const unsigned char nbt_key[] = "cnbt:test:nbt";
    static const unsigned char raw_key_a[] = "cnbt:test:raw:a";
    static const unsigned char raw_key_b[] = "cnbt:test:raw:b";
    static const unsigned char large_key[] = "cnbt:test:raw:large";
    static const unsigned char flush_key[] = "cnbt:test:raw:flush";
    static const unsigned char raw_value_a[] = {0, 1, 2, 0, 255};
    static const unsigned char raw_value_b[] = "temporary";
    BedrockDB* db;
    NBTTag* source;
    NBTTag* loaded;
    char* source_text;
    char* loaded_text;
    unsigned char* raw = NULL;
    unsigned char* large_value = NULL;
    size_t raw_size = 0;
    size_t record_count = 0;
    int found = 0;
    char err[512] = {0};
    BedrockDBMutation mutations[3];
    const char* library_path;
    const char* database_path;
#ifdef NBT_EXPLORER_TEST_BUNDLED_LEVELDB
    char unique_database_path[4096];
#endif

#ifdef NBT_EXPLORER_TEST_BUNDLED_LEVELDB
    if (argc != 2) {
        fprintf(stderr, "Usage: %s /path/to/disposable/test/db\n", argv[0]);
        return 2;
    }
    library_path = NULL;
    if (snprintf(unique_database_path, sizeof(unique_database_path), "%s.%lu",
                 argv[1], process_id()) < 0 ||
        strlen(unique_database_path) >= sizeof(unique_database_path) - 1) {
        fputs("Bedrock LevelDB test fixture path is too long\n", stderr);
        return 2;
    }
    database_path = unique_database_path;
    CHECK(create_fixture(database_path, err, sizeof(err)), err);
    if (failures) return 1;
#else
    if (argc != 3) {
        fprintf(stderr, "Usage: %s /path/to/Amulet/libleveldb /path/to/test/db\n", argv[0]);
        return 2;
    }
    library_path = argv[1];
    database_path = argv[2];
#endif

    db = bedrock_db_open(database_path, BEDROCK_DB_READ_WRITE,
                         library_path, err, sizeof(err));
    CHECK(db != NULL, err);
    if (!db) return 1;
    CHECK(bedrock_db_is_writable(db), "read-write database reports read-only");

    source = snbt_parse("{name:\"LevelDB integration\",value:42,bytes:[B;-1b,0b,1b]}",
                        "", err, sizeof(err));
    CHECK(source != NULL, err);
    if (!source) {
        bedrock_db_close(db);
        return 1;
    }
    CHECK(bedrock_db_put_nbt(db, nbt_key, sizeof(nbt_key) - 1,
                             source, err, sizeof(err)), err);
    loaded = bedrock_db_get_nbt(db, nbt_key, sizeof(nbt_key) - 1,
                                &found, err, sizeof(err));
    CHECK(found && loaded, err);
    source_text = snbt_serialize(source, 0, err, sizeof(err));
    loaded_text = loaded ? snbt_serialize(loaded, 0, err, sizeof(err)) : NULL;
    CHECK(source_text && loaded_text && strcmp(source_text, loaded_text) == 0,
          "little-endian NBT changed during database round trip");
    free(source_text);
    free(loaded_text);
    free_nbt_tree(source);
    free_nbt_tree(loaded);

    mutations[0] = (BedrockDBMutation){
        BEDROCK_DB_PUT, raw_key_a, sizeof(raw_key_a) - 1,
        raw_value_a, sizeof(raw_value_a)
    };
    mutations[1] = (BedrockDBMutation){
        BEDROCK_DB_PUT, raw_key_b, sizeof(raw_key_b) - 1,
        raw_value_b, sizeof(raw_value_b) - 1
    };
    mutations[2] = (BedrockDBMutation){
        BEDROCK_DB_DELETE, raw_key_b, sizeof(raw_key_b) - 1, NULL, 0
    };
    CHECK(bedrock_db_apply_mutations(db, mutations, 3, err, sizeof(err)), err);
    CHECK(bedrock_db_get(db, raw_key_a, sizeof(raw_key_a) - 1,
                         &raw, &raw_size, &found, err, sizeof(err)), err);
    CHECK(found && raw_size == sizeof(raw_value_a) &&
          memcmp(raw, raw_value_a, sizeof(raw_value_a)) == 0,
          "binary LevelDB value changed during round trip");
    free(raw);
    raw = NULL;
    CHECK(bedrock_db_get(db, raw_key_b, sizeof(raw_key_b) - 1,
                         &raw, &raw_size, &found, err, sizeof(err)), err);
    CHECK(!found, "batched LevelDB deletion did not take effect");
    free(raw);
    CHECK(bedrock_db_iterate(db, count_records, &record_count, err, sizeof(err)), err);
    CHECK(record_count >= 2, "LevelDB iteration did not return inserted records");

    /* Exceed the default memtable, then write again so the test covers an SST
       block encoded with Bedrock's raw-zlib compressor rather than only WAL IO. */
    large_value = malloc(5u * 1024u * 1024u);
    CHECK(large_value != NULL, "could not allocate raw-zlib integration fixture");
    if (large_value) {
        for (size_t i = 0; i < 5u * 1024u * 1024u; ++i)
            large_value[i] = (unsigned char)(i % 251u);
        mutations[0] = (BedrockDBMutation){
            BEDROCK_DB_PUT, large_key, sizeof(large_key) - 1,
            large_value, 5u * 1024u * 1024u
        };
        mutations[1] = (BedrockDBMutation){
            BEDROCK_DB_PUT, flush_key, sizeof(flush_key) - 1,
            raw_value_b, sizeof(raw_value_b) - 1
        };
        CHECK(bedrock_db_apply_mutations(db, mutations, 2, err, sizeof(err)), err);
    }
    bedrock_db_close(db);

    db = bedrock_db_open(database_path, BEDROCK_DB_LOGICAL_READ_ONLY,
                         library_path, err, sizeof(err));
    CHECK(db != NULL, err);
    if (db) {
        CHECK(!bedrock_db_is_writable(db), "logical read-only database reports writable");
        if (large_value) {
            CHECK(bedrock_db_get(db, large_key, sizeof(large_key) - 1,
                                 &raw, &raw_size, &found, err, sizeof(err)), err);
            CHECK(found && raw_size == 5u * 1024u * 1024u &&
                  memcmp(raw, large_value, raw_size) == 0,
                  "raw-zlib SST value changed after database reopen");
            free(raw);
            raw = NULL;
        }
        CHECK(!bedrock_db_apply_mutations(db, mutations, 1, err, sizeof(err)),
              "logical read-only database accepted a write");
        bedrock_db_close(db);
    }
    free(large_value);

#ifdef NBT_EXPLORER_TEST_BUNDLED_LEVELDB
    CHECK(destroy_fixture(database_path, err, sizeof(err)), err);
#endif

    if (failures) {
        fprintf(stderr, "%d Bedrock LevelDB integration test(s) failed\n", failures);
        return 1;
    }
    puts("Bedrock LevelDB integration tests passed");
    return 0;
}
