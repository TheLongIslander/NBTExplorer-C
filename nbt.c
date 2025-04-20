#include <stdio.h>
#include <string.h>
#include "nbt.h"

void print_indent(int depth) {
    for (int i = 0; i < depth; i++) printf("  ");
}

uint8_t read_u8(const unsigned char* data, size_t* offset) {
    return data[(*offset)++];
}

uint16_t read_u16(const unsigned char* data, size_t* offset) {
    uint16_t val = (data[*offset] << 8) | data[*offset + 1];
    *offset += 2;
    return val;
}

int32_t read_i32(const unsigned char* data, size_t* offset) {
    int32_t val = 0;
    for (int i = 0; i < 4; i++) val = (val << 8) | data[(*offset)++];
    return val;
}

int64_t read_i64(const unsigned char* data, size_t* offset) {
    int64_t val = 0;
    for (int i = 0; i < 8; i++) val = (val << 8) | data[(*offset)++];
    return val;
}

void parse_nbt(const unsigned char* data, size_t* offset, int indent) {
    uint8_t tag_type = read_u8(data, offset);
    if (tag_type == TAG_End) {
        print_indent(indent); printf("TAG_End\n");
        return;
    }

    uint16_t name_len = read_u16(data, offset);
    char name[256] = {0};
    memcpy(name, data + *offset, name_len);
    *offset += name_len;

    print_indent(indent); printf("Tag: %s (Type %02X)\n", name, tag_type);
    indent++;

    switch (tag_type) {
        case TAG_Byte:
            print_indent(indent); printf("Byte: %d\n", (int8_t)read_u8(data, offset));
            break;
        case TAG_Short:
            print_indent(indent); printf("Short: %d\n", (int16_t)read_u16(data, offset));
            break;
        case TAG_Int:
            print_indent(indent); printf("Int: %d\n", read_i32(data, offset));
            break;
        case TAG_Long:
            print_indent(indent); printf("Long: %lld\n", (long long)read_i64(data, offset));
            break;
        case TAG_Float: {
            union { uint32_t i; float f; } u;
            u.i = read_i32(data, offset);
            print_indent(indent); printf("Float: %f\n", u.f);
            break;
        }
        case TAG_Double: {
            union { uint64_t i; double d; } u;
            u.i = read_i64(data, offset);
            print_indent(indent); printf("Double: %lf\n", u.d);
            break;
        }
        case TAG_Byte_Array: {
            int32_t len = read_i32(data, offset);
            print_indent(indent); printf("Byte_Array[%d]\n", len);
            *offset += len;
            break;
        }
        case TAG_String: {
            uint16_t len = read_u16(data, offset);
            char str[512] = {0};
            memcpy(str, data + *offset, len);
            *offset += len;
            print_indent(indent); printf("String: %s\n", str);
            break;
        }
        case TAG_List: {
            uint8_t elem_type = read_u8(data, offset);
            int32_t count = read_i32(data, offset);
            print_indent(indent); printf("List: Type %02X, Length %d\n", elem_type, count);
            for (int i = 0; i < count; i++) {
                print_indent(indent + 1); printf("[Element %d]\n", i);
        
                switch (elem_type) {
                    case TAG_Byte:
                        print_indent(indent + 2); printf("Byte: %d\n", (int8_t)read_u8(data, offset));
                        break;
                    case TAG_Short:
                        print_indent(indent + 2); printf("Short: %d\n", (int16_t)read_u16(data, offset));
                        break;
                    case TAG_Int:
                        print_indent(indent + 2); printf("Int: %d\n", read_i32(data, offset));
                        break;
                    case TAG_Long:
                        print_indent(indent + 2); printf("Long: %lld\n", (long long)read_i64(data, offset));
                        break;
                    case TAG_Float: {
                        union { uint32_t i; float f; } u;
                        u.i = read_i32(data, offset);
                        print_indent(indent + 2); printf("Float: %f\n", u.f);
                        break;
                    }
                    case TAG_Double: {
                        union { uint64_t i; double d; } u;
                        u.i = read_i64(data, offset);
                        print_indent(indent + 2); printf("Double: %lf\n", u.d);
                        break;
                    }
                    case TAG_String: {
                        uint16_t len = read_u16(data, offset);
                        char str[512] = {0};
                        memcpy(str, data + *offset, len);
                        *offset += len;
                        print_indent(indent + 2); printf("String: %s\n", str);
                        break;
                    }
                    case TAG_Compound:
                        parse_nbt(data, offset, indent + 2); // recursive
                        break;
                    default:
                        print_indent(indent + 2); printf("[Unsupported element type %02X]\n", elem_type);
                        return;
                }
            }
            break;
        }        
        case TAG_Compound:
        while (1) {
            uint8_t t = data[*offset];
            if (t == TAG_End) {
                read_u8(data, offset); // skip end byte
                print_indent(indent); printf("End Compound\n");
                break;
            }
            parse_nbt(data, offset, indent);
        }
            break;
        case TAG_Int_Array: {
            int32_t len = read_i32(data, offset);
            print_indent(indent); printf("Int_Array[%d]\n", len);
            *offset += len * 4;
            break;
        }
        case TAG_Long_Array: {
            int32_t len = read_i32(data, offset);
            print_indent(indent); printf("Long_Array[%d]\n", len);
            *offset += len * 8;
            break;
        }
        default:
            print_indent(indent); printf("Unknown tag type %02X\n", tag_type);
            break;
    }
}
