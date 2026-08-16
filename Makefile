.PHONY: all build test check check-fast check-full rust fmt policy site-check determinism \
	sanitize thread-sanitize coverage analyze fuzz-smoke sbf-check runtime-check \
	migration-check clean

BUILD_DIR ?= build

all: check

build:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) --parallel

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

rust:
	cargo test --locked --workspace --all-targets

fmt:
	cargo fmt --all -- --check

policy: build
	python3 scripts/check-kernel-policy.py --policy ci/kernel-policy.toml --clang "$${CC:-clang}"
	python3 scripts/check-kernel-policy.py --policy ci/kernel-policy.toml --library "$(BUILD_DIR)/libsolc_wire.a"
	python3 scripts/check-kernel-policy.py --self-test

site-check:
	python3 scripts/check-site.py site

determinism:
	python3 scripts/check-determinism.py

sanitize:
	cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DSOLC_ENABLE_SANITIZERS=ON
	cmake --build build-sanitize --parallel
	ctest --test-dir build-sanitize --output-on-failure

thread-sanitize:
	cmake -S . -B build-thread -DCMAKE_BUILD_TYPE=Debug -DSOLC_ENABLE_THREAD_SANITIZER=ON
	cmake --build build-thread --parallel
	ctest --test-dir build-thread --output-on-failure

coverage:
	python3 scripts/check-coverage.py --minimum-lines 77

analyze:
	cmake -S . -B build-analyze -DCMAKE_BUILD_TYPE=Debug -DSOLC_ENABLE_GCC_ANALYZER=ON
	cmake --build build-analyze --parallel

fuzz-smoke:
	scripts/run-fuzz.sh 10 build-fuzz

sbf-check:
	scripts/check-sbf-toolchain.sh

runtime-check:
	scripts/check-runtime-differential.sh

migration-check:
	scripts/check-migration-fixtures.sh

check-fast: policy site-check test fmt rust migration-check
	cargo clippy --locked --workspace --all-targets -- -D warnings
	cargo run --locked --quiet -p solc-orchestrator --bin solc-wire -- check-vectors

check: check-fast

check-full: check-fast determinism sanitize thread-sanitize coverage fuzz-smoke

clean:
	cmake --build $(BUILD_DIR) --target clean
	cargo clean
