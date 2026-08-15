#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nbt_binary.h"
#include "nbt_builder.h"
#include "snbt.h"

static int failures = 0;

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", (message), __LINE__); \
        ++failures; \
    } \
} while (0)

static char* canonical(const NBTTag* tag) {
    char err[256] = {0};
    char* text = snbt_serialize(tag, 0, err, sizeof(err));
    if (!text) fprintf(stderr, "SNBT serialization failed: %s\n", err);
    return text;
}

static void check_round_trip(NBTTag* original, NBTBinaryFormat format, uint32_t version) {
    unsigned char* data = NULL;
    size_t size = 0;
    char err[256] = {0};
    NBTBinaryInfo info;
    NBTTag* parsed;
    char* before;
    char* after;

    CHECK(nbt_binary_serialize(original, format, version, &data, &size,
                               err, sizeof(err)), err);
    if (!data) return;
    parsed = nbt_binary_parse(data, size, NBT_BINARY_AUTO, &info, err, sizeof(err));
    CHECK(parsed != NULL, err);
    if (!parsed) {
        free(data);
        return;
    }
    CHECK(info.format == format, "automatic binary format detection mismatch");
    CHECK(info.bytes_consumed == size, "binary parser did not consume the document");
    if (format == NBT_BINARY_BEDROCK_LEVEL_DAT) {
        CHECK(info.payload_offset == 8, "Bedrock header payload offset mismatch");
        CHECK(info.bedrock_storage_version == version, "Bedrock storage version mismatch");
        CHECK(info.bedrock_declared_payload_size == size - 8,
              "Bedrock declared payload length mismatch");
    }
    before = canonical(original);
    after = canonical(parsed);
    CHECK(before && after && strcmp(before, after) == 0,
          "binary NBT round trip changed the tag tree");
    free(before);
    free(after);
    free_nbt_tree(parsed);
    free(data);
}

static void test_endian_bytes(void) {
    static const unsigned char expected_be[] = {2, 0, 1, 'x', 0x12, 0x34};
    static const unsigned char expected_le[] = {2, 1, 0, 'x', 0x34, 0x12};
    char err[256] = {0};
    NBTTag* value = snbt_parse("4660s", "x", err, sizeof(err));
    unsigned char* data = NULL;
    size_t size = 0;

    CHECK(value != NULL, err);
    if (!value) return;
    CHECK(nbt_binary_serialize(value, NBT_BINARY_JAVA, 0, &data, &size,
                               err, sizeof(err)), err);
    CHECK(size == sizeof(expected_be) && memcmp(data, expected_be, sizeof(expected_be)) == 0,
          "Java NBT was not encoded big-endian");
    free(data);
    data = NULL;
    CHECK(nbt_binary_serialize(value, NBT_BINARY_BEDROCK, 0, &data, &size,
                               err, sizeof(err)), err);
    CHECK(size == sizeof(expected_le) && memcmp(data, expected_le, sizeof(expected_le)) == 0,
          "Bedrock NBT was not encoded little-endian");
    free(data);
    free_nbt_tree(value);
}

static void test_snbt_and_binary_round_trips(void) {
    const char* source =
        "{short:258s,int:16909060,long:72623859790382856L,"
        "float:1.5f,double:-2.25d,bytes:[B;-1b,0b,127b],"
        "ints:[I;1,-2,3],longs:[L;4L,-5L],list:[10,20],"
        "nested:{text:\"hi\\n\\uD83D\\uDE00\",truth:true}}";
    char err[256] = {0};
    NBTTag* root = snbt_parse(source, "Level", err, sizeof(err));
    char* pretty;
    NBTTag* reparsed;
    char* first;
    char* second;

    CHECK(root != NULL, err);
    if (!root) return;
    pretty = snbt_serialize(root, 1, err, sizeof(err));
    CHECK(pretty && strchr(pretty, '\n'), "pretty SNBT has no line breaks");
    reparsed = pretty ? snbt_parse(pretty, "Level", err, sizeof(err)) : NULL;
    CHECK(reparsed != NULL, err);
    first = canonical(root);
    second = reparsed ? canonical(reparsed) : NULL;
    CHECK(first && second && strcmp(first, second) == 0,
          "SNBT parse/serialize round trip changed the tag tree");
    free(first);
    free(second);
    free(pretty);
    free_nbt_tree(reparsed);

    check_round_trip(root, NBT_BINARY_JAVA, 0);
    check_round_trip(root, NBT_BINARY_BEDROCK, 0);
    check_round_trip(root, NBT_BINARY_BEDROCK_LEVEL_DAT, 10);
    free_nbt_tree(root);
}

static void test_invalid_inputs(void) {
    char err[256] = {0};
    NBTTag* tag;
    static const unsigned char bad_header[] = {
        10, 0, 0, 0, 20, 0, 0, 0, 10, 0, 0, 0
    };

    tag = snbt_parse("[1,2b]", "", err, sizeof(err));
    CHECK(tag == NULL, "mixed-type SNBT list was accepted");
    free_nbt_tree(tag);
    tag = snbt_parse("[B;1]", "", err, sizeof(err));
    CHECK(tag == NULL, "TAG_Int was accepted in an SNBT byte array");
    free_nbt_tree(tag);
    tag = snbt_parse("128b", "", err, sizeof(err));
    CHECK(tag == NULL, "out-of-range SNBT byte was accepted");
    free_nbt_tree(tag);
    tag = snbt_parse("{x:1", "", err, sizeof(err));
    CHECK(tag == NULL, "unterminated SNBT compound was accepted");
    free_nbt_tree(tag);

    tag = nbt_binary_parse(bad_header, sizeof(bad_header),
                           NBT_BINARY_BEDROCK_LEVEL_DAT, NULL, err, sizeof(err));
    CHECK(tag == NULL, "Bedrock level.dat with oversized payload was accepted");
    free_nbt_tree(tag);
}

int main(void) {
    test_endian_bytes();
    test_snbt_and_binary_round_trips();
    test_invalid_inputs();
    if (failures) {
        fprintf(stderr, "%d extended format test(s) failed\n", failures);
        return 1;
    }
    puts("Extended format tests passed");
    return 0;
}
