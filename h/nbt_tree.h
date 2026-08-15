#ifndef NBT_TREE_H
#define NBT_TREE_H

#include "nbt_parser.h"

NBTTag* nbt_tag_create(TagType type, const char* name);
NBTTag* nbt_tag_clone(const NBTTag* source);

int nbt_compound_find_index(const NBTTag* compound, const char* name);
int nbt_compound_insert(NBTTag* compound, int index, NBTTag* child);
int nbt_compound_append(NBTTag* compound, NBTTag* child);
NBTTag* nbt_compound_take(NBTTag* compound, int index);

int nbt_list_insert(NBTTag* list, int index, NBTTag* child);
int nbt_list_append(NBTTag* list, NBTTag* child);
NBTTag* nbt_list_take(NBTTag* list, int index);

int nbt_tag_rename(NBTTag* tag, const char* new_name);

#endif
