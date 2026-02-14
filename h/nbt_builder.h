#ifndef NBT_BUILDER_H
#define NBT_BUILDER_H

#include "nbt_parser.h"

NBTTag* build_nbt_tree(const unsigned char* data, size_t data_size, size_t* offset, char* err, size_t err_sz);
void free_nbt_tree(NBTTag* tag);

#endif
