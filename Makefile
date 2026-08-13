.PHONY: all build test check rust fmt sbf-check runtime-check migration-check clean

BUILD_DIR ?= build

all: check

build:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) --parallel

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

rust:
	cargo test --workspace --all-targets

fmt:
	cargo fmt --all -- --check

sbf-check:
	scripts/check-sbf-toolchain.sh

runtime-check:
	scripts/check-runtime-differential.sh

migration-check:
	scripts/check-migration-fixtures.sh

check: test fmt rust migration-check
	cargo clippy --workspace --all-targets -- -D warnings
	cargo run --quiet -p solc-orchestrator --bin solc-wire -- check-vectors

clean:
	cmake --build $(BUILD_DIR) --target clean
	cargo clean
