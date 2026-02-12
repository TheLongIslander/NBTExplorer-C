#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BIN="./bin/nbt_explorer"
INPUT="level.dat"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/nbt_edit_tests.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

if [[ ! -x "$BIN" ]]; then
  echo "Test setup failed: $BIN not found/executable"
  exit 1
fi

if [[ ! -f "$INPUT" ]]; then
  echo "Test setup failed: $INPUT not found"
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

assert_not_grep() {
  local pattern="$1"
  local file="$2"
  if has_pattern "$pattern" "$file"; then
    echo "Assertion failed: unexpected pattern '$pattern' found in $file"
    tail -n 80 "$file" || true
    exit 1
  fi
}

run_edit() {
  local path="$1"
  local value="$2"
  "$BIN" "$INPUT" --edit "$path" "$value" >"$TMP_DIR/last_cmd.log" 2>&1
}

dump_modified() {
  "$BIN" modified_output.dat --dump "$TMP_DIR/dump.txt" >"$TMP_DIR/last_dump.log" 2>&1
}

expect_edit_fail() {
  local path="$1"
  local value="$2"
  local pattern="$3"

  if "$BIN" "$INPUT" --edit "$path" "$value" >"$TMP_DIR/last_fail.log" 2>&1; then
    echo "Expected failure but command succeeded: --edit $path $value"
    exit 1
  fi

  assert_grep "$pattern" "$TMP_DIR/last_fail.log"
}

echo "[1/11] Numeric backward compatibility"
run_edit "Data/SpawnX" "1234"
dump_modified
assert_grep "Int: 1234" "$TMP_DIR/dump.txt"

echo "[2/11] String edit"
run_edit "Data/LevelName" '"world2"'
dump_modified
assert_grep "String: world2" "$TMP_DIR/dump.txt"

echo "[3/11] List element edit"
run_edit "Data/Player/Pos[1]" "70.0"
dump_modified
assert_grep "Double: 70\\.000000" "$TMP_DIR/dump.txt"

echo "[4/11] List whole replace"
run_edit "Data/DataPacks/Enabled" '["vanilla","fabric"]'
dump_modified
assert_grep "String: vanilla" "$TMP_DIR/dump.txt"
assert_grep "String: fabric" "$TMP_DIR/dump.txt"
assert_not_grep "String: file/bukkit" "$TMP_DIR/dump.txt"

echo "[5/11] Int array element edit"
run_edit "Data/Player/UUID[0]" "42"
dump_modified
assert_grep "Tag: UUID \(Type 0B\)" "$TMP_DIR/dump.txt"

echo "[6/11] Int array whole replace"
run_edit "Data/Player/UUID" "[1,2,3,4,5]"
dump_modified
assert_grep "Int_Array\[5\]" "$TMP_DIR/dump.txt"

echo "[7/11] Compound patch"
run_edit "Data" '{"SpawnX":1200,"LevelName":"world3"}'
dump_modified
assert_grep "Int: 1200" "$TMP_DIR/dump.txt"
assert_grep "String: world3" "$TMP_DIR/dump.txt"

# Skipped for current fixture set:
# level.dat and the bundled player .dat do not contain TAG_Byte_Array (Type 07)
# or TAG_Long_Array (Type 0C), so these are documented but intentionally not run.
# Example commands (enable when a fixture has these tags):
# run_edit "Some/ByteArrayPath" "[1,2,3]"
# run_edit "Some/LongArrayPath" "[1,2,3]"

echo "[8/11] Error: index out of bounds"
expect_edit_fail "Data/Player/UUID[99]" "1" "index out of bounds"

echo "[9/11] Error: wrong JSON type"
expect_edit_fail "Data/SpawnX" '"bad"' "type mismatch"

echo "[10/11] Error: unknown compound key"
expect_edit_fail "Data" '{"Nope":1}' "unknown compound key"

echo "[11/11] Error: numeric overflow"
expect_edit_fail "Data/SpawnX" "999999999999999999999" "numeric overflow"

echo "All edit tests passed"
