#!/usr/bin/env sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_bin="${TMPDIR:-/tmp}/c_nbt_extended_formats_$$"
trap 'rm -f "$test_bin"' EXIT HUP INT TERM

${CC:-cc} -std=c11 -Wall -Wextra -Wpedantic -I"$project_dir/h" \
    "$project_dir/tests/test_extended_formats.c" \
    "$project_dir/src/nbt_binary.c" \
    "$project_dir/src/snbt.c" \
    "$project_dir/src/nbt_builder.c" \
    "$project_dir/src/nbt_utils.c" \
    "$project_dir/src/platform.c" \
    -o "$test_bin"

"$test_bin"
