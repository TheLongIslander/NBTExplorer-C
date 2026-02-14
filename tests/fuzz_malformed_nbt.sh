#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BIN="${BIN:-./bin/nbt_explorer}"
INPUT="${1:-level.dat}"
ITERATIONS="${ITERATIONS:-80}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/nbt_fuzz.XXXXXX")"
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

RAW_FILE="$TMP_DIR/raw.nbt"
gzip -dc "$INPUT" >"$RAW_FILE"
RAW_SIZE="$(wc -c <"$RAW_FILE")"

if [[ "$RAW_SIZE" -le 0 ]]; then
  echo "Could not decode non-empty raw NBT from $INPUT"
  exit 1
fi

run_no_crash() {
  local case_file="$1"
  local case_id="$2"
  local rc

  set +e
  "$BIN" "$case_file" --dump "$TMP_DIR/${case_id}.dump.txt" >"$TMP_DIR/${case_id}.log" 2>&1
  rc=$?
  set -e

  if [[ "$rc" -ge 128 ]]; then
    echo "Crash detected in fuzz case '$case_id' (exit code: $rc)"
    tail -n 80 "$TMP_DIR/${case_id}.log" || true
    exit 1
  fi
}

echo "Running malformed-NBT fuzz smoke ($ITERATIONS cases)..."
for i in $(seq 1 "$ITERATIONS"); do
  case_id="fuzz_${i}"
  case_file="$TMP_DIR/${case_id}.dat"
  mode=$((i % 3))

  if [[ "$mode" -eq 0 ]]; then
    cut_len=$((RANDOM % (RAW_SIZE + 1)))
    head -c "$cut_len" "$RAW_FILE" | gzip -c >"$case_file"
  elif [[ "$mode" -eq 1 ]]; then
    mut_file="$TMP_DIR/${case_id}.raw"
    cp "$RAW_FILE" "$mut_file"
    pos=$((RANDOM % RAW_SIZE))
    dd if=/dev/urandom of="$mut_file" bs=1 count=1 seek="$pos" conv=notrunc status=none
    gzip -c "$mut_file" >"$case_file"
  else
    rand_len=$((RANDOM % 256))
    dd if=/dev/urandom bs=1 count="$rand_len" status=none | gzip -c >"$case_file"
  fi

  run_no_crash "$case_file" "$case_id"
done

echo "Fuzz smoke passed: no crashes across $ITERATIONS malformed cases"
