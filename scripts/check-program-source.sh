#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
baseline="$repo_dir/compat/program-source.sha256"
cargo_root=${CARGO_HOME:-"$HOME/.cargo"}
registry_root="$cargo_root/registry/src"

hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

while read -r expected crate version source_path; do
    case "$expected" in
        ''|'#'*) continue ;;
    esac
    crate_dir=$(find "$registry_root" -mindepth 2 -maxdepth 2 -type d \
        -name "$crate-$version" -print | head -n 1)
    if [ -z "$crate_dir" ]; then
        printf '%s\n' "missing exact crate source: $crate $version"
        exit 1
    fi
    source_file="$crate_dir/$source_path"
    actual=$(hash_file "$source_file")
    if [ "$actual" != "$expected" ]; then
        printf '%s\n' "program source mismatch: $crate $version $source_path"
        printf '%s\n' "  expected $expected"
        printf '%s\n' "  actual   $actual"
        exit 1
    fi
    printf '%s\n' "verified $crate $version $source_path"
done < "$baseline"

printf '%s\n' "official program-format sources match the reviewed baseline"
