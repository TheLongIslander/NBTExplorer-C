#ifndef NBT_JSON_H
#define NBT_JSON_H

#include <stddef.h>
#include <stdio.h>

#include "nbt_parser.h"

/*
 * Writes a lossless, typed JSON representation intended for frontends and
 * automation. Long integers remain JSON integers; callers must use a JSON
 * implementation that preserves 64-bit values when editing them.
 */
int nbt_write_typed_json(
    FILE* out,
    const NBTTag* root,
    int pretty,
    char* err,
    size_t err_sz
);

int nbt_write_typed_json_file(
    const char* path,
    const NBTTag* root,
    int pretty,
    char* err,
    size_t err_sz
);

const char* nbt_tag_type_name(TagType type);

#endif
