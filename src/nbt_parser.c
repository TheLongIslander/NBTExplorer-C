#include <stdio.h>
#include "nbt_parser.h"
#include "nbt_utils.h"

static void parse_list_element(const NBTTag* elem, int indent, uint8_t elem_type);

void parse_nbt(const NBTTag* tag, int indent) {
    if (!tag) {
        print_indent(indent);
        printf("[Null tag]\n");
        return;
    }

    print_indent(indent);
    printf("Tag: %s (Type %02X)\n", tag->name ? tag->name : "", tag->type);
    indent++;

    switch (tag->type) {
        case TAG_Byte:
            print_indent(indent);
            printf("Byte: %d\n", (int)tag->value.byte_val);
            break;

        case TAG_Short:
            print_indent(indent);
            printf("Short: %d\n", (int)tag->value.short_val);
            break;

        case TAG_Int:
            print_indent(indent);
            printf("Int: %d\n", tag->value.int_val);
            break;

        case TAG_Long:
            print_indent(indent);
            printf("Long: %lld\n", (long long)tag->value.long_val);
            break;

        case TAG_Float:
            print_indent(indent);
            printf("Float: %f\n", tag->value.float_val);
            break;

        case TAG_Double:
            print_indent(indent);
            printf("Double: %lf\n", tag->value.double_val);
            break;

        case TAG_Byte_Array:
            print_indent(indent);
            printf("Byte_Array[%d]\n", tag->value.byte_array.length);
            break;

        case TAG_String:
            print_indent(indent);
            printf("String: %s\n", tag->value.string_val ? tag->value.string_val : "");
            break;

        case TAG_List:
            print_indent(indent);
            printf("List: Type %02X, Length %d\n", tag->value.list.element_type, tag->value.list.count);
            for (int i = 0; i < tag->value.list.count; i++) {
                print_indent(indent + 1);
                printf("[Element %d]\n", i);
                parse_list_element(tag->value.list.items ? tag->value.list.items[i] : NULL, indent + 2, (uint8_t)tag->value.list.element_type);
            }
            break;

        case TAG_Compound:
            for (int i = 0; i < tag->value.compound.count; i++) {
                parse_nbt(tag->value.compound.items[i], indent);
            }
            print_indent(indent);
            printf("End Compound\n");
            break;

        case TAG_Int_Array:
            print_indent(indent);
            printf("Int_Array[%d]\n", tag->value.int_array.length);
            break;

        case TAG_Long_Array:
            print_indent(indent);
            printf("Long_Array[%d]\n", tag->value.long_array.length);
            break;

        default:
            print_indent(indent);
            printf("Unknown tag type %02X\n", tag->type);
            break;
    }
}

static void parse_list_element(const NBTTag* elem, int indent, uint8_t elem_type) {
    if (!elem || elem->type != (TagType)elem_type) {
        print_indent(indent);
        printf("[Unsupported element type %02X]\n", elem_type);
        return;
    }

    switch (elem_type) {
        case TAG_Byte:
            print_indent(indent);
            printf("Byte: %d\n", (int)elem->value.byte_val);
            break;

        case TAG_Short:
            print_indent(indent);
            printf("Short: %d\n", (int)elem->value.short_val);
            break;

        case TAG_Int:
            print_indent(indent);
            printf("Int: %d\n", elem->value.int_val);
            break;

        case TAG_Long:
            print_indent(indent);
            printf("Long: %lld\n", (long long)elem->value.long_val);
            break;

        case TAG_Float:
            print_indent(indent);
            printf("Float: %f\n", elem->value.float_val);
            break;

        case TAG_Double:
            print_indent(indent);
            printf("Double: %lf\n", elem->value.double_val);
            break;

        case TAG_String:
            print_indent(indent);
            printf("String: %s\n", elem->value.string_val ? elem->value.string_val : "");
            break;

        case TAG_Byte_Array:
            print_indent(indent);
            printf("Byte_Array[%d]\n", elem->value.byte_array.length);
            break;

        case TAG_Int_Array:
            print_indent(indent);
            printf("Int_Array[%d]\n", elem->value.int_array.length);
            break;

        case TAG_Long_Array:
            print_indent(indent);
            printf("Long_Array[%d]\n", elem->value.long_array.length);
            break;

        case TAG_List:
            print_indent(indent);
            printf("List: Type %02X, Length %d\n", elem->value.list.element_type, elem->value.list.count);
            for (int i = 0; i < elem->value.list.count; i++) {
                print_indent(indent + 1);
                printf("[Element %d]\n", i);
                parse_list_element(elem->value.list.items ? elem->value.list.items[i] : NULL, indent + 2, (uint8_t)elem->value.list.element_type);
            }
            break;

        case TAG_Compound:
            for (int i = 0; i < elem->value.compound.count; i++) {
                parse_nbt(elem->value.compound.items[i], indent);
            }
            print_indent(indent);
            printf("End Compound\n");
            break;

        default:
            print_indent(indent);
            printf("[Unsupported element type %02X]\n", elem_type);
            break;
    }
}
