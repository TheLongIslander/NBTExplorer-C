#!/usr/bin/env sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 /path/to/Amulet/libleveldb /path/to/disposable/test/db" >&2
    echo "The database is modified; never point this test at a real world." >&2
    exit 2
fi

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_bin="${TMPDIR:-/tmp}/c_nbt_bedrock_db_test_$$"
trap 'rm -f "$test_bin"' EXIT HUP INT TERM

${CC:-cc} -std=c11 -Wall -Wextra -Wpedantic -I"$project_dir/h" \
    "$project_dir/tests/test_bedrock_db.c" \
    "$project_dir/src/bedrock_db.c" \
    "$project_dir/src/nbt_binary.c" \
    "$project_dir/src/snbt.c" \
    "$project_dir/src/nbt_builder.c" \
    "$project_dir/src/nbt_utils.c" \
    "$project_dir/src/platform.c" \
    -o "$test_bin"

"$test_bin" "$1" "$2"
