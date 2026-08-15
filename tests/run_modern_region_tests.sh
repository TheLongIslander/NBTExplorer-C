#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BIN="${BIN:-./bin/nbt_explorer}"
CC_BIN="${CC:-cc}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/nbt_modern_region_tests.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

if [[ ! -x "$BIN" ]]; then
  echo "Missing binary: $BIN"
  exit 1
fi

"$CC_BIN" -std=c11 -Wall -Wextra -Wpedantic -Ih \
  tests/test_region_lz4.c src/region_lz4.c src/region_file.c \
  -o "$TMP_DIR/test_region_lz4"
"$TMP_DIR/test_region_lz4"

python3 - "$TMP_DIR" <<'PY'
import pathlib
import struct
import sys

tmp = pathlib.Path(sys.argv[1])

P1 = 0x9E3779B1
P2 = 0x85EBCA77
P3 = 0xC2B2AE3D
P4 = 0x27D4EB2F
P5 = 0x165667B1
MASK = 0xFFFFFFFF

def rol(value, count):
    return ((value << count) | (value >> (32 - count))) & MASK

def xxh32(data, seed=0x9747B28C):
    pos = 0
    if len(data) >= 16:
        values = [(seed + P1 + P2) & MASK, (seed + P2) & MASK, seed, (seed - P1) & MASK]
        while pos <= len(data) - 16:
            for lane in range(4):
                value = struct.unpack_from("<I", data, pos)[0]
                pos += 4
                values[lane] = rol((values[lane] + value * P2) & MASK, 13) * P1 & MASK
        result = (rol(values[0], 1) + rol(values[1], 7) + rol(values[2], 12) + rol(values[3], 18)) & MASK
    else:
        result = (seed + P5) & MASK
    result = (result + len(data)) & MASK
    while pos <= len(data) - 4:
        result = (result + struct.unpack_from("<I", data, pos)[0] * P3) & MASK
        result = rol(result, 17) * P4 & MASK
        pos += 4
    while pos < len(data):
        result = (result + data[pos] * P5) & MASK
        result = rol(result, 11) * P1 & MASK
        pos += 1
    result ^= result >> 15
    result = result * P2 & MASK
    result ^= result >> 13
    result = result * P3 & MASK
    result ^= result >> 16
    return result & MASK

def lz4_block_stream(raw):
    magic = b"LZ4Block"
    header = magic + bytes([0x16]) + struct.pack("<III", len(raw), len(raw), xxh32(raw) & 0x0FFFFFFF)
    end = magic + bytes([0x16]) + bytes(12)
    return header + raw + end

def nbt(xpos):
    return b"\x0a\x00\x00" + b"\x03\x00\x04xPos" + struct.pack(">i", xpos) + b"\x00"

def region(chunk_index, flags, payload):
    data = bytearray(3 * 4096)
    struct.pack_into(">I", data, chunk_index * 4, (2 << 8) | 1)
    struct.pack_into(">I", data, 4096 + chunk_index * 4, 123456789)
    if flags & 0x80:
        struct.pack_into(">I", data, 8192, 1)
        data[8196] = flags
    else:
        struct.pack_into(">I", data, 8192, len(payload) + 1)
        data[8196] = flags
        data[8197:8197 + len(payload)] = payload
    return data

raw = nbt(123)
stream = lz4_block_stream(raw)
(tmp / "r.0.0.mcr").write_bytes(region(0, 4, stream))

# local (31, 0) in region (-2, 3) is global chunk (-33, 96)
(tmp / "r.-2.3.mca").write_bytes(region(31, 0x84, b""))
(tmp / "c.-33.96.mcc").write_bytes(stream)
PY

"$BIN" "$TMP_DIR/r.0.0.mcr" --chunk 0 0 --dump "$TMP_DIR/mcr.txt" >"$TMP_DIR/mcr.log" 2>&1
grep -q "Detected input format: lz4" "$TMP_DIR/mcr.log"
grep -q "Int: 123" "$TMP_DIR/mcr.txt"

"$BIN" "$TMP_DIR/r.-2.3.mca" --chunk 31 0 --dump "$TMP_DIR/external.txt" >"$TMP_DIR/external.log" 2>&1
grep -q "Detected input format: lz4" "$TMP_DIR/external.log"
grep -q "Int: 123" "$TMP_DIR/external.txt"

mkdir "$TMP_DIR/output"
"$BIN" "$TMP_DIR/r.-2.3.mca" --chunk 31 0 --set xPos 456 \
  --output "$TMP_DIR/output/r.4.-1.mca" >"$TMP_DIR/write.log" 2>&1

if [[ ! -f "$TMP_DIR/output/c.159.-32.mcc" ]]; then
  echo "Expected rewritten external .mcc chunk was not created"
  exit 1
fi

python3 - "$TMP_DIR/output/r.4.-1.mca" <<'PY'
import pathlib
import struct
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
loc = struct.unpack_from(">I", data, 31 * 4)[0]
offset = (loc >> 8) & 0xFFFFFF
count = loc & 0xFF
if count != 1:
    raise SystemExit(f"external chunk should use one stub sector, got {count}")
start = offset * 4096
if struct.unpack_from(">I", data, start)[0] != 1 or data[start + 4] != 0x84:
    raise SystemExit("rewritten external chunk stub is invalid")
PY

"$BIN" "$TMP_DIR/output/r.4.-1.mca" --chunk 31 0 --dump "$TMP_DIR/rewritten.txt" >"$TMP_DIR/rewritten.log" 2>&1
grep -q "Int: 456" "$TMP_DIR/rewritten.txt"

mv "$TMP_DIR/output/c.159.-32.mcc" "$TMP_DIR/output/missing.mcc"
if "$BIN" "$TMP_DIR/output/r.4.-1.mca" --chunk 31 0 --dump "$TMP_DIR/missing.txt" >"$TMP_DIR/missing.log" 2>&1; then
  echo "Expected an external chunk with a missing .mcc file to fail"
  exit 1
fi
grep -q "Failed to load file" "$TMP_DIR/missing.log"

echo "All modern region tests passed"
