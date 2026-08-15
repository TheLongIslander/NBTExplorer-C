#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "region_lz4.h"

#define LZ4_BLOCK_MAGIC_LENGTH 8U
#define LZ4_BLOCK_HEADER_LENGTH 21U
#define LZ4_BLOCK_LEVEL_BASE 10U
#define LZ4_BLOCK_METHOD_RAW 0x10U
#define LZ4_BLOCK_METHOD_LZ4 0x20U
#define LZ4_BLOCK_DEFAULT_SIZE (1U << 16)
#define LZ4_BLOCK_DEFAULT_LEVEL 6U
#define LZ4_BLOCK_XXHASH_SEED 0x9747B28CU
/* lz4-java's Checksum adapter exposes 28 bits; matching it is format-critical. */
#define LZ4_BLOCK_CHECKSUM_MASK 0x0FFFFFFFU
#define LZ4_MIN_MATCH 4U
#define LZ4_LAST_LITERALS 5U
#define LZ4_MATCH_FIND_LIMIT 12U
#define LZ4_HASH_LOG 16U
#define LZ4_HASH_SIZE (1U << LZ4_HASH_LOG)

static const unsigned char LZ4_BLOCK_MAGIC[LZ4_BLOCK_MAGIC_LENGTH] = {
    'L', 'Z', '4', 'B', 'l', 'o', 'c', 'k'
};

static void set_err(char* err, size_t err_sz, const char* msg) {
    if (err && err_sz > 0) snprintf(err, err_sz, "%s", msg);
}

static uint32_t read_le_u32(const unsigned char* p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void write_le_u32(unsigned char* p, uint32_t value) {
    p[0] = (unsigned char)(value & 0xFFU);
    p[1] = (unsigned char)((value >> 8) & 0xFFU);
    p[2] = (unsigned char)((value >> 16) & 0xFFU);
    p[3] = (unsigned char)((value >> 24) & 0xFFU);
}

static uint32_t rotate_left_u32(uint32_t value, unsigned int count) {
    return (value << count) | (value >> (32U - count));
}

static uint32_t xxhash_round(uint32_t accumulator, uint32_t lane) {
    accumulator += lane * 0x85EBCA77U;
    accumulator = rotate_left_u32(accumulator, 13U);
    return accumulator * 0x9E3779B1U;
}

static uint32_t xxhash32(const unsigned char* data, size_t size, uint32_t seed) {
    const unsigned char* cursor = data;
    const unsigned char* end = data + size;
    uint32_t hash;

    if (size >= 16U) {
        const unsigned char* limit = end - 16U;
        uint32_t v1 = seed + 0x9E3779B1U + 0x85EBCA77U;
        uint32_t v2 = seed + 0x85EBCA77U;
        uint32_t v3 = seed;
        uint32_t v4 = seed - 0x9E3779B1U;

        do {
            v1 = xxhash_round(v1, read_le_u32(cursor));
            cursor += 4;
            v2 = xxhash_round(v2, read_le_u32(cursor));
            cursor += 4;
            v3 = xxhash_round(v3, read_le_u32(cursor));
            cursor += 4;
            v4 = xxhash_round(v4, read_le_u32(cursor));
            cursor += 4;
        } while (cursor <= limit);

        hash = rotate_left_u32(v1, 1U) + rotate_left_u32(v2, 7U) +
               rotate_left_u32(v3, 12U) + rotate_left_u32(v4, 18U);
    } else {
        hash = seed + 0x165667B1U;
    }

    hash += (uint32_t)size;
    while ((size_t)(end - cursor) >= 4U) {
        hash += read_le_u32(cursor) * 0xC2B2AE3DU;
        hash = rotate_left_u32(hash, 17U) * 0x27D4EB2FU;
        cursor += 4;
    }
    while (cursor < end) {
        hash += (uint32_t)(*cursor++) * 0x165667B1U;
        hash = rotate_left_u32(hash, 11U) * 0x9E3779B1U;
    }

    hash ^= hash >> 15;
    hash *= 0x85EBCA77U;
    hash ^= hash >> 13;
    hash *= 0xC2B2AE3DU;
    hash ^= hash >> 16;
    return hash;
}

static int grow_output(unsigned char** output, size_t* capacity, size_t needed) {
    size_t new_capacity;
    unsigned char* grown;

    if (needed <= *capacity) return 1;
    new_capacity = *capacity ? *capacity : LZ4_BLOCK_DEFAULT_SIZE;
    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2U) {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2U;
    }

    grown = realloc(*output, new_capacity);
    if (!grown) return 0;
    *output = grown;
    *capacity = new_capacity;
    return 1;
}

static int read_extended_length(
    const unsigned char* source,
    size_t source_size,
    size_t* source_pos,
    size_t* length
) {
    unsigned int byte_value;

    do {
        if (*source_pos >= source_size) return 0;
        byte_value = source[(*source_pos)++];
        if (*length > SIZE_MAX - byte_value) return 0;
        *length += byte_value;
    } while (byte_value == 255U);

    return 1;
}

static int decode_lz4_block(
    const unsigned char* source,
    size_t source_size,
    unsigned char* destination,
    size_t destination_size
) {
    size_t source_pos = 0;
    size_t destination_pos = 0;

    while (source_pos < source_size) {
        unsigned int token = source[source_pos++];
        size_t literal_length = token >> 4;
        size_t match_length;
        size_t match_offset;

        if (literal_length == 15U &&
            !read_extended_length(source, source_size, &source_pos, &literal_length)) {
            return 0;
        }
        if (literal_length > source_size - source_pos ||
            literal_length > destination_size - destination_pos) {
            return 0;
        }

        if (literal_length > 0) {
            memcpy(destination + destination_pos, source + source_pos, literal_length);
            source_pos += literal_length;
            destination_pos += literal_length;
        }

        if (source_pos == source_size) break;
        if (source_size - source_pos < 2U) return 0;

        match_offset = (size_t)source[source_pos] | ((size_t)source[source_pos + 1U] << 8);
        source_pos += 2U;
        if (match_offset == 0 || match_offset > destination_pos) return 0;

        match_length = (token & 0x0FU) + 4U;
        if ((token & 0x0FU) == 15U &&
            !read_extended_length(source, source_size, &source_pos, &match_length)) {
            return 0;
        }
        if (match_length > destination_size - destination_pos) return 0;

        /* Byte-wise copying intentionally supports overlapping LZ4 matches. */
        while (match_length-- > 0) {
            destination[destination_pos] = destination[destination_pos - match_offset];
            destination_pos++;
        }
    }

    return source_pos == source_size && destination_pos == destination_size;
}

static uint32_t lz4_hash_sequence(const unsigned char* source) {
    return (read_le_u32(source) * 2654435761U) >> (32U - LZ4_HASH_LOG);
}

static int emit_length(
    unsigned char* destination,
    size_t destination_capacity,
    size_t* destination_pos,
    size_t length
) {
    while (length >= 255U) {
        if (*destination_pos >= destination_capacity) return 0;
        destination[(*destination_pos)++] = 255U;
        length -= 255U;
    }
    if (*destination_pos >= destination_capacity) return 0;
    destination[(*destination_pos)++] = (unsigned char)length;
    return 1;
}

/* A small portable LZ4 block encoder; the decoder accepts streams from lz4-java too. */
static size_t encode_lz4_block(
    const unsigned char* source,
    size_t source_size,
    unsigned char* destination,
    size_t destination_capacity
) {
    uint32_t* hash_table;
    size_t source_pos = 0;
    size_t anchor = 0;
    size_t destination_pos = 0;
    size_t i;

    hash_table = malloc((size_t)LZ4_HASH_SIZE * sizeof(*hash_table));
    if (!hash_table) return 0;
    for (i = 0; i < LZ4_HASH_SIZE; i++) hash_table[i] = UINT32_MAX;

    while (source_size >= LZ4_MATCH_FIND_LIMIT &&
           source_pos <= source_size - LZ4_MATCH_FIND_LIMIT) {
        uint32_t hash = lz4_hash_sequence(source + source_pos);
        uint32_t reference_u32 = hash_table[hash];
        size_t reference_start;
        size_t reference_pos;
        size_t match_start;
        size_t literal_length;
        size_t match_length;
        size_t offset;
        size_t token_pos;
        unsigned int token = 0;

        hash_table[hash] = (uint32_t)source_pos;
        if (reference_u32 == UINT32_MAX) {
            source_pos++;
            continue;
        }

        reference_start = reference_u32;
        if (reference_start >= source_pos || source_pos - reference_start > 65535U ||
            memcmp(source + reference_start, source + source_pos, LZ4_MIN_MATCH) != 0) {
            source_pos++;
            continue;
        }

        match_start = source_pos;
        reference_pos = reference_start;
        source_pos += LZ4_MIN_MATCH;
        reference_pos += LZ4_MIN_MATCH;
        while (source_pos < source_size - LZ4_LAST_LITERALS &&
               source[source_pos] == source[reference_pos]) {
            source_pos++;
            reference_pos++;
        }

        literal_length = match_start - anchor;
        match_length = source_pos - match_start - LZ4_MIN_MATCH;
        offset = match_start - reference_start;

        if (destination_pos >= destination_capacity) goto fail;
        token_pos = destination_pos++;
        if (literal_length < 15U) {
            token = (unsigned int)(literal_length << 4);
        } else {
            token = 0xF0U;
            if (!emit_length(
                    destination,
                    destination_capacity,
                    &destination_pos,
                    literal_length - 15U)) goto fail;
        }

        if (literal_length > destination_capacity - destination_pos) goto fail;
        memcpy(destination + destination_pos, source + anchor, literal_length);
        destination_pos += literal_length;

        if (destination_capacity - destination_pos < 2U) goto fail;
        destination[destination_pos++] = (unsigned char)(offset & 0xFFU);
        destination[destination_pos++] = (unsigned char)((offset >> 8) & 0xFFU);

        if (match_length < 15U) {
            token |= (unsigned int)match_length;
        } else {
            token |= 0x0FU;
            if (!emit_length(
                    destination,
                    destination_capacity,
                    &destination_pos,
                    match_length - 15U)) goto fail;
        }
        destination[token_pos] = (unsigned char)token;
        anchor = source_pos;

        if (source_pos >= 2U && source_pos - 2U + LZ4_MIN_MATCH <= source_size) {
            hash_table[lz4_hash_sequence(source + source_pos - 2U)] = (uint32_t)(source_pos - 2U);
        }
    }

    {
        size_t literal_length = source_size - anchor;
        size_t token_pos;
        unsigned int token;

        if (destination_pos >= destination_capacity) goto fail;
        token_pos = destination_pos++;
        if (literal_length < 15U) {
            token = (unsigned int)(literal_length << 4);
        } else {
            token = 0xF0U;
            if (!emit_length(
                    destination,
                    destination_capacity,
                    &destination_pos,
                    literal_length - 15U)) goto fail;
        }
        if (literal_length > destination_capacity - destination_pos) goto fail;
        memcpy(destination + destination_pos, source + anchor, literal_length);
        destination_pos += literal_length;
        destination[token_pos] = (unsigned char)token;
    }

    free(hash_table);
    return destination_pos;

fail:
    free(hash_table);
    return 0;
}

unsigned char* region_lz4_decode(
    const unsigned char* input,
    size_t input_size,
    size_t* out_size,
    char* err,
    size_t err_sz
) {
    unsigned char* output = NULL;
    size_t output_size = 0;
    size_t output_capacity = 0;
    size_t input_pos = 0;
    int found_end = 0;

    if (out_size) *out_size = 0;
    if (!input || !out_size) {
        set_err(err, err_sz, "invalid LZ4 block stream arguments");
        return NULL;
    }

    while (input_pos < input_size) {
        const unsigned char* header;
        unsigned int token;
        unsigned int method;
        unsigned int level;
        uint32_t compressed_length;
        uint32_t original_length;
        uint32_t expected_checksum;
        unsigned char* block_output;

        if (input_size - input_pos < LZ4_BLOCK_HEADER_LENGTH) {
            set_err(err, err_sz, "truncated Minecraft LZ4 block header");
            goto fail;
        }

        header = input + input_pos;
        if (memcmp(header, LZ4_BLOCK_MAGIC, LZ4_BLOCK_MAGIC_LENGTH) != 0) {
            set_err(err, err_sz, "invalid Minecraft LZ4 block magic");
            goto fail;
        }

        token = header[LZ4_BLOCK_MAGIC_LENGTH];
        method = token & 0xF0U;
        level = LZ4_BLOCK_LEVEL_BASE + (token & 0x0FU);
        compressed_length = read_le_u32(header + LZ4_BLOCK_MAGIC_LENGTH + 1U);
        original_length = read_le_u32(header + LZ4_BLOCK_MAGIC_LENGTH + 5U);
        expected_checksum = read_le_u32(header + LZ4_BLOCK_MAGIC_LENGTH + 9U);
        input_pos += LZ4_BLOCK_HEADER_LENGTH;

        if (method != LZ4_BLOCK_METHOD_RAW && method != LZ4_BLOCK_METHOD_LZ4) {
            set_err(err, err_sz, "invalid Minecraft LZ4 compression method");
            goto fail;
        }
        if (original_length > (1U << level) ||
            (original_length == 0U) != (compressed_length == 0U) ||
            (method == LZ4_BLOCK_METHOD_RAW && original_length != compressed_length) ||
            (method == LZ4_BLOCK_METHOD_LZ4 && original_length != 0U &&
             compressed_length >= original_length)) {
            set_err(err, err_sz, "invalid Minecraft LZ4 block lengths");
            goto fail;
        }

        if (original_length == 0U) {
            if (expected_checksum != 0U) {
                set_err(err, err_sz, "invalid Minecraft LZ4 end marker");
                goto fail;
            }
            found_end = 1;
            break;
        }

        if ((size_t)compressed_length > input_size - input_pos) {
            set_err(err, err_sz, "truncated Minecraft LZ4 block payload");
            goto fail;
        }
        if (output_size > SIZE_MAX - (size_t)original_length ||
            !grow_output(&output, &output_capacity, output_size + (size_t)original_length)) {
            set_err(err, err_sz, "out of memory decoding Minecraft LZ4 payload");
            goto fail;
        }

        block_output = output + output_size;
        if (method == LZ4_BLOCK_METHOD_RAW) {
            memcpy(block_output, input + input_pos, original_length);
        } else if (!decode_lz4_block(input + input_pos, compressed_length, block_output, original_length)) {
            set_err(err, err_sz, "corrupt Minecraft LZ4-compressed block");
            goto fail;
        }

        if ((xxhash32(block_output, original_length, LZ4_BLOCK_XXHASH_SEED) &
             LZ4_BLOCK_CHECKSUM_MASK) != expected_checksum) {
            set_err(err, err_sz, "Minecraft LZ4 block checksum mismatch");
            goto fail;
        }

        input_pos += compressed_length;
        output_size += original_length;
    }

    if (!found_end) {
        set_err(err, err_sz, "Minecraft LZ4 stream is missing its end marker");
        goto fail;
    }
    if (input_pos != input_size) {
        set_err(err, err_sz, "trailing data after Minecraft LZ4 end marker");
        goto fail;
    }

    if (!output) {
        output = malloc(1U);
        if (!output) {
            set_err(err, err_sz, "out of memory decoding Minecraft LZ4 payload");
            return NULL;
        }
    }
    *out_size = output_size;
    return output;

fail:
    free(output);
    return NULL;
}

unsigned char* region_lz4_encode(
    const unsigned char* input,
    size_t input_size,
    size_t* out_size,
    char* err,
    size_t err_sz
) {
    size_t block_count;
    size_t encoded_size;
    size_t input_pos = 0;
    size_t output_pos = 0;
    unsigned char* output;
    unsigned char* compressed_block = NULL;
    size_t compressed_capacity = 0;

    if (out_size) *out_size = 0;
    if ((!input && input_size > 0) || !out_size) {
        set_err(err, err_sz, "invalid LZ4 block stream arguments");
        return NULL;
    }

    block_count = input_size / LZ4_BLOCK_DEFAULT_SIZE;
    if (input_size % LZ4_BLOCK_DEFAULT_SIZE != 0) block_count++;
    if (block_count == SIZE_MAX ||
        block_count + 1U > (SIZE_MAX - input_size) / LZ4_BLOCK_HEADER_LENGTH) {
        set_err(err, err_sz, "NBT payload too large for Minecraft LZ4 encoding");
        return NULL;
    }
    encoded_size = input_size + (block_count + 1U) * LZ4_BLOCK_HEADER_LENGTH;
    output = malloc(encoded_size == 0 ? 1U : encoded_size);
    if (!output) {
        set_err(err, err_sz, "out of memory encoding Minecraft LZ4 payload");
        return NULL;
    }

    while (input_pos < input_size) {
        size_t block_size = input_size - input_pos;
        size_t compressed_size;
        const unsigned char* block_data;
        size_t stored_size;
        unsigned int method;
        unsigned char* header = output + output_pos;
        if (block_size > LZ4_BLOCK_DEFAULT_SIZE) block_size = LZ4_BLOCK_DEFAULT_SIZE;

        if (block_size > SIZE_MAX - block_size / 255U - 16U) {
            free(compressed_block);
            free(output);
            set_err(err, err_sz, "NBT payload too large for Minecraft LZ4 encoding");
            return NULL;
        }
        if (compressed_capacity < block_size + block_size / 255U + 16U) {
            unsigned char* grown;
            compressed_capacity = block_size + block_size / 255U + 16U;
            grown = realloc(compressed_block, compressed_capacity);
            if (!grown) {
                free(compressed_block);
                free(output);
                set_err(err, err_sz, "out of memory encoding Minecraft LZ4 payload");
                return NULL;
            }
            compressed_block = grown;
        }

        compressed_size = encode_lz4_block(
            input + input_pos,
            block_size,
            compressed_block,
            compressed_capacity
        );
        if (compressed_size > 0 && compressed_size < block_size) {
            method = LZ4_BLOCK_METHOD_LZ4;
            block_data = compressed_block;
            stored_size = compressed_size;
        } else {
            method = LZ4_BLOCK_METHOD_RAW;
            block_data = input + input_pos;
            stored_size = block_size;
        }
        if (!block_data) {
            free(compressed_block);
            free(output);
            set_err(err, err_sz, "missing input while encoding Minecraft LZ4 payload");
            return NULL;
        }

        memcpy(header, LZ4_BLOCK_MAGIC, LZ4_BLOCK_MAGIC_LENGTH);
        header[LZ4_BLOCK_MAGIC_LENGTH] = (unsigned char)(method | LZ4_BLOCK_DEFAULT_LEVEL);
        write_le_u32(header + LZ4_BLOCK_MAGIC_LENGTH + 1U, (uint32_t)stored_size);
        write_le_u32(header + LZ4_BLOCK_MAGIC_LENGTH + 5U, (uint32_t)block_size);
        write_le_u32(
            header + LZ4_BLOCK_MAGIC_LENGTH + 9U,
            xxhash32(input + input_pos, block_size, LZ4_BLOCK_XXHASH_SEED) &
                LZ4_BLOCK_CHECKSUM_MASK
        );
        if (stored_size > 0) {
            memcpy(header + LZ4_BLOCK_HEADER_LENGTH, block_data, stored_size);
        }
        input_pos += block_size;
        output_pos += LZ4_BLOCK_HEADER_LENGTH + stored_size;
    }

    free(compressed_block);

    memcpy(output + output_pos, LZ4_BLOCK_MAGIC, LZ4_BLOCK_MAGIC_LENGTH);
    output[output_pos + LZ4_BLOCK_MAGIC_LENGTH] =
        (unsigned char)(LZ4_BLOCK_METHOD_RAW | LZ4_BLOCK_DEFAULT_LEVEL);
    write_le_u32(output + output_pos + LZ4_BLOCK_MAGIC_LENGTH + 1U, 0U);
    write_le_u32(output + output_pos + LZ4_BLOCK_MAGIC_LENGTH + 5U, 0U);
    write_le_u32(output + output_pos + LZ4_BLOCK_MAGIC_LENGTH + 9U, 0U);
    output_pos += LZ4_BLOCK_HEADER_LENGTH;

    *out_size = output_pos;
    return output;
}
