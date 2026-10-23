RUST_MANIFEST := protocol/rust/Cargo.toml
RUST_STATICLIB := protocol/rust/target/release/libqset_can_protocol.a
RUST_SOURCES := $(wildcard protocol/rust/src/*.rs)
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2

.PHONY: all test rust-test rust-check-no-std c-test clean

all: $(RUST_STATICLIB)

$(RUST_STATICLIB): $(RUST_SOURCES) protocol/rust/Cargo.toml
	cargo build --release --manifest-path $(RUST_MANIFEST)

build/test_c_api: protocol/tests/test_c_api.c protocol/include/qset_can.h $(RUST_STATICLIB)
	mkdir -p build
	$(CC) $(CFLAGS) -Iprotocol/include protocol/tests/test_c_api.c $(RUST_STATICLIB) -o $@

rust-test:
	cargo test --manifest-path $(RUST_MANIFEST)

rust-check-no-std:
	cargo check --manifest-path $(RUST_MANIFEST) --no-default-features

c-test: build/test_c_api
	./build/test_c_api

test: rust-test rust-check-no-std c-test

clean:
	cargo clean --manifest-path $(RUST_MANIFEST)
	rm -rf build
