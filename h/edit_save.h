#ifndef EDIT_SAVE_H
#define EDIT_SAVE_H

#include <stdio.h>
#include "nbt_parser.h"
#include <zlib.h>
void write_tag(gzFile f, NBTTag* tag);
NBTTag* find_tag_by_path(NBTTag* root, char* path);


#endif
