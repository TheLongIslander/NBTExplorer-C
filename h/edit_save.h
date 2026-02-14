#ifndef EDIT_SAVE_H
#define EDIT_SAVE_H

#include <stddef.h>
#include <zlib.h>
#include "nbt_parser.h"

typedef enum {
    EDIT_OK = 0,
    EDIT_ERR_PATH_SYNTAX,
    EDIT_ERR_PATH_NOT_FOUND,
    EDIT_ERR_INDEX_BOUNDS,
    EDIT_ERR_TYPE_MISMATCH,
    EDIT_ERR_INVALID_JSON,
    EDIT_ERR_NUMERIC_RANGE,
    EDIT_ERR_UNSUPPORTED,
    EDIT_ERR_MEMORY
} EditStatus;

void write_tag(gzFile f, NBTTag* tag);
int serialize_tag_to_nbt_bytes(const NBTTag* tag, unsigned char** out_data, size_t* out_size, char* err, size_t err_sz);
EditStatus edit_tag_by_path(NBTTag* root, const char* path, const char* value_expr, char* err, size_t err_sz);
EditStatus set_tag_by_path(NBTTag* root, const char* path, const char* value_expr, char* err, size_t err_sz);
EditStatus delete_tag_by_path(NBTTag* root, const char* path, char* err, size_t err_sz);
const char* edit_status_name(EditStatus status);

/* Backward-compat utility: returns only direct tag targets. */
NBTTag* find_tag_by_path(NBTTag* root, const char* path);

#endif
