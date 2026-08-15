#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "region_file.h"
#include "region_lz4.h"

static int fail(const char* message) {
    fprintf(stderr, "%s\n", message);
    return 1;
}

int main(int argc, char** argv) {
    static const unsigned char expected_pattern[] = "HelloHelloHello12345";
    /* Produced by lz4-java 1.8.0's LZ4BlockOutputStream. */
    static const unsigned char compressed_stream[] = {
        0x4c,0x5a,0x34,0x42,0x6c,0x6f,0x63,0x6b,0x26,0x1d,0x00,0x00,
        0x00,0xa0,0x00,0x00,0x00,0xc1,0x00,0x2b,0x02,0x56,0x48,0x65,
        0x6c,0x6c,0x6f,0x05,0x00,0x56,0x31,0x32,0x33,0x34,0x35,0x0f,
        0x00,0x01,0x0a,0x00,0x0f,0x14,0x00,0x65,0x50,0x31,0x32,0x33,
        0x34,0x35,0x4c,0x5a,0x34,0x42,0x6c,0x6f,0x63,0x6b,0x16,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
    };
    unsigned char roundtrip_input[150000];
    unsigned char* encoded;
    unsigned char* decoded;
    size_t encoded_size = 0;
    size_t decoded_size = 0;
    char err[256] = {0};
    char* external_path;
    RegionFile* region;
    int cubic_x;
    int cubic_y;
    int cubic_z;
    size_t i;

    for (i = 0; i < sizeof(compressed_stream); i++) {
        unsigned char* truncated = region_lz4_decode(
            compressed_stream,
            i,
            &decoded_size,
            err,
            sizeof(err)
        );
        if (truncated) {
            free(truncated);
            return fail("truncated LZ4 test vector was accepted");
        }
    }

    decoded = region_lz4_decode(
        compressed_stream,
        sizeof(compressed_stream),
        &decoded_size,
        err,
        sizeof(err)
    );
    if (!decoded) return fail(err[0] ? err : "failed to decode compressed LZ4 test vector");
    if (decoded_size != (sizeof(expected_pattern) - 1U) * 8U) {
        free(decoded);
        return fail("compressed LZ4 test vector has the wrong decoded size");
    }
    for (i = 0; i < decoded_size; i++) {
        if (decoded[i] != expected_pattern[i % (sizeof(expected_pattern) - 1U)]) {
            free(decoded);
            return fail("compressed LZ4 test vector decoded incorrectly");
        }
    }
    free(decoded);

    for (i = 0; i < sizeof(roundtrip_input); i++) {
        roundtrip_input[i] = (unsigned char)((i * 37U + i / 251U) & 0xFFU);
    }
    encoded = region_lz4_encode(
        roundtrip_input,
        sizeof(roundtrip_input),
        &encoded_size,
        err,
        sizeof(err)
    );
    if (!encoded) return fail(err[0] ? err : "failed to encode LZ4 round trip");
    if (encoded_size < 9U || (encoded[8] & 0xF0U) != 0x20U) {
        free(encoded);
        return fail("compressible payload was not emitted as an LZ4-compressed block");
    }
    if (argc > 1) {
        FILE* output = fopen(argv[1], "wb");
        if (!output) {
            free(encoded);
            return fail("failed to write requested LZ4 interoperability fixture");
        }
        if (fwrite(encoded, 1, encoded_size, output) != encoded_size) {
            fclose(output);
            free(encoded);
            return fail("failed to write requested LZ4 interoperability fixture");
        }
        if (fclose(output) != 0) {
            free(encoded);
            return fail("failed to write requested LZ4 interoperability fixture");
        }
    }
    decoded = region_lz4_decode(encoded, encoded_size, &decoded_size, err, sizeof(err));
    free(encoded);
    if (!decoded) return fail(err[0] ? err : "failed to decode LZ4 round trip");
    if (decoded_size != sizeof(roundtrip_input) ||
        memcmp(decoded, roundtrip_input, sizeof(roundtrip_input)) != 0) {
        free(decoded);
        return fail("LZ4 round trip changed payload bytes");
    }
    free(decoded);

    if (!region_path_has_extension("r.0.0.mca") ||
        !region_path_has_extension("R.-2.3.MCR") ||
        region_path_has_extension("level.dat") ||
        region_path_parse_coords("r.+1.2.mca", NULL, NULL) ||
        region_path_parse_coords("r. 1.2.mca", NULL, NULL) ||
        region_path_parse_coords("r.1.2.extra.mca", NULL, NULL)) {
        return fail("region extension/coordinate detection failed");
    }

    external_path = region_external_chunk_path("world/region/r.-2.3.mca", 31, 0);
    if (!external_path || strcmp(external_path, "world/region/c.-33.96.mcc") != 0) {
        free(external_path);
        return fail("external chunk path calculation failed");
    }
    free(external_path);

    if (!region_path_parse_cubic_r2_coords(
            "world/region/r2.-12.34.56.mca",
            &cubic_x,
            &cubic_y,
            &cubic_z) ||
        cubic_x != -12 || cubic_y != 34 || cubic_z != 56 ||
        !region_path_is_cubic_r2("R2.-12.34.56.MCR") ||
        region_path_is_cubic_r2("r2.-12.56.mca") ||
        region_path_is_cubic_r2("r2.-12.34.56.extra.mca") ||
        region_path_is_cubic_r2("r2.+12.34.56.mca") ||
        region_external_chunk_path("r2.-12.34.56.mca", 0, 0) != NULL) {
        return fail("cubic r2 region path detection failed");
    }

    region = region_file_create();
    if (!region || region_file_sector_bytes(region) != REGION_SECTOR_BYTES ||
        region_file_header_sectors(region) != 2U) {
        region_file_free(region);
        return fail("standard region layout defaults failed");
    }
    region->layout = REGION_LAYOUT_CUBIC_R2;
    if (region_file_sector_bytes(region) != REGION_CUBIC_R2_SECTOR_BYTES ||
        region_file_header_sectors(region) != 32U) {
        region_file_free(region);
        return fail("cubic r2 region layout metadata failed");
    }
    region_file_free(region);

    return 0;
}
