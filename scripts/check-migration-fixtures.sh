#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

check_manifest() {
    manifest=$1
    base=$2
    checked=0
    skipped=0
    while read -r expected path; do
        case "$expected" in
            ''|'#'*) continue ;;
        esac
        target="$base/$path"
        if [ ! -f "$target" ]; then
            skipped=$((skipped + 1))
            if [ "${SOLC_REQUIRE_MIGRATION_SOURCES:-0}" = "1" ]; then
                echo "missing migration source: $target" >&2
                return 1
            fi
            continue
        fi
        actual=$(hash_file "$target")
        if [ "$actual" != "$expected" ]; then
            echo "migration compatibility mismatch: $path" >&2
            echo "expected $expected" >&2
            echo "actual   $actual" >&2
            return 1
        fi
        checked=$((checked + 1))
    done <"$manifest"
    echo "migration hashes match: checked=$checked skipped=$skipped"
}

check_manifest "$project_root/compat/migration-fixtures.sha256" "$project_root"
check_manifest "$project_root/tests/vectors/migrations/provenance.sha256" "$project_root"
