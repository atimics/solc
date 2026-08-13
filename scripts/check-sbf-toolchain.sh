#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
manifest="$project_root/compat/sbf-toolchain.toml"
sbf_sdk_path=${SBF_SDK_PATH:-}
if [ -z "$sbf_sdk_path" ]; then
    solana_binary=$(command -v solana || true)
    if [ -z "$solana_binary" ]; then
        echo "solana CLI not found; set SBF_SDK_PATH" >&2
        exit 1
    fi
    sbf_sdk_path=$(dirname "$solana_binary")/platform-tools-sdk/sbf
fi

manifest_string() {
    key=$1
    awk -F '"' -v key="$key" '$0 ~ "^" key " =" { print $2; exit }' "$manifest"
}

manifest_number() {
    key=$1
    awk -F '= ' -v key="$key" '$0 ~ "^" key " =" { print $2; exit }' "$manifest"
}

hash_file() {
    file=$1
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$file" | awk '{print $1}'
    else
        shasum -a 256 "$file" | awk '{print $1}'
    fi
}

check_hash() {
    label=$1
    file=$2
    key=$3
    expected=$(manifest_string "$key")
    actual=$(hash_file "$file")
    if [ "$actual" != "$expected" ]; then
        echo "$label hash mismatch: expected $expected, got $actual" >&2
        exit 1
    fi
    echo "unchanged: $label"
}

check_hash deserialize.h "$sbf_sdk_path/c/inc/sol/deserialize.h" deserialize_h
check_hash entrypoint.h "$sbf_sdk_path/c/inc/sol/entrypoint.h" entrypoint_h
check_hash cpi.h "$sbf_sdk_path/c/inc/sol/cpi.h" cpi_h
check_hash pubkey.h "$sbf_sdk_path/c/inc/sol/pubkey.h" pubkey_h
check_hash sbf.ld "$sbf_sdk_path/c/sbf.ld" sbf_ld

build_output=$(
    "$project_root/scripts/build-reference-sbf.sh" \
        "$project_root/build/sbf/solc_reference.so"
)
actual_bytes=$(printf '%s\n' "$build_output" | awk -F= '$1 == "bytes" {print $2}')
actual_sha256=$(printf '%s\n' "$build_output" | awk -F= '$1 == "sha256" {print $2}')
expected_bytes=$(manifest_number bytes)
expected_sha256=$(manifest_string sha256)
if [ "$actual_bytes" != "$expected_bytes" ]; then
    echo "SBF artifact size mismatch: expected $expected_bytes, got $actual_bytes" >&2
    exit 1
fi
if [ "$actual_sha256" != "$expected_sha256" ]; then
    echo "SBF artifact hash mismatch: expected $expected_sha256, got $actual_sha256" >&2
    exit 1
fi
printf '%s\n' "$build_output"
echo "SBF SDK headers and reference artifact match the pinned manifest"
