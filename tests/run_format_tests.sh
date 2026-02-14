#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BIN="${BIN:-./bin/nbt_explorer}"
INPUT="${1:-level.dat}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/nbt_format_tests.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

if [[ ! -x "$BIN" ]]; then
  echo "Missing binary: $BIN"
  echo "Run: make"
  exit 1
fi

if [[ ! -f "$INPUT" ]]; then
  echo "Missing input fixture: $INPUT"
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "Missing dependency: python3 (required for zlib fixture generation)"
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
    tail -n 80 "$file" || true
    exit 1
  fi
}

RAW_FILE="$TMP_DIR/raw.nbt"
ZLIB_FILE="$TMP_DIR/zlib.dat"

gzip -dc "$INPUT" >"$RAW_FILE"

python3 - "$RAW_FILE" "$ZLIB_FILE" <<'PY'
import pathlib
import sys
import zlib

raw = pathlib.Path(sys.argv[1]).read_bytes()
pathlib.Path(sys.argv[2]).write_bytes(zlib.compress(raw))
PY

echo "[1/4] Detect raw NBT for dump"
"$BIN" "$RAW_FILE" --dump "$TMP_DIR/raw_dump.txt" >"$TMP_DIR/raw_dump.log" 2>&1
assert_grep "Detected input format: raw" "$TMP_DIR/raw_dump.log"
assert_grep "Tag: Data \(Type 0A\)" "$TMP_DIR/raw_dump.txt"

echo "[2/4] Detect zlib NBT for dump"
"$BIN" "$ZLIB_FILE" --dump "$TMP_DIR/zlib_dump.txt" >"$TMP_DIR/zlib_dump.log" 2>&1
assert_grep "Detected input format: zlib" "$TMP_DIR/zlib_dump.log"
assert_grep "Tag: Data \(Type 0A\)" "$TMP_DIR/zlib_dump.txt"

echo "[3/4] Edit raw NBT input"
"$BIN" "$RAW_FILE" --edit "Data/SpawnX" "2468" --output "$TMP_DIR/raw_edit_out.dat" >"$TMP_DIR/raw_edit.log" 2>&1
"$BIN" "$TMP_DIR/raw_edit_out.dat" --dump "$TMP_DIR/raw_edit_dump.txt" >"$TMP_DIR/raw_edit_dump.log" 2>&1
assert_grep "Int: 2468" "$TMP_DIR/raw_edit_dump.txt"

echo "[4/4] Edit zlib NBT input"
"$BIN" "$ZLIB_FILE" --edit "Data/SpawnX" "1357" --output "$TMP_DIR/zlib_edit_out.dat" >"$TMP_DIR/zlib_edit.log" 2>&1
"$BIN" "$TMP_DIR/zlib_edit_out.dat" --dump "$TMP_DIR/zlib_edit_dump.txt" >"$TMP_DIR/zlib_edit_dump.log" 2>&1
assert_grep "Int: 1357" "$TMP_DIR/zlib_edit_dump.txt"

echo "All format tests passed"
