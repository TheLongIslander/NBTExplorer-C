#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BIN="${BIN:-./bin/nbt_explorer}"
CC_BIN="${CC:-cc}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/nbt_cubic_region_tests.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT
ZLIB_LINK=(-lz)

if [[ "$(uname -s)" == "Darwin" ]] && command -v xcrun >/dev/null 2>&1; then
  ZLIB_LINK=(-L"$(xcrun --sdk macosx --show-sdk-path)/usr/lib" -lz)
fi

if [[ ! -x "$BIN" ]]; then
  echo "Missing binary: $BIN"
  exit 1
fi

"$CC_BIN" -std=c11 -Wall -Wextra -Wpedantic -Ih \
  tests/test_cubic_region.c src/region_file.c src/region_lz4.c \
  src/region_write.c src/platform.c "${ZLIB_LINK[@]}" -o "$TMP_DIR/test_cubic_region"
"$TMP_DIR/test_cubic_region" "$TMP_DIR/r2.0.0.0.mca"

python3 - "$TMP_DIR" <<'PY'
import gzip
import pathlib
import struct
import sys
import zlib

tmp = pathlib.Path(sys.argv[1])
SECTOR = 256
HEADER = 8192

def nbt(x_pos, y_pos, z_pos):
    out = bytearray(b"\x0a\x00\x00")
    for name, value in ((b"xPos", x_pos), (b"yPos", y_pos), (b"zPos", z_pos)):
        out += b"\x03" + struct.pack(">H", len(name)) + name + struct.pack(">i", value)
    out += b"\x00"
    return bytes(out)

def make_region():
    data = bytearray(HEADER)
    next_sector = HEADER // SECTOR
    chunks = [
        (5 + 7 * 32, 2, zlib.compress(nbt(-91, 64, 777)), 0x01020304),
        (31, 1, gzip.compress(nbt(31, 4, 0)), 0x05060708),
    ]
    for index, compression, payload, timestamp in chunks:
        sectors = (len(payload) + 5 + SECTOR - 1) // SECTOR
        struct.pack_into(">I", data, index * 4, (next_sector << 8) | sectors)
        struct.pack_into(">I", data, 4096 + index * 4, timestamp)
        required = (next_sector + sectors) * SECTOR
        data.extend(bytes(required - len(data)))
        start = next_sector * SECTOR
        struct.pack_into(">I", data, start, len(payload) + 1)
        data[start + 4] = compression
        data[start + 5:start + 5 + len(payload)] = payload
        next_sector += sectors
    return data

region = make_region()
(tmp / "r2.-3.4.8.mca").write_bytes(region)
(tmp / "r2.-3.4.8.mcr").write_bytes(region)

bad_header = bytearray(region)
struct.pack_into(">I", bad_header, (5 + 7 * 32) * 4, (2 << 8) | 1)
(tmp / "r2.0.0.0.mca").write_bytes(bad_header)

external = bytearray(HEADER + SECTOR)
struct.pack_into(">I", external, 0, (32 << 8) | 1)
struct.pack_into(">I", external, HEADER, 1)
external[HEADER + 4] = 0x82
(tmp / "r2.1.2.3.mca").write_bytes(external)
PY

"$BIN" "$TMP_DIR/r2.-3.4.8.mca" --list-chunks >"$TMP_DIR/list.log" 2>&1
grep -Eq '^5[[:space:]]+7[[:space:]]+zlib[[:space:]]+inline' "$TMP_DIR/list.log"
grep -Eq '^31[[:space:]]+0[[:space:]]+gzip[[:space:]]+inline' "$TMP_DIR/list.log"

"$BIN" "$TMP_DIR/r2.-3.4.8.mca" --chunk 5 7 --dump "$TMP_DIR/zlib.txt" >"$TMP_DIR/zlib.log" 2>&1
grep -q "Detected input format: zlib" "$TMP_DIR/zlib.log"
grep -q "Int: -91" "$TMP_DIR/zlib.txt"
grep -q "Int: 64" "$TMP_DIR/zlib.txt"
grep -q "Int: 777" "$TMP_DIR/zlib.txt"

"$BIN" "$TMP_DIR/r2.-3.4.8.mcr" --chunk 31 0 --dump "$TMP_DIR/gzip.txt" >"$TMP_DIR/gzip.log" 2>&1
grep -q "Detected input format: gzip" "$TMP_DIR/gzip.log"
grep -q "Int: 31" "$TMP_DIR/gzip.txt"

mkdir "$TMP_DIR/output"
"$BIN" "$TMP_DIR/r2.-3.4.8.mca" --chunk 5 7 --set xPos 456 \
  --output "$TMP_DIR/output/r2.9.-10.11.mca" >"$TMP_DIR/write.log" 2>&1

python3 - "$TMP_DIR/output/r2.9.-10.11.mca" <<'PY'
import pathlib
import struct
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
if len(data) % 256 or len(data) % 4096 == 0:
    raise SystemExit(f"cubic r2 output has unexpected padding: {len(data)} bytes")

expected = {31: 1, 5 + 7 * 32: 2}
for index in range(1024):
    location = struct.unpack_from(">I", data, index * 4)[0]
    if index not in expected:
        if location != 0:
            raise SystemExit(f"unexpected location entry {index}: 0x{location:08x}")
        continue
    offset = location >> 8
    count = location & 0xFF
    if offset < 32 or count != 1:
        raise SystemExit(f"bad cubic allocation for slot {index}: offset={offset}, count={count}")
    start = offset * 256
    length = struct.unpack_from(">I", data, start)[0]
    if length < 2 or data[start + 4] != expected[index]:
        raise SystemExit(f"bad chunk record for slot {index}")

if (struct.unpack_from(">I", data, 31 * 4)[0] >> 8) != 32:
    raise SystemExit("writer did not start cube data after all 32 header sectors")
if struct.unpack_from(">I", data, 4096 + 31 * 4)[0] != 0x05060708:
    raise SystemExit("timestamp table was not preserved at its fixed 4096-byte offset")
if struct.unpack_from(">I", data, 4096 + (5 + 7 * 32) * 4)[0] == 0x01020304:
    raise SystemExit("edited cube timestamp was not updated")
PY

"$BIN" "$TMP_DIR/output/r2.9.-10.11.mca" --chunk 5 7 --dump "$TMP_DIR/rewritten.txt" >"$TMP_DIR/rewritten.log" 2>&1
grep -q "Int: 456" "$TMP_DIR/rewritten.txt"

cp "$TMP_DIR/r2.-3.4.8.mcr" "$TMP_DIR/output/r2.12.13.14.mcr"
cp "$TMP_DIR/r2.-3.4.8.mcr" "$TMP_DIR/original.mcr"
"$BIN" "$TMP_DIR/output/r2.12.13.14.mcr" --chunk 31 0 --set yPos 99 \
  --in-place --backup >"$TMP_DIR/in_place.log" 2>&1
cmp "$TMP_DIR/original.mcr" "$TMP_DIR/output/r2.12.13.14.mcr.bak"
"$BIN" "$TMP_DIR/output/r2.12.13.14.mcr" --chunk 31 0 --dump "$TMP_DIR/in_place.txt" >"$TMP_DIR/in_place_read.log" 2>&1
grep -q "Int: 99" "$TMP_DIR/in_place.txt"

if "$BIN" "$TMP_DIR/r2.0.0.0.mca" --chunk 5 7 --validate >"$TMP_DIR/header.log" 2>&1; then
  echo "Expected a cubic chunk pointing into the 8 KiB header to fail"
  exit 1
fi
grep -q "chunk points into header sectors" "$TMP_DIR/header.log"

if "$BIN" "$TMP_DIR/r2.1.2.3.mca" --chunk 0 0 --validate >"$TMP_DIR/external.log" 2>&1; then
  echo "Expected a cubic external chunk stub to fail"
  exit 1
fi
grep -q "external chunk storage is not defined" "$TMP_DIR/external.log"

if "$BIN" "$TMP_DIR/r2.-3.4.8.mca" --chunk 5 7 --set xPos 1 \
    --output "$TMP_DIR/output/mislabeled.mca" >"$TMP_DIR/mislabeled.log" 2>&1; then
  echo "Expected cubic data written to a non-r2 filename to fail"
  exit 1
fi
grep -q "cubic r2 output requires" "$TMP_DIR/mislabeled.log"

echo "All legacy cubic r2 region tests passed"
