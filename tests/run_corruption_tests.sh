#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BIN="${BIN:-./bin/nbt_explorer}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/nbt_corruption_tests.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

if [[ ! -x "$BIN" ]]; then
  echo "Missing binary: $BIN"
  echo "Run: make"
  exit 1
fi

expect_fail_no_crash() {
  local input_file="$1"
  local label="$2"
  local dump_file="$TMP_DIR/${label}.dump.txt"
  local log_file="$TMP_DIR/${label}.log"
  local rc

  set +e
  "$BIN" "$input_file" --dump "$dump_file" >"$log_file" 2>&1
  rc=$?
  set -e

  if [[ "$rc" -eq 0 ]]; then
    echo "Expected failure for malformed input '$label', but command succeeded"
    tail -n 80 "$log_file" || true
    exit 1
  fi

  if [[ "$rc" -ge 128 ]]; then
    echo "Process crashed for malformed input '$label' (exit code: $rc)"
    tail -n 80 "$log_file" || true
    exit 1
  fi
}

echo "[1/4] Non-gzip input"
printf 'this is not gzip nbt data' >"$TMP_DIR/not_gzip.dat"
expect_fail_no_crash "$TMP_DIR/not_gzip.dat" "not_gzip"

echo "[2/4] Truncated compound root"
printf '\x0A\x00\x00' | gzip -c >"$TMP_DIR/truncated_root.dat"
expect_fail_no_crash "$TMP_DIR/truncated_root.dat" "truncated_root"

echo "[3/4] Declared string length beyond buffer"
printf '\x0A\x00\x00\x08\x00\x01\x41\x00\x05\x61\x00' | gzip -c >"$TMP_DIR/bad_string_len.dat"
expect_fail_no_crash "$TMP_DIR/bad_string_len.dat" "bad_string_len"

echo "[4/4] Negative list length"
printf '\x0A\x00\x00\x09\x00\x01\x42\x08\xFF\xFF\xFF\xFF\x00' | gzip -c >"$TMP_DIR/negative_list_len.dat"
expect_fail_no_crash "$TMP_DIR/negative_list_len.dat" "negative_list_len"

echo "All corruption tests passed"
