#!/bin/sh
set -eu

seconds=${1:-10}
build_dir=${2:-build-fuzz}

case "$seconds" in
    *[!0-9]*|'')
        printf '%s\n' "fuzz duration must be a positive integer" >&2
        exit 2
        ;;
esac
if [ "$seconds" -eq 0 ]; then
    printf '%s\n' "fuzz duration must be greater than zero" >&2
    exit 2
fi

if [ -z "${CC+x}" ] && [ "$(uname -s)" = "Darwin" ]; then
    if [ -x /opt/homebrew/opt/llvm/bin/clang ]; then
        CC=/opt/homebrew/opt/llvm/bin/clang
        export CC
    elif [ -x /usr/local/opt/llvm/bin/clang ]; then
        CC=/usr/local/opt/llvm/bin/clang
        export CC
    fi
fi

cmake -S . -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DSOLC_BUILD_FUZZER=ON \
    -DBUILD_TESTING=OFF
cmake --build "$build_dir" --parallel

scripts/materialize-fuzz-corpus.sh "$build_dir/corpus/wire"
scripts/materialize-sbf-fuzz-corpus.sh "$build_dir/corpus/sbf"
scripts/materialize-program-fuzz-corpus.sh "$build_dir/corpus/programs"
mkdir -p "$build_dir/corpus/encoding"
for seed in tests/fuzz/corpus/encoding/*; do
    cp "$seed" "$build_dir/corpus/encoding/"
done

"$build_dir/solc_wire_fuzz" "$build_dir/corpus/wire" \
    -max_total_time="$seconds" -timeout=2 -verbosity=0 -print_final_stats=1
"$build_dir/solc_encoding_fuzz" "$build_dir/corpus/encoding" \
    -max_total_time="$seconds" -timeout=2 -verbosity=0 -print_final_stats=1
"$build_dir/solc_sbf_fuzz" "$build_dir/corpus/sbf" \
    -max_total_time="$seconds" -timeout=2 -verbosity=0 -print_final_stats=1
"$build_dir/solc_program_fuzz" "$build_dir/corpus/programs" \
    -max_total_time="$seconds" -timeout=2 -verbosity=0 -print_final_stats=1
