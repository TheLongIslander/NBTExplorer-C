#ifndef EDIT_PATH_H
#define EDIT_PATH_H

#include <stddef.h>
#include "edit_save.h"

typedef enum {
    PATH_TARGET_TAG = 0,
    PATH_TARGET_LIST_ELEMENT,
    PATH_TARGET_BYTE_ARRAY_ELEMENT,
    PATH_TARGET_INT_ARRAY_ELEMENT,
    PATH_TARGET_LONG_ARRAY_ELEMENT
} PathTargetKind;

typedef struct {
    PathTargetKind kind;
    NBTTag* tag;
    int index;
} PathTarget;

EditStatus resolve_edit_path(NBTTag* root, const char* path, PathTarget* out, char* err, size_t err_sz);

#endif
