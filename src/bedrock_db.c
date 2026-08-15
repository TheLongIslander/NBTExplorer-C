#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bedrock_db.h"
#include "nbt_binary.h"
#include "nbt_builder.h"

#ifdef NBT_EXPLORER_BUNDLED_LEVELDB
#include <leveldb/c.h>
typedef int BedrockLibrary;
#else
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef HMODULE BedrockLibrary;
#else
#include <dlfcn.h>
typedef void* BedrockLibrary;
#endif

typedef struct leveldb_t leveldb_t;
typedef struct leveldb_iterator_t leveldb_iterator_t;
typedef struct leveldb_options_t leveldb_options_t;
typedef struct leveldb_readoptions_t leveldb_readoptions_t;
typedef struct leveldb_writebatch_t leveldb_writebatch_t;
typedef struct leveldb_writeoptions_t leveldb_writeoptions_t;
#endif

typedef struct {
    leveldb_t* (*open)(const leveldb_options_t*, const char*, char**);
    void (*close)(leveldb_t*);
    char* (*get)(leveldb_t*, const leveldb_readoptions_t*, const char*, size_t,
                 size_t*, char**);
    void (*write)(leveldb_t*, const leveldb_writeoptions_t*,
                  leveldb_writebatch_t*, char**);

    leveldb_iterator_t* (*create_iterator)(leveldb_t*, const leveldb_readoptions_t*);
    void (*iter_destroy)(leveldb_iterator_t*);
    uint8_t (*iter_valid)(const leveldb_iterator_t*);
    void (*iter_seek_to_first)(leveldb_iterator_t*);
    void (*iter_next)(leveldb_iterator_t*);
    const char* (*iter_key)(const leveldb_iterator_t*, size_t*);
    const char* (*iter_value)(const leveldb_iterator_t*, size_t*);
    void (*iter_get_error)(const leveldb_iterator_t*, char**);

    leveldb_options_t* (*options_create)(void);
    void (*options_destroy)(leveldb_options_t*);
    void (*options_set_create_if_missing)(leveldb_options_t*, uint8_t);
    void (*options_set_error_if_exists)(leveldb_options_t*, uint8_t);
    void (*options_set_paranoid_checks)(leveldb_options_t*, uint8_t);
    void (*options_set_disable_seek_autocompaction)(leveldb_options_t*, uint8_t);
    void (*options_set_compression)(leveldb_options_t*, int);

    leveldb_readoptions_t* (*readoptions_create)(void);
    void (*readoptions_destroy)(leveldb_readoptions_t*);
    void (*readoptions_set_verify_checksums)(leveldb_readoptions_t*, uint8_t);
    void (*readoptions_set_fill_cache)(leveldb_readoptions_t*, uint8_t);

    leveldb_writeoptions_t* (*writeoptions_create)(void);
    void (*writeoptions_destroy)(leveldb_writeoptions_t*);
    void (*writeoptions_set_sync)(leveldb_writeoptions_t*, uint8_t);

    leveldb_writebatch_t* (*writebatch_create)(void);
    void (*writebatch_destroy)(leveldb_writebatch_t*);
    void (*writebatch_put)(leveldb_writebatch_t*, const char*, size_t,
                           const char*, size_t);
    void (*writebatch_delete)(leveldb_writebatch_t*, const char*, size_t);
    void (*free_memory)(void*);
} BedrockLevelDBApi;

struct BedrockDB {
    BedrockLibrary library;
    BedrockLevelDBApi api;
    leveldb_t* database;
    leveldb_options_t* options;
    leveldb_readoptions_t* read_options;
    leveldb_writeoptions_t* write_options;
    BedrockDBOpenMode mode;
};

static void set_error(char* err, size_t err_sz, const char* message) {
    if (err && err_sz > 0) snprintf(err, err_sz, "%s", message);
}

static void set_backend_error(
    BedrockDB* db,
    char* err,
    size_t err_sz,
    const char* operation,
    char* backend_error
) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s: %s", operation,
                 backend_error ? backend_error : "unknown LevelDB error");
    }
    if (backend_error && db && db->api.free_memory) db->api.free_memory(backend_error);
}

static BedrockLibrary open_library(const char* path, char* err, size_t err_sz) {
#ifdef NBT_EXPLORER_BUNDLED_LEVELDB
    (void)path;
    (void)err;
    (void)err_sz;
    return 1;
#else
#ifdef _WIN32
    BedrockLibrary library = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!library && err && err_sz > 0) {
        snprintf(err, err_sz, "could not load Bedrock LevelDB library '%s' (Windows error %lu)",
                 path, (unsigned long)GetLastError());
    }
    return library;
#else
    BedrockLibrary library;
    const char* detail;
    dlerror();
    library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    detail = library ? NULL : dlerror();
    if (!library && err && err_sz > 0) {
        snprintf(err, err_sz, "could not load Bedrock LevelDB library '%s': %s",
                 path, detail ? detail : "dynamic loader error");
    }
    return library;
#endif
#endif
}

static void close_library(BedrockLibrary library) {
    if (!library) return;
#ifdef NBT_EXPLORER_BUNDLED_LEVELDB
    (void)library;
#else
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
#endif
}

#ifndef NBT_EXPLORER_BUNDLED_LEVELDB
static void* find_symbol(BedrockLibrary library, const char* name) {
#ifdef _WIN32
    FARPROC proc = GetProcAddress(library, name);
    void* result = NULL;
    if (sizeof(proc) != sizeof(result)) return NULL;
    memcpy(&result, &proc, sizeof(result));
    return result;
#else
    return dlsym(library, name);
#endif
}

static int load_function(
    BedrockLibrary library,
    const char* name,
    void* destination,
    size_t destination_size,
    char* err,
    size_t err_sz
) {
    void* symbol = find_symbol(library, name);
    if (!symbol) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "Bedrock LevelDB library is missing required symbol %s", name);
        }
        return 0;
    }
    if (destination_size != sizeof(symbol)) {
        set_error(err, err_sz, "function pointers are incompatible with the dynamic loader on this platform");
        return 0;
    }
    memcpy(destination, &symbol, sizeof(symbol));
    return 1;
}

#define LOAD_LEVELDB_FUNCTION(api, library, field, symbol, err, err_sz) \
    load_function((library), (symbol), &(api)->field, sizeof((api)->field), (err), (err_sz))
#endif

static int load_leveldb_api(
    BedrockLibrary library,
    BedrockLevelDBApi* api,
    char* err,
    size_t err_sz
) {
    memset(api, 0, sizeof(*api));

#ifdef NBT_EXPLORER_BUNDLED_LEVELDB
    (void)library;
    (void)err;
    (void)err_sz;
    api->open = leveldb_open;
    api->close = leveldb_close;
    api->get = leveldb_get;
    api->write = leveldb_write;
    api->create_iterator = leveldb_create_iterator;
    api->iter_destroy = leveldb_iter_destroy;
    api->iter_valid = leveldb_iter_valid;
    api->iter_seek_to_first = leveldb_iter_seek_to_first;
    api->iter_next = leveldb_iter_next;
    api->iter_key = leveldb_iter_key;
    api->iter_value = leveldb_iter_value;
    api->iter_get_error = leveldb_iter_get_error;
    api->options_create = leveldb_options_create;
    api->options_destroy = leveldb_options_destroy;
    api->options_set_create_if_missing = leveldb_options_set_create_if_missing;
    api->options_set_error_if_exists = leveldb_options_set_error_if_exists;
    api->options_set_paranoid_checks = leveldb_options_set_paranoid_checks;
    api->options_set_disable_seek_autocompaction =
        leveldb_options_set_disable_seek_autocompaction;
    api->options_set_compression = leveldb_options_set_compression;
    api->readoptions_create = leveldb_readoptions_create;
    api->readoptions_destroy = leveldb_readoptions_destroy;
    api->readoptions_set_verify_checksums = leveldb_readoptions_set_verify_checksums;
    api->readoptions_set_fill_cache = leveldb_readoptions_set_fill_cache;
    api->writeoptions_create = leveldb_writeoptions_create;
    api->writeoptions_destroy = leveldb_writeoptions_destroy;
    api->writeoptions_set_sync = leveldb_writeoptions_set_sync;
    api->writebatch_create = leveldb_writebatch_create;
    api->writebatch_destroy = leveldb_writebatch_destroy;
    api->writebatch_put = leveldb_writebatch_put;
    api->writebatch_delete = leveldb_writebatch_delete;
    api->free_memory = leveldb_free;
    return 1;
#else
    /* This is a fork-specific marker. Stock Google LevelDB lacks raw-zlib support. */
    if (!LOAD_LEVELDB_FUNCTION(api, library, options_set_disable_seek_autocompaction,
                               "leveldb_options_set_disable_seek_autocompaction", err, err_sz)) {
        set_error(err, err_sz,
                  "incompatible LevelDB library: use the Amulet-Team fork with raw-zlib support");
        return 0;
    }

#define REQUIRED(field, symbol) \
    if (!LOAD_LEVELDB_FUNCTION(api, library, field, symbol, err, err_sz)) return 0
    REQUIRED(open, "leveldb_open");
    REQUIRED(close, "leveldb_close");
    REQUIRED(get, "leveldb_get");
    REQUIRED(write, "leveldb_write");
    REQUIRED(create_iterator, "leveldb_create_iterator");
    REQUIRED(iter_destroy, "leveldb_iter_destroy");
    REQUIRED(iter_valid, "leveldb_iter_valid");
    REQUIRED(iter_seek_to_first, "leveldb_iter_seek_to_first");
    REQUIRED(iter_next, "leveldb_iter_next");
    REQUIRED(iter_key, "leveldb_iter_key");
    REQUIRED(iter_value, "leveldb_iter_value");
    REQUIRED(iter_get_error, "leveldb_iter_get_error");
    REQUIRED(options_create, "leveldb_options_create");
    REQUIRED(options_destroy, "leveldb_options_destroy");
    REQUIRED(options_set_create_if_missing, "leveldb_options_set_create_if_missing");
    REQUIRED(options_set_error_if_exists, "leveldb_options_set_error_if_exists");
    REQUIRED(options_set_paranoid_checks, "leveldb_options_set_paranoid_checks");
    REQUIRED(options_set_compression, "leveldb_options_set_compression");
    REQUIRED(readoptions_create, "leveldb_readoptions_create");
    REQUIRED(readoptions_destroy, "leveldb_readoptions_destroy");
    REQUIRED(readoptions_set_verify_checksums, "leveldb_readoptions_set_verify_checksums");
    REQUIRED(readoptions_set_fill_cache, "leveldb_readoptions_set_fill_cache");
    REQUIRED(writeoptions_create, "leveldb_writeoptions_create");
    REQUIRED(writeoptions_destroy, "leveldb_writeoptions_destroy");
    REQUIRED(writeoptions_set_sync, "leveldb_writeoptions_set_sync");
    REQUIRED(writebatch_create, "leveldb_writebatch_create");
    REQUIRED(writebatch_destroy, "leveldb_writebatch_destroy");
    REQUIRED(writebatch_put, "leveldb_writebatch_put");
    REQUIRED(writebatch_delete, "leveldb_writebatch_delete");
    REQUIRED(free_memory, "leveldb_free");
#undef REQUIRED
    return 1;
#endif
}

BedrockDB* bedrock_db_open(
    const char* db_directory,
    BedrockDBOpenMode mode,
    const char* library_path,
    char* err,
    size_t err_sz
) {
    BedrockDB* db;
    char* backend_error = NULL;
    if (err && err_sz > 0) err[0] = '\0';
    if (!db_directory || db_directory[0] == '\0') {
        set_error(err, err_sz, "Bedrock LevelDB directory is missing");
        return NULL;
    }
    if (mode != BEDROCK_DB_LOGICAL_READ_ONLY && mode != BEDROCK_DB_READ_WRITE) {
        set_error(err, err_sz, "invalid Bedrock LevelDB open mode");
        return NULL;
    }
#ifndef NBT_EXPLORER_BUNDLED_LEVELDB
    if (!library_path || library_path[0] == '\0') {
        library_path = getenv("NBT_EXPLORER_LEVELDB_LIBRARY");
    }
    if (!library_path || library_path[0] == '\0') {
        set_error(err, err_sz,
                  "Bedrock database support needs the Amulet-Team LevelDB shared library; pass its path or set NBT_EXPLORER_LEVELDB_LIBRARY");
        return NULL;
    }
#else
    (void)library_path;
#endif

    db = calloc(1, sizeof(*db));
    if (!db) {
        set_error(err, err_sz, "out of memory while opening Bedrock LevelDB");
        return NULL;
    }
    db->mode = mode;
    db->library = open_library(library_path, err, err_sz);
    if (!db->library) goto fail;
    if (!load_leveldb_api(db->library, &db->api, err, err_sz)) goto fail;

    db->options = db->api.options_create();
    db->read_options = db->api.readoptions_create();
    db->write_options = db->api.writeoptions_create();
    if (!db->options || !db->read_options || !db->write_options) {
        set_error(err, err_sz, "Bedrock LevelDB library failed to allocate options");
        goto fail;
    }

    db->api.options_set_create_if_missing(db->options, 0);
    db->api.options_set_error_if_exists(db->options, 0);
    db->api.options_set_paranoid_checks(db->options, 1);
    db->api.options_set_disable_seek_autocompaction(db->options, 0);
    db->api.options_set_compression(db->options, 4); /* kZlibRawCompression */
    db->api.readoptions_set_verify_checksums(db->read_options, 1);
    db->api.readoptions_set_fill_cache(db->read_options, 1);
    db->api.writeoptions_set_sync(db->write_options, 1);

    db->database = db->api.open(db->options, db_directory, &backend_error);
    if (backend_error || !db->database) {
        set_backend_error(db, err, err_sz, "could not open Bedrock world database", backend_error);
        backend_error = NULL;
        goto fail;
    }
    return db;

fail:
    bedrock_db_close(db);
    return NULL;
}

void bedrock_db_close(BedrockDB* db) {
    BedrockLibrary library;
    if (!db) return;
    library = db->library;
    if (db->database && db->api.close) db->api.close(db->database);
    if (db->read_options && db->api.readoptions_destroy)
        db->api.readoptions_destroy(db->read_options);
    if (db->write_options && db->api.writeoptions_destroy)
        db->api.writeoptions_destroy(db->write_options);
    if (db->options && db->api.options_destroy) db->api.options_destroy(db->options);
    memset(db, 0, sizeof(*db));
    free(db);
    close_library(library);
}

int bedrock_db_is_writable(const BedrockDB* db) {
    return db && db->mode == BEDROCK_DB_READ_WRITE;
}

int bedrock_db_get(
    BedrockDB* db,
    const unsigned char* key,
    size_t key_size,
    unsigned char** out_value,
    size_t* out_value_size,
    int* out_found,
    char* err,
    size_t err_sz
) {
    char* backend_value;
    char* backend_error = NULL;
    size_t backend_size = 0;
    unsigned char* copy;
    if (out_value) *out_value = NULL;
    if (out_value_size) *out_value_size = 0;
    if (out_found) *out_found = 0;
    if (err && err_sz > 0) err[0] = '\0';
    if (!db || !db->database || !key || !out_value || !out_value_size || !out_found) {
        set_error(err, err_sz, "invalid Bedrock LevelDB get arguments");
        return 0;
    }

    backend_value = db->api.get(db->database, db->read_options,
                                (const char*)key, key_size, &backend_size,
                                &backend_error);
    if (backend_error) {
        set_backend_error(db, err, err_sz, "Bedrock LevelDB get failed", backend_error);
        return 0;
    }
    if (!backend_value) return 1;

    copy = malloc(backend_size ? backend_size : 1);
    if (!copy) {
        db->api.free_memory(backend_value);
        set_error(err, err_sz, "out of memory while copying Bedrock LevelDB value");
        return 0;
    }
    if (backend_size > 0) memcpy(copy, backend_value, backend_size);
    db->api.free_memory(backend_value);
    *out_value = copy;
    *out_value_size = backend_size;
    *out_found = 1;
    return 1;
}

int bedrock_db_iterate(
    BedrockDB* db,
    BedrockDBIterateFn callback,
    void* user_data,
    char* err,
    size_t err_sz
) {
    leveldb_iterator_t* iterator;
    char* backend_error = NULL;
    if (err && err_sz > 0) err[0] = '\0';
    if (!db || !db->database || !callback) {
        set_error(err, err_sz, "invalid Bedrock LevelDB iteration arguments");
        return 0;
    }
    iterator = db->api.create_iterator(db->database, db->read_options);
    if (!iterator) {
        set_error(err, err_sz, "Bedrock LevelDB failed to create an iterator");
        return 0;
    }
    db->api.iter_seek_to_first(iterator);
    while (db->api.iter_valid(iterator)) {
        size_t key_size = 0;
        size_t value_size = 0;
        const char* key = db->api.iter_key(iterator, &key_size);
        const char* value = db->api.iter_value(iterator, &value_size);
        if (!callback((const unsigned char*)key, key_size,
                      (const unsigned char*)value, value_size, user_data)) break;
        db->api.iter_next(iterator);
    }
    db->api.iter_get_error(iterator, &backend_error);
    db->api.iter_destroy(iterator);
    if (backend_error) {
        set_backend_error(db, err, err_sz, "Bedrock LevelDB iteration failed", backend_error);
        return 0;
    }
    return 1;
}

int bedrock_db_apply_mutations(
    BedrockDB* db,
    const BedrockDBMutation* mutations,
    size_t mutation_count,
    char* err,
    size_t err_sz
) {
    leveldb_writebatch_t* batch;
    char* backend_error = NULL;
    if (err && err_sz > 0) err[0] = '\0';
    if (!db || !db->database || (mutation_count > 0 && !mutations)) {
        set_error(err, err_sz, "invalid Bedrock LevelDB mutation arguments");
        return 0;
    }
    if (db->mode != BEDROCK_DB_READ_WRITE) {
        set_error(err, err_sz, "Bedrock LevelDB was opened in logical read-only mode");
        return 0;
    }
    if (mutation_count == 0) return 1;
    batch = db->api.writebatch_create();
    if (!batch) {
        set_error(err, err_sz, "Bedrock LevelDB failed to allocate a write batch");
        return 0;
    }
    for (size_t i = 0; i < mutation_count; ++i) {
        const BedrockDBMutation* mutation = &mutations[i];
        if (!mutation->key ||
            (mutation->type == BEDROCK_DB_PUT &&
             mutation->value_size > 0 && !mutation->value)) {
            db->api.writebatch_destroy(batch);
            set_error(err, err_sz, "invalid key or value in Bedrock LevelDB mutation batch");
            return 0;
        }
        if (mutation->type == BEDROCK_DB_PUT) {
            db->api.writebatch_put(batch, (const char*)mutation->key,
                                   mutation->key_size,
                                   (const char*)mutation->value,
                                   mutation->value_size);
        } else if (mutation->type == BEDROCK_DB_DELETE) {
            db->api.writebatch_delete(batch, (const char*)mutation->key,
                                      mutation->key_size);
        } else {
            db->api.writebatch_destroy(batch);
            set_error(err, err_sz, "invalid mutation type in Bedrock LevelDB batch");
            return 0;
        }
    }
    db->api.write(db->database, db->write_options, batch, &backend_error);
    db->api.writebatch_destroy(batch);
    if (backend_error) {
        set_backend_error(db, err, err_sz, "Bedrock LevelDB write batch failed", backend_error);
        return 0;
    }
    return 1;
}

NBTTag* bedrock_db_get_nbt(
    BedrockDB* db,
    const unsigned char* key,
    size_t key_size,
    int* out_found,
    char* err,
    size_t err_sz
) {
    unsigned char* value = NULL;
    size_t value_size = 0;
    int found = 0;
    NBTBinaryInfo info;
    NBTTag* root;
    if (out_found) *out_found = 0;
    if (!out_found) {
        set_error(err, err_sz, "invalid Bedrock NBT get arguments");
        return NULL;
    }
    if (!bedrock_db_get(db, key, key_size, &value, &value_size, &found,
                        err, err_sz)) return NULL;
    *out_found = found;
    if (!found) return NULL;
    root = nbt_binary_parse(value, value_size, NBT_BINARY_BEDROCK,
                            &info, err, err_sz);
    free(value);
    if (!root) return NULL;
    if (info.bytes_consumed != value_size) {
        free_nbt_tree(root);
        set_error(err, err_sz,
                  "Bedrock database value is not exactly one little-endian NBT root");
        return NULL;
    }
    return root;
}

int bedrock_db_put_nbt(
    BedrockDB* db,
    const unsigned char* key,
    size_t key_size,
    const NBTTag* root,
    char* err,
    size_t err_sz
) {
    unsigned char* value = NULL;
    size_t value_size = 0;
    BedrockDBMutation mutation;
    int result;
    if (!root) {
        set_error(err, err_sz, "Bedrock NBT root is null");
        return 0;
    }
    if (!nbt_binary_serialize(root, NBT_BINARY_BEDROCK, 0,
                              &value, &value_size, err, err_sz)) return 0;
    mutation.type = BEDROCK_DB_PUT;
    mutation.key = key;
    mutation.key_size = key_size;
    mutation.value = value;
    mutation.value_size = value_size;
    result = bedrock_db_apply_mutations(db, &mutation, 1, err, err_sz);
    free(value);
    return result;
}
