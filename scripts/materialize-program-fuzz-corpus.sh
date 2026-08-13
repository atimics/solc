#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
destination=${1:-"$repo_dir/build-program-fuzz-corpus"}

mkdir -p "$destination"

case_index=0
for vector in "$repo_dir"/tests/vectors/programs/*.hex; do
    case_index=$((case_index + 1))
    selector=$((case_index % 10))
    output="$destination/case-$case_index"
    printf "\\$(printf '%03o' "$selector")" > "$output"
    tr -d '[:space:]' < "$vector" | xxd -r -p >> "$output"
done

for vector in \
    "$repo_dir/tests/vectors/migrations/rati-attestation.hex" \
    "$repo_dir/tests/vectors/migrations/rati-chain-proof.hex" \
    "$repo_dir/tests/vectors/migrations/rati-chain-proof-payload-mismatch.invalid.hex"
do
    case_index=$((case_index + 1))
    output="$destination/case-$case_index"
    printf '\011' > "$output"
    sed 's/#.*//' "$vector" | tr -d '[:space:]' | xxd -r -p >> "$output"
done

printf '%s\n' "materialized $case_index program corpus entries in $destination"
