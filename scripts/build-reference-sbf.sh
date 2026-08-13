#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output_path=${1:-"$project_root/build/sbf/solc_reference.so"}
sbf_sdk_path=${SBF_SDK_PATH:-}
if [ -z "$sbf_sdk_path" ]; then
    solana_binary=$(command -v solana || true)
    if [ -z "$solana_binary" ]; then
        echo "solana CLI not found; set SBF_SDK_PATH" >&2
        exit 1
    fi
    sbf_sdk_path=$(dirname "$solana_binary")/platform-tools-sdk/sbf
fi

platform_tools_path=${SOLC_PLATFORM_TOOLS_PATH:-"$sbf_sdk_path/dependencies/platform-tools"}
if [ ! -x "$platform_tools_path/llvm/bin/clang" ]; then
    cache_base=${XDG_CACHE_HOME:-"${HOME}/.cache"}
    cached_tools="$cache_base/solana/v1.48/platform-tools"
    if [ -x "$cached_tools/llvm/bin/clang" ]; then
        platform_tools_path=$cached_tools
    else
        echo "platform-tools v1.48 not found; set SOLC_PLATFORM_TOOLS_PATH" >&2
        exit 1
    fi
fi

clang="$platform_tools_path/llvm/bin/clang"
linker="$platform_tools_path/llvm/bin/ld.lld"
objcopy="$platform_tools_path/llvm/bin/llvm-objcopy"
clang_include="$platform_tools_path/llvm/lib/clang/19/include"
target_include="$platform_tools_path/llvm/sbpfv2/include"
target_lib="$platform_tools_path/llvm/lib/sbpfv2"
compiler_builtins_dir="$platform_tools_path/rust/lib/rustlib/sbpfv2-solana-solana/lib"
set -- "$compiler_builtins_dir"/libcompiler_builtins-*.rlib
if [ "$#" -ne 1 ] || [ ! -f "$1" ]; then
    echo "expected exactly one sbpfv2 compiler-builtins archive" >&2
    exit 1
fi
compiler_builtins=$1

output_dir=$(dirname "$output_path")
mkdir -p "$output_dir"
sbf_object="$output_dir/solc_sbf.o"
entry_object="$output_dir/reference.o"

compile_source() {
    source_path=$1
    object_path=$2
    "$clang" \
        -Werror -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -O2 -fno-builtin -std=c17 \
        -ffile-prefix-map="$project_root"=. \
        -fdebug-prefix-map="$project_root"=. \
        -isystem "$sbf_sdk_path/c/inc" \
        -isystem "$clang_include" \
        -I "$project_root/include" \
        -isystem "$target_include" \
        -target sbf -mcpu=v2 -fPIC \
        -c "$source_path" -o "$object_path"
}

compile_source "$project_root/src/sbf.c" "$sbf_object"
compile_source "$project_root/programs/reference-entrypoint/reference.c" "$entry_object"

"$linker" \
    -z notext -shared --Bdynamic "$sbf_sdk_path/c/sbf.ld" \
    --entry entrypoint -L "$target_lib" -z max-page-size=4096 -lc \
    -o "$output_path" "$entry_object" "$sbf_object" "$compiler_builtins"
"$objcopy" --strip-all "$output_path"

if command -v sha256sum >/dev/null 2>&1; then
    artifact_sha256=$(sha256sum "$output_path" | awk '{print $1}')
else
    artifact_sha256=$(shasum -a 256 "$output_path" | awk '{print $1}')
fi
artifact_bytes=$(wc -c < "$output_path" | tr -d ' ')
echo "artifact=$output_path"
echo "bytes=$artifact_bytes"
echo "sha256=$artifact_sha256"
echo "arch=v2 platform_tools=v1.48"
