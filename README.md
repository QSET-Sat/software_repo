# QSET software: CAN protocol foundation

This repository contains the first host-tested foundation for QSET's custom
Classic CAN application protocol. The [protocol specification](protocol/SPEC.md)
is the versioned source of truth for implemented wire-level rules. The older
`Intro_Project_README.md` describes a separate teaching exercise; its `0x321`
sensor message is not a QSET production message.

## What works today

- A `no_std` Rust protocol core constructs and parses the four draft ID tiers,
  checks the registered node table, and validates standard 11-bit data-frame
  IDs and DLCs.
- A small C API exposes those functions for future FreeRTOS firmware. No
  FreeRTOS kernel or board driver is needed for host testing.
- Rust unit tests and a linked C integration test run with `make test`.

Rust owns application-protocol decisions. C will own the FreeRTOS tasks,
queues, CAN driver, and ISR handoff when hardware is selected. See the
[FreeRTOS design](docs/FREERTOS_DESIGN.md) and [next steps](NEXT_STEPS.md).

## Build and test

Requirements: stable Rust/Cargo, a C11 compiler, and Make. Tested on macOS;
CI runs on macOS and Ubuntu.

```sh
make test
make clean
```

The host build links the Rust standard library for its panic runtime, but the
protocol code itself uses only `core` and does not allocate. Firmware builds
will disable Cargo's `host-std` feature and select the target toolchain and
panic/fault policy during board integration.

## Layout

- `protocol/SPEC.md`: authoritative implemented protocol rules and pending decisions
- `protocol/rust`: Rust core and unit tests
- `protocol/include/qset_can.h`: C ABI contract
- `protocol/tests/test_c_api.c`: linked C/Rust boundary test
- `docs/FREERTOS_DESIGN.md`: planned C transport/task architecture
- `NEXT_STEPS.md`: follow-on milestones and approval gates
