#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BIN="${BIN:-./bin/nbt_explorer}"
MCA_FILE="${MCA_FILE:-r.-9.3.mca}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/nbt_region_tests.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

if [[ ! -x "$BIN" ]]; then
  echo "Missing binary: $BIN"
  echo "Run: make"
  exit 1
fi

if [[ ! -f "$MCA_FILE" ]]; then
  echo "Skip region tests: missing fixture $MCA_FILE"
  exit 0
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "Missing dependency: python3"
  exit 1
fi

has_pattern() {
  local pattern="$1"
  local file="$2"
  if command -v rg >/dev/null 2>&1; then
    rg -q "$pattern" "$file"
  else
    grep -qE "$pattern" "$file"
  fi
}

assert_grep() {
  local pattern="$1"
  local file="$2"
  if ! has_pattern "$pattern" "$file"; then
    echo "Assertion failed: pattern '$pattern' not found in $file"
    tail -n 120 "$file" || true
    exit 1
  fi
}

assert_python_region_valid() {
  local region_path="$1"
  python3 - "$region_path" <<'PY'
import pathlib
import struct
import sys

path = pathlib.Path(sys.argv[1])
data = path.read_bytes()
if len(data) < 8192:
    raise SystemExit("region too small")

sectors_total = (len(data) + 4095) // 4096
used = [False] * sectors_total
used[0] = True
used[1] = True

count = 0
for i in range(1024):
    loc = struct.unpack_from(">I", data, i * 4)[0]
    off = (loc >> 8) & 0xFFFFFF
    cnt = loc & 0xFF

    if off == 0 and cnt == 0:
        continue
    if off == 0 or cnt == 0:
        raise SystemExit(f"invalid off/cnt pair at index {i}")
    if off < 2:
        raise SystemExit(f"chunk {i} points into header")
    if off + cnt > sectors_total:
        raise SystemExit(f"chunk {i} points outside file")

    for s in range(off, off + cnt):
        if used[s]:
            raise SystemExit(f"overlap at sector {s}")
        used[s] = True

    start = off * 4096
    length = struct.unpack_from(">I", data, start)[0]
    comp = data[start + 4]

    if length < 1:
        raise SystemExit(f"chunk {i} has invalid length")
    if length + 4 > cnt * 4096:
        raise SystemExit(f"chunk {i} length exceeds allocation")
    if comp not in (1, 2, 3):
        raise SystemExit(f"chunk {i} has unsupported compression {comp}")

    count += 1

print(count)
PY
}

extract_first_xpos() {
  local dump_file="$1"
  awk '
    /Tag: xPos \(Type 03\)/ {capture=1; next}
    capture && /Int:/ {print $2; exit}
  ' "$dump_file"
}

orig_dump="$TMP_DIR/orig_dump.txt"
orig_log="$TMP_DIR/orig.log"


echo "[1/7] Load .mca and dump selected chunk"
"$BIN" "$MCA_FILE" --chunk 0 0 --dump "$orig_dump" >"$orig_log" 2>&1
assert_grep "Detected source: mca_chunk" "$orig_log"
assert_grep "Using region chunk \(0, 0\)" "$orig_log"
assert_grep "Tag: Level \(Type 0A\)" "$orig_dump"

orig_xpos="$(extract_first_xpos "$orig_dump")"
if [[ -z "$orig_xpos" ]]; then
  echo "Failed to locate Level/xPos in chunk dump"
  exit 1
fi

orig_count="$(assert_python_region_valid "$MCA_FILE")"


echo "[2/7] Edit chunk and write full .mca output"
edited_region="$TMP_DIR/edited_region.mca"
"$BIN" "$MCA_FILE" --chunk 0 0 --set "Level/xPos" "12345" --output "$edited_region" >"$TMP_DIR/edit_out.log" 2>&1
"$BIN" "$edited_region" --chunk 0 0 --dump "$TMP_DIR/edited_dump.txt" >"$TMP_DIR/edited_dump.log" 2>&1
assert_grep "Int: 12345" "$TMP_DIR/edited_dump.txt"
assert_python_region_valid "$edited_region" >/dev/null


echo "[3/7] In-place .mca edit with backup"
cp "$MCA_FILE" "$TMP_DIR/in_place.mca"
"$BIN" "$TMP_DIR/in_place.mca" --chunk 0 0 --set "Level/xPos" "22222" --in-place --backup >"$TMP_DIR/in_place.log" 2>&1
assert_grep "Created backup:" "$TMP_DIR/in_place.log"
if [[ ! -f "$TMP_DIR/in_place.mca.bak" ]]; then
  echo "Expected backup file was not created"
  exit 1
fi
"$BIN" "$TMP_DIR/in_place.mca" --chunk 0 0 --dump "$TMP_DIR/in_place_dump.txt" >"$TMP_DIR/in_place_dump.log" 2>&1
assert_grep "Int: 22222" "$TMP_DIR/in_place_dump.txt"


echo "[4/7] Idempotence sanity (chunk count preserved on no-op write)"
no_op_region="$TMP_DIR/no_op_region.mca"
"$BIN" "$MCA_FILE" --chunk 0 0 --set "Level/xPos" "$orig_xpos" --output "$no_op_region" >"$TMP_DIR/no_op.log" 2>&1
new_count="$(assert_python_region_valid "$no_op_region")"
if [[ "$orig_count" != "$new_count" ]]; then
  echo "Chunk count changed after no-op region rewrite: $orig_count -> $new_count"
  exit 1
fi


echo "[5/7] Reject --in-place .mca without explicit --chunk"
if "$BIN" "$MCA_FILE" --set "Level/xPos" "1" --in-place >"$TMP_DIR/missing_chunk.log" 2>&1; then
  echo "Expected command to fail without explicit --chunk"
  exit 1
fi
assert_grep "requires explicit --chunk" "$TMP_DIR/missing_chunk.log"


echo "[6/7] Corruption test: out-of-range chunk offset"
python3 - "$MCA_FILE" "$TMP_DIR/corrupt_oob.mca" <<'PY'
import pathlib
import struct
import sys

src = pathlib.Path(sys.argv[1]).read_bytes()
data = bytearray(src)
# force chunk (0,0) entry to a clearly out-of-range sector offset
struct.pack_into(">I", data, 0, (0x00FFFF << 8) | 1)
pathlib.Path(sys.argv[2]).write_bytes(data)
PY
if "$BIN" "$TMP_DIR/corrupt_oob.mca" --chunk 0 0 --dump "$TMP_DIR/ignore.txt" >"$TMP_DIR/corrupt_oob.log" 2>&1; then
  echo "Expected corrupt out-of-range region file to fail"
  exit 1
fi
assert_grep "Failed to load file" "$TMP_DIR/corrupt_oob.log"


echo "[7/7] Corruption test: overlapping sector allocations"
python3 - "$MCA_FILE" "$TMP_DIR/corrupt_overlap.mca" <<'PY'
import pathlib
import struct
import sys

src = pathlib.Path(sys.argv[1]).read_bytes()
data = bytearray(src)
entry0 = struct.unpack_from(">I", data, 0)[0]
# Copy chunk(0,0) location into chunk(1,0) to force overlap.
struct.pack_into(">I", data, 4, entry0)
pathlib.Path(sys.argv[2]).write_bytes(data)
PY
if "$BIN" "$TMP_DIR/corrupt_overlap.mca" --dump "$TMP_DIR/ignore2.txt" >"$TMP_DIR/corrupt_overlap.log" 2>&1; then
  echo "Expected corrupt overlapping region file to fail"
  exit 1
fi
assert_grep "Failed to load file" "$TMP_DIR/corrupt_overlap.log"

echo "All region tests passed"
