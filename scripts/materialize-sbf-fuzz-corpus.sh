#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    printf '%s\n' "usage: materialize-sbf-fuzz-corpus.sh OUTPUT_DIRECTORY" >&2
    exit 2
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
output_dir=$1
mkdir -p "$output_dir"

for vector in "$repo_dir"/tests/vectors/sbf/*.hex; do
    name=$(basename "$vector" .hex)
    sed '/^[[:space:]]*#/d' "$vector" \
        | tr -d '[:space:]' \
        | xxd -r -p > "$output_dir/$name"
done

printf '%s\n' "materialized SBF entrypoint fuzz seeds in $output_dir"
