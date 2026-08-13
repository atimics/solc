#!/usr/bin/env sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
artifact="$project_root/build/sbf/solc_reference.so"
runtime_manifest="$project_root/tests/runtime-oracle/Cargo.toml"
budgets="$project_root/compat/runtime-budgets.toml"
rpc_url=${SOLC_DEVNET_RPC_URL:-https://api.devnet.solana.com}
expected_genesis=EtWTRABZaYq6iMfeYKouRu166VU2xqa1wcaWoxPkrZBG
evidence_path=${SOLC_DEVNET_EVIDENCE:-$project_root/build/devnet-evidence.json}

if [ "${SOLC_DEVNET_ENABLE:-}" != "1" ]; then
    echo "refusing devnet writes: set SOLC_DEVNET_ENABLE=1 explicitly" >&2
    exit 1
fi
if [ -z "${SOLC_DEVNET_PAYER:-}" ] || [ -z "${SOLC_DEVNET_PROGRAM_KEYPAIR:-}" ]; then
    echo "SOLC_DEVNET_PAYER and SOLC_DEVNET_PROGRAM_KEYPAIR are required" >&2
    exit 1
fi
for secret_file in "$SOLC_DEVNET_PAYER" "$SOLC_DEVNET_PROGRAM_KEYPAIR"; do
    if [ ! -f "$secret_file" ]; then
        echo "keypair file not found: $secret_file" >&2
        exit 1
    fi
done

expected_agave=$(awk -F '"' '$1 ~ /^agave_release = / { print $2; exit }' "$budgets")
actual_agave=$(solana --version | awk '{print $2}')
if [ "$actual_agave" != "$expected_agave" ]; then
    echo "Agave CLI version mismatch: expected $expected_agave, got $actual_agave" >&2
    exit 1
fi
actual_genesis=$(solana genesis-hash --url "$rpc_url")
if [ "$actual_genesis" != "$expected_genesis" ]; then
    echo "devnet genesis mismatch: expected $expected_genesis, got $actual_genesis" >&2
    exit 1
fi

"$project_root/scripts/check-sbf-toolchain.sh"
program_id=$(solana-keygen pubkey "$SOLC_DEVNET_PROGRAM_KEYPAIR")
solana balance "$(solana-keygen pubkey "$SOLC_DEVNET_PAYER")" \
    --url "$rpc_url" --commitment confirmed >/dev/null

deploy_result=$(solana program deploy "$artifact" \
    --url "$rpc_url" \
    --keypair "$SOLC_DEVNET_PAYER" \
    --fee-payer "$SOLC_DEVNET_PAYER" \
    --upgrade-authority "$SOLC_DEVNET_PAYER" \
    --program-id "$SOLC_DEVNET_PROGRAM_KEYPAIR" \
    --use-rpc \
    --commitment confirmed \
    --output json-compact)
runtime_result=$(cargo run --quiet --locked --manifest-path "$runtime_manifest" -- \
    agave "$rpc_url" "$SOLC_DEVNET_PAYER" "$program_id" \
    "$expected_genesis" "$artifact")

mkdir -p "$(dirname -- "$evidence_path")"
python3 - "$evidence_path" "$deploy_result" "$runtime_result" <<'PY'
import json
import pathlib
import sys

destination = pathlib.Path(sys.argv[1])
document = {
    "schema": "solc-devnet-evidence-v1",
    "deployment": json.loads(sys.argv[2]),
    "execution": json.loads(sys.argv[3]),
}
destination.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
PY
echo "devnet deployment and execution evidence written to $evidence_path"
