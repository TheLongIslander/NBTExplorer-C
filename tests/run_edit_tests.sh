#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BIN="$ROOT_DIR/bin/nbt_explorer"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/nbt_edit_tests.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT
INPUT="${1:-$TMP_DIR/level.dat}"
MODIFIED_OUTPUT="$TMP_DIR/modified_output.dat"
GENERATED_FIXTURE=0

if [[ ! -x "$BIN" ]]; then
  echo "Test setup failed: $BIN not found/executable"
  exit 1
fi

if [[ $# -eq 0 ]]; then
  if ! command -v python3 >/dev/null 2>&1; then
    echo "Test setup failed: python3 not found"
    exit 1
  fi
  python3 tests/generate_test_fixtures.py level "$INPUT"
  GENERATED_FIXTURE=1
fi

if [[ ! -f "$INPUT" ]]; then
  echo "Test setup failed: $INPUT not found"
  exit 1
fi
INPUT="$(cd "$(dirname "$INPUT")" && pwd -P)/$(basename "$INPUT")"

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
  (cd "$TMP_DIR" && "$BIN" "$INPUT" --edit "$path" "$value") >"$TMP_DIR/last_cmd.log" 2>&1
}

run_set() {
  local path="$1"
  local value="$2"
  (cd "$TMP_DIR" && "$BIN" "$INPUT" --set "$path" "$value") >"$TMP_DIR/last_set.log" 2>&1
}

run_delete() {
  local path="$1"
  (cd "$TMP_DIR" && "$BIN" "$INPUT" --delete "$path") >"$TMP_DIR/last_delete.log" 2>&1
}

dump_modified() {
  "$BIN" "$MODIFIED_OUTPUT" --dump "$TMP_DIR/dump.txt" >"$TMP_DIR/last_dump.log" 2>&1
}

expect_edit_fail() {
  local path="$1"
  local value="$2"
  local pattern="$3"

  if (cd "$TMP_DIR" && "$BIN" "$INPUT" --edit "$path" "$value") >"$TMP_DIR/last_fail.log" 2>&1; then
    echo "Expected failure but command succeeded: --edit $path $value"
    exit 1
  fi

  assert_grep "$pattern" "$TMP_DIR/last_fail.log"
}

expect_set_fail() {
  local path="$1"
  local value="$2"
  local pattern="$3"

  if (cd "$TMP_DIR" && "$BIN" "$INPUT" --set "$path" "$value") >"$TMP_DIR/last_set_fail.log" 2>&1; then
    echo "Expected failure but command succeeded: --set $path $value"
    exit 1
  fi

  assert_grep "$pattern" "$TMP_DIR/last_set_fail.log"
}

expect_delete_fail() {
  local path="$1"
  local pattern="$2"

  if (cd "$TMP_DIR" && "$BIN" "$INPUT" --delete "$path") >"$TMP_DIR/last_delete_fail.log" 2>&1; then
    echo "Expected failure but command succeeded: --delete $path"
    exit 1
  fi

  assert_grep "$pattern" "$TMP_DIR/last_delete_fail.log"
}

echo "[1/25] Numeric backward compatibility"
run_edit "Data/SpawnX" "1234"
dump_modified
assert_grep "Int: 1234" "$TMP_DIR/dump.txt"

echo "[2/25] String edit"
run_edit "Data/LevelName" '"world2"'
dump_modified
assert_grep "String: world2" "$TMP_DIR/dump.txt"

echo "[3/25] List element edit"
run_edit "Data/Player/Pos[1]" "70.0"
dump_modified
assert_grep "Double: 70\\.000000" "$TMP_DIR/dump.txt"

echo "[4/25] List whole replace"
run_edit "Data/DataPacks/Enabled" '["vanilla","fabric"]'
dump_modified
assert_grep "String: vanilla" "$TMP_DIR/dump.txt"
assert_grep "String: fabric" "$TMP_DIR/dump.txt"
assert_not_grep "String: file/bukkit" "$TMP_DIR/dump.txt"

echo "[5/25] Int array element edit"
run_edit "Data/Player/UUID[0]" "42"
dump_modified
assert_grep "Tag: UUID \(Type 0B\)" "$TMP_DIR/dump.txt"

echo "[6/25] Int array whole replace"
run_edit "Data/Player/UUID" "[1,2,3,4,5]"
dump_modified
assert_grep "Int_Array\[5\]" "$TMP_DIR/dump.txt"

echo "[7/25] Compound patch"
run_edit "Data" '{"SpawnX":1200,"LevelName":"world3"}'
dump_modified
assert_grep "Int: 1200" "$TMP_DIR/dump.txt"
assert_grep "String: world3" "$TMP_DIR/dump.txt"

echo "[8/25] Byte array whole replace"
if [[ "$GENERATED_FIXTURE" -eq 1 ]]; then
  run_edit "Data/Player/TestBytes" "[1,2,3]"
  dump_modified
  assert_grep "Byte_Array\[3\]" "$TMP_DIR/dump.txt"
else
  echo "Skip synthetic TestBytes tag for caller-provided fixture"
fi

echo "[9/25] Long array whole replace"
if [[ "$GENERATED_FIXTURE" -eq 1 ]]; then
  run_edit "Data/Player/TestLongs" "[1,2,3,4]"
  dump_modified
  assert_grep "Long_Array\[4\]" "$TMP_DIR/dump.txt"
else
  echo "Skip synthetic TestLongs tag for caller-provided fixture"
fi

echo "[10/25] Error: index out of bounds"
expect_edit_fail "Data/Player/UUID[99]" "1" "index out of bounds"

echo "[11/25] Error: wrong JSON type"
expect_edit_fail "Data/SpawnX" '"bad"' "type mismatch"

echo "[12/25] Error: unknown compound key"
expect_edit_fail "Data" '{"Nope":1}' "unknown compound key"

echo "[13/25] Error: numeric overflow"
expect_edit_fail "Data/SpawnX" "999999999999999999999" "numeric overflow"

echo "[14/25] Custom output path"
CUSTOM_OUT="$TMP_DIR/custom_output.dat"
"$BIN" "$INPUT" --edit "Data/SpawnX" "2222" --output "$CUSTOM_OUT" >"$TMP_DIR/custom_output_edit.log" 2>&1
"$BIN" "$CUSTOM_OUT" --dump "$TMP_DIR/custom_output_dump.txt" >"$TMP_DIR/custom_output_dump.log" 2>&1
assert_grep "Int: 2222" "$TMP_DIR/custom_output_dump.txt"

echo "[15/25] In-place edit with backup"
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

echo "[16/25] Set creates new tag"
run_set "Data/CodexSetInt" "4444"
dump_modified
assert_grep "Tag: CodexSetInt \(Type 03\)" "$TMP_DIR/dump.txt"
assert_grep "Int: 4444" "$TMP_DIR/dump.txt"

echo "[17/25] Set updates existing tag"
run_set "Data/SpawnX" "5555"
dump_modified
assert_grep "Int: 5555" "$TMP_DIR/dump.txt"

echo "[18/25] Set creates nested compound"
run_set "Data/CodexMeta" '{"Build":1,"Name":"codex"}'
dump_modified
assert_grep "Tag: CodexMeta \(Type 0A\)" "$TMP_DIR/dump.txt"
assert_grep "Tag: Build \(Type 03\)" "$TMP_DIR/dump.txt"
assert_grep "Tag: Name \(Type 08\)" "$TMP_DIR/dump.txt"
assert_grep "String: codex" "$TMP_DIR/dump.txt"

echo "[19/25] Delete removes created tag"
DELETE_INPUT="$TMP_DIR/delete_level.dat"
cp "$INPUT" "$DELETE_INPUT"
"$BIN" "$DELETE_INPUT" --set "Data/ToDelete" "8888" --in-place >"$TMP_DIR/delete_set.log" 2>&1
"$BIN" "$DELETE_INPUT" --delete "Data/ToDelete" --in-place >"$TMP_DIR/delete_cmd.log" 2>&1
"$BIN" "$DELETE_INPUT" --dump "$TMP_DIR/delete_dump.txt" >"$TMP_DIR/delete_dump.log" 2>&1
assert_not_grep "Tag: ToDelete \(Type 03\)" "$TMP_DIR/delete_dump.txt"

echo "[20/25] Delete removes list element"
DELETE_LIST_INPUT="$TMP_DIR/delete_list_level.dat"
cp "$INPUT" "$DELETE_LIST_INPUT"
"$BIN" "$DELETE_LIST_INPUT" --set "Data/DataPacks/Enabled" '["codex-delete-a","codex-delete-b"]' --in-place >"$TMP_DIR/delete_list_set.log" 2>&1
"$BIN" "$DELETE_LIST_INPUT" --delete "Data/DataPacks/Enabled[0]" --in-place >"$TMP_DIR/delete_list_cmd.log" 2>&1
"$BIN" "$DELETE_LIST_INPUT" --dump "$TMP_DIR/delete_list_dump.txt" >"$TMP_DIR/delete_list_dump.log" 2>&1
assert_not_grep "String: codex-delete-a" "$TMP_DIR/delete_list_dump.txt"
assert_grep "String: codex-delete-b" "$TMP_DIR/delete_list_dump.txt"

echo "[21/25] Error: set with missing parent"
expect_set_fail "Data/NoSuchParent/NewKey" "1" "path not found"

echo "[22/25] Error: delete missing path"
expect_delete_fail "Data/NoSuchKey" "path not found"

echo "[23/25] Quoted key path create and edit"
QUOTED_INPUT="$TMP_DIR/quoted_path_level.dat"
cp "$INPUT" "$QUOTED_INPUT"
"$BIN" "$QUOTED_INPUT" --set 'Data/"Codex/Key"' "101" --in-place >"$TMP_DIR/quoted_set.log" 2>&1
"$BIN" "$QUOTED_INPUT" --dump "$TMP_DIR/quoted_dump_1.txt" >"$TMP_DIR/quoted_dump_1.log" 2>&1
assert_grep "Tag: Codex/Key \(Type 03\)" "$TMP_DIR/quoted_dump_1.txt"
"$BIN" "$QUOTED_INPUT" --edit 'Data/"Codex/Key"' "202" --in-place >"$TMP_DIR/quoted_edit.log" 2>&1
"$BIN" "$QUOTED_INPUT" --dump "$TMP_DIR/quoted_dump_2.txt" >"$TMP_DIR/quoted_dump_2.log" 2>&1
assert_grep "Int: 202" "$TMP_DIR/quoted_dump_2.txt"

echo "[24/25] Wildcard list edit"
WILDCARD_EDIT_INPUT="$TMP_DIR/wildcard_edit_level.dat"
cp "$INPUT" "$WILDCARD_EDIT_INPUT"
"$BIN" "$WILDCARD_EDIT_INPUT" --set "Data/DataPacks/Enabled" '["codex-wild-a","codex-wild-b"]' --in-place >"$TMP_DIR/wildcard_edit_set.log" 2>&1
"$BIN" "$WILDCARD_EDIT_INPUT" --edit "Data/DataPacks/Enabled[*]" '"codex-wild-all"' --in-place >"$TMP_DIR/wildcard_edit_cmd.log" 2>&1
"$BIN" "$WILDCARD_EDIT_INPUT" --dump "$TMP_DIR/wildcard_edit_dump.txt" >"$TMP_DIR/wildcard_edit_dump.log" 2>&1
assert_not_grep "String: codex-wild-a$" "$TMP_DIR/wildcard_edit_dump.txt"
assert_not_grep "String: codex-wild-b$" "$TMP_DIR/wildcard_edit_dump.txt"
assert_grep "String: codex-wild-all" "$TMP_DIR/wildcard_edit_dump.txt"

echo "[25/25] Wildcard delete list elements"
WILDCARD_DELETE_INPUT="$TMP_DIR/wildcard_delete_level.dat"
cp "$INPUT" "$WILDCARD_DELETE_INPUT"
"$BIN" "$WILDCARD_DELETE_INPUT" --set "Data/DataPacks/Enabled" '["codex-del-a","codex-del-b"]' --in-place >"$TMP_DIR/wildcard_delete_set.log" 2>&1
"$BIN" "$WILDCARD_DELETE_INPUT" --delete "Data/DataPacks/Enabled[*]" --in-place >"$TMP_DIR/wildcard_delete_cmd.log" 2>&1
"$BIN" "$WILDCARD_DELETE_INPUT" --dump "$TMP_DIR/wildcard_delete_dump.txt" >"$TMP_DIR/wildcard_delete_dump.log" 2>&1
assert_not_grep "String: codex-del-a" "$TMP_DIR/wildcard_delete_dump.txt"
assert_not_grep "String: codex-del-b" "$TMP_DIR/wildcard_delete_dump.txt"

echo "All edit tests passed"
