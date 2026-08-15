#ifndef SNBT_H
#define SNBT_H

#include <stddef.h>

#include "nbt_parser.h"

/* Parse one complete SNBT value. The returned root owns a copy of root_name. */
NBTTag* snbt_parse(
    const char* text,
    const char* root_name,
    char* err,
    size_t err_sz
);

/*
 * Serialize a tag payload as canonical SNBT. When pretty is nonzero, compounds
 * and lists are indented by two spaces. The caller owns the returned string.
 */
char* snbt_serialize(
    const NBTTag* root,
    int pretty,
    char* err,
    size_t err_sz
);

#endif
