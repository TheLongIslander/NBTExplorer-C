#ifndef NBT_BINARY_H
#define NBT_BINARY_H

#include <stddef.h>
#include <stdint.h>

#include "nbt_parser.h"

/*
 * The Java edition uses big-endian NBT.  Bedrock's files use little-endian
 * NBT; Bedrock level.dat additionally wraps the NBT payload in an eight-byte
 * little-endian header (storage version, then payload byte length).
 */
typedef enum {
    NBT_BINARY_AUTO = 0,
    NBT_BINARY_JAVA,
    NBT_BINARY_BEDROCK,
    NBT_BINARY_BEDROCK_LEVEL_DAT
} NBTBinaryFormat;

typedef struct {
    NBTBinaryFormat format;
    size_t payload_offset;
    size_t payload_size;
    size_t bytes_consumed;
    uint32_t bedrock_storage_version;
    uint32_t bedrock_declared_payload_size;
} NBTBinaryInfo;

/*
 * Parse one complete binary NBT document.  AUTO recognizes an exact Bedrock
 * level.dat envelope first, then exact Java and Bedrock NBT payloads.  Explicit
 * formats permit bytes after the parsed root and report their count via info.
 */
NBTTag* nbt_binary_parse(
    const unsigned char* data,
    size_t size,
    NBTBinaryFormat format,
    NBTBinaryInfo* info,
    char* err,
    size_t err_sz
);

/* Serialize Java, Bedrock, or an enveloped Bedrock level.dat document. */
int nbt_binary_serialize(
    const NBTTag* root,
    NBTBinaryFormat format,
    uint32_t bedrock_storage_version,
    unsigned char** out_data,
    size_t* out_size,
    char* err,
    size_t err_sz
);

const char* nbt_binary_format_name(NBTBinaryFormat format);

#endif
