#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h> // REQUIRED for gzip writing
#include "nbt_parser.h"
#include "nbt_utils.h"

static void write_payload(gzFile f, NBTTag* tag);
void write_tag(gzFile f, NBTTag* tag);

// Save a 2-byte length-prefixed string
static void write_string(gzFile f, const char* str) {
    if (!str) str = "";
    uint16_t len = (uint16_t)strlen(str);
    uint8_t buf[2] = { (len >> 8) & 0xFF, len & 0xFF };
    gzwrite(f, buf, 2);
    gzwrite(f, str, len);
}

static void write_payload(gzFile f, NBTTag* tag) {
    if (!tag) return; // SAFETY: avoid crash if NULL passed

    switch (tag->type) {
        case TAG_Byte:
            gzwrite(f, &tag->value.byte_val, 1);
            break;

        case TAG_Short: {
            uint8_t buf[2] = {
                (tag->value.short_val >> 8) & 0xFF,
                tag->value.short_val & 0xFF
            };
            gzwrite(f, buf, 2);
            break;
        }

        case TAG_Int: {
            int32_t v = tag->value.int_val;
            uint8_t buf[4] = {
                (v >> 24) & 0xFF,
                (v >> 16) & 0xFF,
                (v >> 8) & 0xFF,
                v & 0xFF
            };
            gzwrite(f, buf, 4);
            break;
        }

        case TAG_Long: {
            int64_t v = tag->value.long_val;
            uint8_t buf[8];
            for (int i = 0; i < 8; i++)
                buf[7 - i] = (v >> (i * 8)) & 0xFF;
            gzwrite(f, buf, 8);
            break;
        }

        case TAG_Float: {
            union { float f; uint32_t i; } u;
            u.f = tag->value.float_val;
            uint8_t buf[4] = {
                (u.i >> 24) & 0xFF,
                (u.i >> 16) & 0xFF,
                (u.i >> 8) & 0xFF,
                u.i & 0xFF
            };
            gzwrite(f, buf, 4);
            break;
        }

        case TAG_Double: {
            union { double d; uint64_t i; } u;
            u.d = tag->value.double_val;
            uint8_t buf[8];
            for (int i = 0; i < 8; i++)
                buf[7 - i] = (u.i >> (i * 8)) & 0xFF;
            gzwrite(f, buf, 8);
            break;
        }

        case TAG_Byte_Array: {
            int32_t len = tag->value.byte_array.length;
            uint8_t len_buf[4] = {
                (len >> 24) & 0xFF,
                (len >> 16) & 0xFF,
                (len >> 8) & 0xFF,
                len & 0xFF
            };
            gzwrite(f, len_buf, 4);
            gzwrite(f, tag->value.byte_array.data, len);
            break;
        }

        case TAG_String:
            write_string(f, tag->value.string_val);
            break;

        case TAG_List: {
            gzputc(f, tag->value.list.element_type);

            // Count only valid elements (non-NULL and matching element type)
            int32_t real_count = 0;
            for (int i = 0; i < tag->value.list.count; i++) {
                if (tag->value.list.items[i] && tag->value.list.items[i]->type == tag->value.list.element_type) {
                    real_count++;
                }
            }

            uint8_t len_buf[4] = {
                (real_count >> 24) & 0xFF,
                (real_count >> 16) & 0xFF,
                (real_count >> 8) & 0xFF,
                real_count & 0xFF
            };
            gzwrite(f, len_buf, 4);

            for (int i = 0; i < tag->value.list.count; i++) {
                if (tag->value.list.items[i] && tag->value.list.items[i]->type == tag->value.list.element_type) {
                    write_payload(f, tag->value.list.items[i]);
                }
            }
            break;
        }

        case TAG_Compound:
            for (int i = 0; i < tag->value.compound.count; i++) {
                if (tag->value.compound.items[i]) {
                    write_tag(f, tag->value.compound.items[i]);
                }
            }
            gzputc(f, TAG_End);
            break;

        case TAG_Int_Array: {
            int32_t len = tag->value.int_array.length;
            uint8_t len_buf[4] = {
                (len >> 24) & 0xFF,
                (len >> 16) & 0xFF,
                (len >> 8) & 0xFF,
                len & 0xFF
            };
            gzwrite(f, len_buf, 4);
            gzwrite(f, tag->value.int_array.data, len * sizeof(int32_t));
            break;
        }

        case TAG_Long_Array: {
            int32_t len = tag->value.long_array.length;
            uint8_t len_buf[4] = {
                (len >> 24) & 0xFF,
                (len >> 16) & 0xFF,
                (len >> 8) & 0xFF,
                len & 0xFF
            };
            gzwrite(f, len_buf, 4);
            gzwrite(f, tag->value.long_array.data, len * sizeof(int64_t));
            break;
        }

        default:
            printf("[WARN] Unsupported tag type %d — skipped\n", tag->type);
            break;
    }
}

void write_tag(gzFile f, NBTTag* tag) {
    if (!tag) return;
    gzputc(f, tag->type);
    if (tag->name) {
        write_string(f, tag->name);
    } else {
        write_string(f, "");  // fallback for unnamed root tag
    }
    write_payload(f, tag);
}

NBTTag* find_tag_by_path(NBTTag* root, char* path) {
    if (!root || root->type != TAG_Compound) return NULL;

    while (*path == '/') path++;

    NBTTag* current = root;
    char path_copy[512];
    snprintf(path_copy, sizeof(path_copy), "%s", path);

    printf("[DEBUG] Walking path: %s\n", path_copy);
    char* token = strtok(path_copy, "/");

    if (token && (strcmp(token, root->name) == 0 || strcmp(token, "") == 0)) {
        printf("[DEBUG] Skipping root token: \"%s\"\n", token);
        token = strtok(NULL, "/");
    }

    while (token) {
        printf("[DEBUG] Looking for token: \"%s\" in current compound (%s)\n", token, current->name ? current->name : "");

        int found = 0;
        if (current->type != TAG_Compound) {
            printf("[DEBUG] Current tag is not a compound. Aborting.\n");
            return NULL;
        }

        for (int i = 0; i < current->value.compound.count; i++) {
            NBTTag* child = current->value.compound.items[i];
            if (child && child->name && strcmp(child->name, token) == 0) {
                printf("    ✔ Match found. Descending into: \"%s\"\n", token);
                current = child;
                found = 1;
                break;
            }
        }

        if (!found) {
            printf("    ✘ Token not found: \"%s\"\n", token);
            return NULL;
        }

        token = strtok(NULL, "/");
    }

    printf("[DEBUG] Target tag found: \"%s\" (type %d)\n", current->name ? current->name : "(null)", current->type);
    return current;
}
