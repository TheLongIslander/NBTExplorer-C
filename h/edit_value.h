#ifndef EDIT_VALUE_H
#define EDIT_VALUE_H

#include <stddef.h>
#include "edit_save.h"

EditStatus parse_json_for_tag_type(NBTTag* target, const char* value_expr, char* err, size_t err_sz);
EditStatus apply_json_patch_to_compound(NBTTag* compound, const char* json_object, char* err, size_t err_sz);
EditStatus parse_json_for_list_element(NBTTag* list_tag, int index, const char* value_expr, char* err, size_t err_sz);
EditStatus parse_json_for_array_element(NBTTag* array_tag, int index, const char* value_expr, char* err, size_t err_sz);
EditStatus create_tag_from_json_expr(const char* tag_name, const char* value_expr, NBTTag** out_tag, char* err, size_t err_sz);

#endif
