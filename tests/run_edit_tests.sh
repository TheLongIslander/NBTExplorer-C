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

run_set() {
  local path="$1"
  local value="$2"
  "$BIN" "$INPUT" --set "$path" "$value" >"$TMP_DIR/last_set.log" 2>&1
}

run_delete() {
  local path="$1"
  "$BIN" "$INPUT" --delete "$path" >"$TMP_DIR/last_delete.log" 2>&1
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

expect_set_fail() {
  local path="$1"
  local value="$2"
  local pattern="$3"

  if "$BIN" "$INPUT" --set "$path" "$value" >"$TMP_DIR/last_set_fail.log" 2>&1; then
    echo "Expected failure but command succeeded: --set $path $value"
    exit 1
  fi

  assert_grep "$pattern" "$TMP_DIR/last_set_fail.log"
}

expect_delete_fail() {
  local path="$1"
  local pattern="$2"

  if "$BIN" "$INPUT" --delete "$path" >"$TMP_DIR/last_delete_fail.log" 2>&1; then
    echo "Expected failure but command succeeded: --delete $path"
    exit 1
  fi

  assert_grep "$pattern" "$TMP_DIR/last_delete_fail.log"
}

echo "[1/20] Numeric backward compatibility"
run_edit "Data/SpawnX" "1234"
dump_modified
assert_grep "Int: 1234" "$TMP_DIR/dump.txt"

echo "[2/20] String edit"
run_edit "Data/LevelName" '"world2"'
dump_modified
assert_grep "String: world2" "$TMP_DIR/dump.txt"

echo "[3/20] List element edit"
run_edit "Data/Player/Pos[1]" "70.0"
dump_modified
assert_grep "Double: 70\\.000000" "$TMP_DIR/dump.txt"

echo "[4/20] List whole replace"
run_edit "Data/DataPacks/Enabled" '["vanilla","fabric"]'
dump_modified
assert_grep "String: vanilla" "$TMP_DIR/dump.txt"
assert_grep "String: fabric" "$TMP_DIR/dump.txt"
assert_not_grep "String: file/bukkit" "$TMP_DIR/dump.txt"

echo "[5/20] Int array element edit"
run_edit "Data/Player/UUID[0]" "42"
dump_modified
assert_grep "Tag: UUID \(Type 0B\)" "$TMP_DIR/dump.txt"

echo "[6/20] Int array whole replace"
run_edit "Data/Player/UUID" "[1,2,3,4,5]"
dump_modified
assert_grep "Int_Array\[5\]" "$TMP_DIR/dump.txt"

echo "[7/20] Compound patch"
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

echo "[8/20] Error: index out of bounds"
expect_edit_fail "Data/Player/UUID[99]" "1" "index out of bounds"

echo "[9/20] Error: wrong JSON type"
expect_edit_fail "Data/SpawnX" '"bad"' "type mismatch"

echo "[10/20] Error: unknown compound key"
expect_edit_fail "Data" '{"Nope":1}' "unknown compound key"

echo "[11/20] Error: numeric overflow"
expect_edit_fail "Data/SpawnX" "999999999999999999999" "numeric overflow"

echo "[12/20] Custom output path"
CUSTOM_OUT="$TMP_DIR/custom_output.dat"
"$BIN" "$INPUT" --edit "Data/SpawnX" "2222" --output "$CUSTOM_OUT" >"$TMP_DIR/custom_output_edit.log" 2>&1
"$BIN" "$CUSTOM_OUT" --dump "$TMP_DIR/custom_output_dump.txt" >"$TMP_DIR/custom_output_dump.log" 2>&1
assert_grep "Int: 2222" "$TMP_DIR/custom_output_dump.txt"

echo "[13/20] In-place edit with backup"
INPLACE_INPUT="$TMP_DIR/inplace_level.dat"
cp "$INPUT" "$INPLACE_INPUT"
"$BIN" "$INPLACE_INPUT" --edit "Data/SpawnX" "3333" --in-place --backup=.orig >"$TMP_DIR/inplace_edit.log" 2>&1
if [[ ! -f "$INPLACE_INPUT.orig" ]]; then
  echo "Assertion failed: backup file was not created"
  exit 1
fi
if ! cmp -s "$INPUT" "$INPLACE_INPUT.orig"; then
  echo "Assertion failed: backup file does not match original input"
  exit 1
fi
"$BIN" "$INPLACE_INPUT" --dump "$TMP_DIR/inplace_dump.txt" >"$TMP_DIR/inplace_dump.log" 2>&1
assert_grep "Int: 3333" "$TMP_DIR/inplace_dump.txt"

echo "[14/20] Set creates new tag"
run_set "Data/CodexSetInt" "4444"
dump_modified
assert_grep "Tag: CodexSetInt \(Type 03\)" "$TMP_DIR/dump.txt"
assert_grep "Int: 4444" "$TMP_DIR/dump.txt"

echo "[15/20] Set updates existing tag"
run_set "Data/SpawnX" "5555"
dump_modified
assert_grep "Int: 5555" "$TMP_DIR/dump.txt"

echo "[16/20] Set creates nested compound"
run_set "Data/CodexMeta" '{"Build":1,"Name":"codex"}'
dump_modified
assert_grep "Tag: CodexMeta \(Type 0A\)" "$TMP_DIR/dump.txt"
assert_grep "Tag: Build \(Type 03\)" "$TMP_DIR/dump.txt"
assert_grep "Tag: Name \(Type 08\)" "$TMP_DIR/dump.txt"
assert_grep "String: codex" "$TMP_DIR/dump.txt"

echo "[17/20] Delete removes created tag"
DELETE_INPUT="$TMP_DIR/delete_level.dat"
cp "$INPUT" "$DELETE_INPUT"
"$BIN" "$DELETE_INPUT" --set "Data/ToDelete" "8888" --in-place >"$TMP_DIR/delete_set.log" 2>&1
"$BIN" "$DELETE_INPUT" --delete "Data/ToDelete" --in-place >"$TMP_DIR/delete_cmd.log" 2>&1
"$BIN" "$DELETE_INPUT" --dump "$TMP_DIR/delete_dump.txt" >"$TMP_DIR/delete_dump.log" 2>&1
assert_not_grep "Tag: ToDelete \(Type 03\)" "$TMP_DIR/delete_dump.txt"

echo "[18/20] Delete removes list element"
DELETE_LIST_INPUT="$TMP_DIR/delete_list_level.dat"
cp "$INPUT" "$DELETE_LIST_INPUT"
"$BIN" "$DELETE_LIST_INPUT" --set "Data/DataPacks/Enabled" '["codex-delete-a","codex-delete-b"]' --in-place >"$TMP_DIR/delete_list_set.log" 2>&1
"$BIN" "$DELETE_LIST_INPUT" --delete "Data/DataPacks/Enabled[0]" --in-place >"$TMP_DIR/delete_list_cmd.log" 2>&1
"$BIN" "$DELETE_LIST_INPUT" --dump "$TMP_DIR/delete_list_dump.txt" >"$TMP_DIR/delete_list_dump.log" 2>&1
assert_not_grep "String: codex-delete-a" "$TMP_DIR/delete_list_dump.txt"
assert_grep "String: codex-delete-b" "$TMP_DIR/delete_list_dump.txt"

echo "[19/20] Error: set with missing parent"
expect_set_fail "Data/NoSuchParent/NewKey" "1" "path not found"

echo "[20/20] Error: delete missing path"
expect_delete_fail "Data/NoSuchKey" "path not found"

echo "All edit tests passed"
