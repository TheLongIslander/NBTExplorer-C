#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nbt_builder.h"
#include "nbt_tree.h"

static char* duplicate_text(const char* text) {
    size_t len = strlen(text ? text : "");
    char* copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, text ? text : "", len + 1);
    return copy;
}

static int duplicate_bytes(void** out, const void* source, size_t size) {
    void* copy;
    *out = NULL;
    if (size == 0) return 1;
    if (!source) return 0;
    copy = malloc(size);
    if (!copy) return 0;
    memcpy(copy, source, size);
    *out = copy;
    return 1;
}

NBTTag* nbt_tag_create(TagType type, const char* name) {
    NBTTag* tag;
    if (type < TAG_End || type > TAG_Long_Array) return NULL;

    tag = calloc(1, sizeof(*tag));
    if (!tag) return NULL;
    tag->type = type;
    tag->name = duplicate_text(name);
    if (!tag->name) {
        free(tag);
        return NULL;
    }

    if (type == TAG_String) {
        tag->value.string_val = duplicate_text("");
        if (!tag->value.string_val) {
            free(tag->name);
            free(tag);
            return NULL;
        }
    } else if (type == TAG_List) {
        tag->value.list.element_type = TAG_End;
    }
    return tag;
}

NBTTag* nbt_tag_clone(const NBTTag* source) {
    NBTTag* clone;
    int i;
    if (!source) return NULL;

    clone = nbt_tag_create(source->type, source->name);
    if (!clone) return NULL;
    clone->array_length = source->array_length;

    switch (source->type) {
        case TAG_End:
            break;
        case TAG_Byte:
            clone->value.byte_val = source->value.byte_val;
            break;
        case TAG_Short:
            clone->value.short_val = source->value.short_val;
            break;
        case TAG_Int:
            clone->value.int_val = source->value.int_val;
            break;
        case TAG_Long:
            clone->value.long_val = source->value.long_val;
            break;
        case TAG_Float:
            clone->value.float_val = source->value.float_val;
            break;
        case TAG_Double:
            clone->value.double_val = source->value.double_val;
            break;
        case TAG_String: {
            char* value = duplicate_text(source->value.string_val);
            if (!value) goto fail;
            free(clone->value.string_val);
            clone->value.string_val = value;
            break;
        }
        case TAG_Byte_Array:
            clone->value.byte_array.length = source->value.byte_array.length;
            if (source->value.byte_array.length < 0 ||
                !duplicate_bytes(
                    (void**)&clone->value.byte_array.data,
                    source->value.byte_array.data,
                    (size_t)source->value.byte_array.length
                )) goto fail;
            break;
        case TAG_Int_Array:
            clone->value.int_array.length = source->value.int_array.length;
            if (source->value.int_array.length < 0 ||
                !duplicate_bytes(
                    (void**)&clone->value.int_array.data,
                    source->value.int_array.data,
                    (size_t)source->value.int_array.length * sizeof(int32_t)
                )) goto fail;
            break;
        case TAG_Long_Array:
            clone->value.long_array.length = source->value.long_array.length;
            if (source->value.long_array.length < 0 ||
                !duplicate_bytes(
                    (void**)&clone->value.long_array.data,
                    source->value.long_array.data,
                    (size_t)source->value.long_array.length * sizeof(int64_t)
                )) goto fail;
            break;
        case TAG_List:
            clone->value.list.element_type = source->value.list.element_type;
            for (i = 0; i < source->value.list.count; i++) {
                NBTTag* child = nbt_tag_clone(source->value.list.items[i]);
                if (!child || !nbt_list_append(clone, child)) {
                    free_nbt_tree(child);
                    goto fail;
                }
            }
            break;
        case TAG_Compound:
            for (i = 0; i < source->value.compound.count; i++) {
                NBTTag* child = nbt_tag_clone(source->value.compound.items[i]);
                if (!child || !nbt_compound_append(clone, child)) {
                    free_nbt_tree(child);
                    goto fail;
                }
            }
            break;
        default:
            goto fail;
    }
    return clone;

fail:
    free_nbt_tree(clone);
    return NULL;
}

int nbt_compound_find_index(const NBTTag* compound, const char* name) {
    int i;
    if (!compound || compound->type != TAG_Compound || !name) return -1;
    for (i = 0; i < compound->value.compound.count; i++) {
        const NBTTag* child = compound->value.compound.items[i];
        if (child && child->name && strcmp(child->name, name) == 0) return i;
    }
    return -1;
}

int nbt_compound_insert(NBTTag* compound, int index, NBTTag* child) {
    int count;
    NBTTag** items;
    if (!compound || compound->type != TAG_Compound || !child) return 0;
    count = compound->value.compound.count;
    if (index < 0 || index > count) return 0;
    if (child->name && nbt_compound_find_index(compound, child->name) >= 0) return 0;

    items = realloc(compound->value.compound.items, (size_t)(count + 1) * sizeof(*items));
    if (!items) return 0;
    compound->value.compound.items = items;
    if (index < count) {
        memmove(&items[index + 1], &items[index], (size_t)(count - index) * sizeof(*items));
    }
    items[index] = child;
    compound->value.compound.count = count + 1;
    return 1;
}

int nbt_compound_append(NBTTag* compound, NBTTag* child) {
    if (!compound || compound->type != TAG_Compound) return 0;
    return nbt_compound_insert(compound, compound->value.compound.count, child);
}

NBTTag* nbt_compound_take(NBTTag* compound, int index) {
    NBTTag* child;
    int count;
    if (!compound || compound->type != TAG_Compound) return NULL;
    count = compound->value.compound.count;
    if (index < 0 || index >= count) return NULL;
    child = compound->value.compound.items[index];
    if (index < count - 1) {
        memmove(
            &compound->value.compound.items[index],
            &compound->value.compound.items[index + 1],
            (size_t)(count - index - 1) * sizeof(NBTTag*)
        );
    }
    compound->value.compound.count--;
    if (compound->value.compound.count == 0) {
        free(compound->value.compound.items);
        compound->value.compound.items = NULL;
    }
    return child;
}

int nbt_list_insert(NBTTag* list, int index, NBTTag* child) {
    int count;
    NBTTag** items;
    if (!list || list->type != TAG_List || !child) return 0;
    count = list->value.list.count;
    if (index < 0 || index > count) return 0;
    if (list->value.list.element_type == TAG_End && count == 0) {
        list->value.list.element_type = child->type;
    }
    if (child->type != list->value.list.element_type) return 0;

    items = realloc(list->value.list.items, (size_t)(count + 1) * sizeof(*items));
    if (!items) return 0;
    list->value.list.items = items;
    if (index < count) {
        memmove(&items[index + 1], &items[index], (size_t)(count - index) * sizeof(*items));
    }
    items[index] = child;
    list->value.list.count = count + 1;
    return 1;
}

int nbt_list_append(NBTTag* list, NBTTag* child) {
    if (!list || list->type != TAG_List) return 0;
    return nbt_list_insert(list, list->value.list.count, child);
}

NBTTag* nbt_list_take(NBTTag* list, int index) {
    NBTTag* child;
    int count;
    if (!list || list->type != TAG_List) return NULL;
    count = list->value.list.count;
    if (index < 0 || index >= count) return NULL;
    child = list->value.list.items[index];
    if (index < count - 1) {
        memmove(
            &list->value.list.items[index],
            &list->value.list.items[index + 1],
            (size_t)(count - index - 1) * sizeof(NBTTag*)
        );
    }
    list->value.list.count--;
    if (list->value.list.count == 0) {
        free(list->value.list.items);
        list->value.list.items = NULL;
    }
    return child;
}

int nbt_tag_rename(NBTTag* tag, const char* new_name) {
    char* replacement;
    if (!tag || !new_name) return 0;
    replacement = duplicate_text(new_name);
    if (!replacement) return 0;
    free(tag->name);
    tag->name = replacement;
    return 1;
}
