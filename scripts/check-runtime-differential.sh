#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
artifact="$project_root/build/sbf/solc_reference.so"
budgets="$project_root/compat/runtime-budgets.toml"
runtime_manifest="$project_root/tests/runtime-oracle/Cargo.toml"
rpc_port=${SOLC_TEST_VALIDATOR_RPC_PORT:-18899}
faucet_port=${SOLC_TEST_VALIDATOR_FAUCET_PORT:-18900}
rpc_url="http://127.0.0.1:$rpc_port"
validator_pid=
temporary_root=

cleanup() {
    if [ -n "$validator_pid" ]; then
        kill "$validator_pid" 2>/dev/null || true
        wait "$validator_pid" 2>/dev/null || true
    fi
    if [ -n "$temporary_root" ]; then
        case "$temporary_root" in
            "${TMPDIR:-/tmp}"/solc-runtime.*) rm -rf -- "$temporary_root" ;;
            *) echo "refusing to remove unexpected temporary path: $temporary_root" >&2 ;;
        esac
    fi
}
trap cleanup EXIT HUP INT TERM

for command_name in cargo python3 solana solana-keygen solana-test-validator; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "$command_name is required" >&2
        exit 1
    fi
done

expected_agave=$(awk -F '"' '$1 ~ /^agave_release = / { print $2; exit }' "$budgets")
actual_cli=$(solana --version | awk '{print $2}')
actual_validator=$(solana-test-validator --version | awk '{print $2}')
if [ "$actual_cli" != "$expected_agave" ] || [ "$actual_validator" != "$expected_agave" ]; then
    echo "Agave version mismatch: expected $expected_agave, CLI=$actual_cli validator=$actual_validator" >&2
    exit 1
fi

"$project_root/scripts/check-sbf-toolchain.sh"
temporary_root=$(mktemp -d "${TMPDIR:-/tmp}/solc-runtime.XXXXXX")
solana-keygen new --silent --no-bip39-passphrase --force \
    --outfile "$temporary_root/payer.json"

program_id=$(
    cargo run --quiet --locked --manifest-path "$runtime_manifest" -- program-id
)
cargo run --quiet --locked --manifest-path "$runtime_manifest" -- \
    mollusk "$artifact" >"$temporary_root/mollusk.json"

solana-test-validator \
    --reset \
    --quiet \
    --ledger "$temporary_root/ledger" \
    --rpc-port "$rpc_port" \
    --faucet-port "$faucet_port" \
    --dynamic-port-range 19000-19100 \
    --bpf-program "$program_id" "$artifact" \
    >"$temporary_root/validator.log" 2>&1 &
validator_pid=$!

attempt=0
while ! solana cluster-version --url "$rpc_url" >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 120 ]; then
        echo "local validator did not become ready" >&2
        sed -n '1,240p' "$temporary_root/validator.log" >&2
        exit 1
    fi
    sleep 0.25
done

payer_pubkey=$(solana-keygen pubkey "$temporary_root/payer.json")
solana airdrop 2 "$payer_pubkey" --url "$rpc_url" \
    --commitment finalized >/dev/null
solana balance "$payer_pubkey" --url "$rpc_url" \
    --commitment finalized >/dev/null
genesis_hash=$(solana genesis-hash --url "$rpc_url")
cargo run --quiet --locked --manifest-path "$runtime_manifest" -- \
    agave "$rpc_url" "$temporary_root/payer.json" "$program_id" \
    "$genesis_hash" "$artifact" >"$temporary_root/agave.json"

python3 "$project_root/scripts/compare-runtime-evidence.py" \
    --budgets "$budgets" \
    --mollusk "$temporary_root/mollusk.json" \
    --agave "$temporary_root/agave.json"
cat "$temporary_root/mollusk.json"
cat "$temporary_root/agave.json"
