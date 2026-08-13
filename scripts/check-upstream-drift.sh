#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
baseline="$repo_dir/compat/upstream-source.sha256"
upstream_ref=${SOLC_UPSTREAM_REF:-master}
temporary_dir=$(mktemp -d "${TMPDIR:-/tmp}/solc-upstream.XXXXXX")
trap 'rm -rf "$temporary_dir"' EXIT HUP INT TERM

hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

failures=0
while read -r expected path; do
    case "$expected" in
        ''|'#'*) continue ;;
    esac
    destination="$temporary_dir/$(printf '%s' "$path" | tr '/' '_')"
    url="https://raw.githubusercontent.com/anza-xyz/solana-sdk/$upstream_ref/$path"
    curl -fsSL --retry 3 "$url" -o "$destination"
    actual=$(hash_file "$destination")
    if [ "$actual" != "$expected" ]; then
        printf '%s\n' "upstream drift: $path"
        printf '%s\n' "  expected $expected"
        printf '%s\n' "  actual   $actual"
        failures=$((failures + 1))
    else
        printf '%s\n' "unchanged: $path"
    fi
done < "$baseline"

if [ "$failures" -ne 0 ]; then
    printf '%s\n' "$failures upstream source file(s) changed; review before updating the baseline"
    exit 1
fi

printf '%s\n' "upstream wire-format sources match the recorded baseline"
