#!/usr/bin/env bash
set -euo pipefail

BIN="${BIN:-./bin/nbt_explorer}"
INPUT="${1:-level.dat}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/nbt_verify_all_types.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

if [[ ! -x "$BIN" ]]; then
  echo "Missing binary: $BIN"
  echo "Run: make"
  exit 1
fi

if [[ ! -f "$INPUT" ]]; then
  echo "Missing input file: $INPUT"
  exit 1
fi

ORIG_DUMP="$TMP_DIR/original_dump.txt"
"$BIN" "$INPUT" --dump "$ORIG_DUMP" >/dev/null 2>&1

find_line() {
  local pattern="$1"
  local file="$2"
  if command -v rg >/dev/null 2>&1; then
    rg -n "$pattern" "$file" | head -n 1 | cut -d: -f1
  else
    grep -nE "$pattern" "$file" | head -n 1 | cut -d: -f1
  fi
}

show_anchor_block() {
  local file="$1"
  local anchor="$2"
  local extra_lines="$3"
  local line

  line="$(find_line "$anchor" "$file" || true)"
  if [[ -z "$line" ]]; then
    echo "  (anchor not found: $anchor)"
    return
  fi

  sed -n "${line},$((line + extra_lines))p" "$file" | sed 's/^/  /'
}

run_case() {
  local id="$1"
  local title="$2"
  local path="$3"
  local value="$4"
  local anchor="$5"
  local lines="$6"
  local mod_dump="$TMP_DIR/${id}_modified_dump.txt"

  echo
  echo "== $title =="
  echo "Path: $path"
  echo "Value: $value"
  echo "Original:"
  show_anchor_block "$ORIG_DUMP" "$anchor" "$lines"

  if ! "$BIN" "$INPUT" --edit "$path" "$value" >"$TMP_DIR/${id}_edit.log" 2>&1; then
    echo "Edit failed for $title"
    cat "$TMP_DIR/${id}_edit.log"
    exit 1
  fi

  "$BIN" modified_output.dat --dump "$mod_dump" >/dev/null 2>&1

  echo "Modified:"
  show_anchor_block "$mod_dump" "$anchor" "$lines"
}

run_case "byte" "TAG_Byte" "Data/Difficulty" "4" "Tag: Difficulty \\(Type 01\\)" 1
run_case "short" "TAG_Short" "Data/Player/Air" "250" "Tag: Air \\(Type 02\\)" 1
run_case "int" "TAG_Int" "Data/SpawnX" "1234" "Tag: SpawnX \\(Type 03\\)" 1
run_case "long" "TAG_Long" "Data/LastPlayed" "1742628144751" "Tag: LastPlayed \\(Type 04\\)" 1
run_case "float" "TAG_Float" "Data/SpawnAngle" "12.5" "Tag: SpawnAngle \\(Type 05\\)" 1
run_case "double" "TAG_Double" "Data/BorderSize" "60000001.25" "Tag: BorderSize \\(Type 06\\)" 1
run_case "string" "TAG_String" "Data/LevelName" '"world2"' "Tag: LevelName \\(Type 08\\)" 1

run_case "list_whole" "TAG_List (whole replace)" "Data/DataPacks/Enabled" '["vanilla","fabric"]' "Tag: Enabled \\(Type 09\\)" 10
run_case "list_elem" "TAG_List (element edit)" "Data/Player/Pos[1]" "70.0" "Tag: Pos \\(Type 09\\)" 8

run_case "int_array" "TAG_Int_Array" "Data/Player/UUID" "[1,2,3,4,5]" "Tag: UUID \\(Type 0B\\)" 2
run_case "compound" "TAG_Compound (patch existing keys)" "Data" '{"SpawnX":1200,"LevelName":"world3"}' "Tag: SpawnX \\(Type 03\\)" 1

echo
line="$(find_line "Tag: LevelName \\(Type 08\\)" "$TMP_DIR/compound_modified_dump.txt" || true)"
if [[ -n "$line" ]]; then
  echo "Compound patch also changed LevelName:" 
  sed -n "${line},$((line + 1))p" "$TMP_DIR/compound_modified_dump.txt" | sed 's/^/  /'
fi

echo
if [[ -n "${BYTE_ARRAY_PATH:-}" && -n "${BYTE_ARRAY_VALUE:-}" && -n "${BYTE_ARRAY_ANCHOR:-}" ]]; then
  run_case "byte_array" "TAG_Byte_Array (optional)" "$BYTE_ARRAY_PATH" "$BYTE_ARRAY_VALUE" "$BYTE_ARRAY_ANCHOR" "${BYTE_ARRAY_LINES:-2}"
else
  echo "Skipped TAG_Byte_Array: set BYTE_ARRAY_PATH, BYTE_ARRAY_VALUE, BYTE_ARRAY_ANCHOR to test it."
fi

if [[ -n "${LONG_ARRAY_PATH:-}" && -n "${LONG_ARRAY_VALUE:-}" && -n "${LONG_ARRAY_ANCHOR:-}" ]]; then
  run_case "long_array" "TAG_Long_Array (optional)" "$LONG_ARRAY_PATH" "$LONG_ARRAY_VALUE" "$LONG_ARRAY_ANCHOR" "${LONG_ARRAY_LINES:-2}"
else
  echo "Skipped TAG_Long_Array: set LONG_ARRAY_PATH, LONG_ARRAY_VALUE, LONG_ARRAY_ANCHOR to test it."
fi

echo
printf 'Done.\nTemporary files were kept at: %s\n' "$TMP_DIR"
trap - EXIT
