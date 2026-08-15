#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "edit_save.h"
#include "region_file.h"
#include "region_write.h"

/* region_write.c needs the serializer; this focused test supplies deterministic bytes. */
int serialize_tag_to_nbt_bytes(
    const NBTTag* tag,
    unsigned char** out_data,
    size_t* out_size,
    char* err,
    size_t err_sz
) {
    unsigned char* data;
    size_t i;

    (void)tag;
    (void)err;
    (void)err_sz;
    if (!out_data || !out_size) return 0;
    data = malloc(70000U);
    if (!data) return 0;
    for (i = 0; i < 70000U; i++) data[i] = (unsigned char)(i * 37U + i / 251U);
    *out_data = data;
    *out_size = 70000U;
    return 1;
}

static int fail(const char* message) {
    fprintf(stderr, "%s\n", message);
    return 1;
}

int main(int argc, char** argv) {
    RegionFile* region;
    RegionChunkSlot* slot;
    NBTTag dummy_root;
    unsigned char* original_payload;
    char err[256] = {0};
    size_t oversized = (size_t)255U * REGION_CUBIC_R2_SECTOR_BYTES - 4U;

    if (argc != 2) return fail("test_cubic_region expects an output path");

    region = region_file_create();
    if (!region) return fail("failed to allocate cubic region model");
    region->layout = REGION_LAYOUT_CUBIC_R2;
    slot = region_file_get_chunk_mut(region, 0, 0);
    slot->present = 1;
    slot->compression_type = REGION_COMPRESSION_ZLIB;
    slot->payload = malloc(oversized);
    if (!slot->payload) {
        region_file_free(region);
        return fail("failed to allocate oversized cube payload");
    }
    slot->payload_size = oversized;

    if (region_file_write(region, argv[1], err, sizeof(err)) ||
        strstr(err, "do not support external chunk storage") == NULL) {
        region_file_free(region);
        return fail("oversized cubic r2 model was not rejected");
    }

    free(slot->payload);
    original_payload = malloc(3U);
    if (!original_payload) {
        slot->payload = NULL;
        region_file_free(region);
        return fail("failed to allocate original cube payload");
    }
    memcpy(original_payload, "old", 3U);
    slot->payload = original_payload;
    slot->payload_size = 3U;
    slot->stored_length = 4U;
    slot->compression_type = REGION_COMPRESSION_NONE;
    memset(&dummy_root, 0, sizeof(dummy_root));
    err[0] = '\0';

    if (region_file_update_chunk_from_nbt(
            region,
            0,
            0,
            &dummy_root,
            REGION_COMPRESSION_NONE,
            err,
            sizeof(err)) ||
        strstr(err, "too large") == NULL ||
        slot->payload != original_payload ||
        slot->payload_size != 3U ||
        memcmp(slot->payload, "old", 3U) != 0) {
        region_file_free(region);
        return fail("oversized cube update was not rejected without mutation");
    }

    region_file_free(region);
    return 0;
}
