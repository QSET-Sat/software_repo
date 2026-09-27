# CAN Onboarding Lab: C and Rust Frame Codec

## Goal

In this lab, you will implement a small host-side CAN telemetry codec. You will work across a C/Rust boundary, encode a message into an eight-byte Classic CAN payload, decode it again, and prove the implementation with tests.

This is deliberately **generic**. It teaches the techniques that apply to spacecraft software without defining QSET's still-evolving CAN identifiers or priority scheme.

**Estimated time:** 1–2 hours  
**Scope:** Host-side only; no CAN hardware, RTOS, or QSET flight protocol knowledge is required.

## What you are building

`src/can_frame.c` is the C-facing layer. It owns a generic CAN-frame container and is the API used by the C test program. `rust_codec/src/lib.rs` is compiled as a `no_std` Rust static library. It owns payload byte layout and validation.

The boundary is intentional:

- **C:** Frame metadata (`id`, `dlc`), the caller-facing API, and calling the Rust functions.
- **Rust:** Converting structured telemetry to/from bytes and validating payload content.

The code is a small host-side analogue of the architecture where C owns hardware/RTOS-facing work and Rust owns raw-frame protocol logic.

## Wire format

This lab's one generic sensor-telemetry message has CAN ID `0x321` and DLC `8`.

| Byte | Field                       | Encoding / valid value                                                   |
| ---- | --------------------------- | ------------------------------------------------------------------------ |
| 0    | `sensor_id`                 | Unsigned 8-bit integer                                                   |
| 1    | `status`                    | Unsigned 8-bit integer                                                   |
| 2–3  | `temperature_centi_degrees` | Signed 16-bit integer, **big-endian** (for example, `-42` °C is `-4200`) |
| 4–5  | `voltage_mv`                | Unsigned 16-bit integer, **big-endian**                                  |
| 6–7  | reserved                    | Both bytes must be zero                                                  |

Big-endian means the most-significant byte comes first. For example, `3300` is `0x0C E4`.

## Getting started

From this folder, run:

```sh
make test
```

The starter project deliberately compiles but does **not** pass the whole test suite. Read the code, trace the C/Rust call flow, and implement the marked `TODO`s. Do not treat the initial failures as a list of hints: use the wire-format table and function contracts to reason about correct behaviour.

Useful commands:

```sh
make test       # build and run all C tests
make clean      # remove generated build artifacts
```

## Requirements

Implement the following behavior:

1. `telemetry_to_can_frame` creates a frame with ID `0x321`, DLC `8`, and a correctly encoded payload.
2. `can_frame_to_telemetry` rejects frames whose ID is not `0x321` or whose DLC is not `8`.
3. Rust encoding writes all payload bytes in the table above, including zero reserved bytes.
4. Rust decoding restores the original values, including negative temperatures.
5. Rust decoding rejects a payload if either reserved byte is non-zero.
6. Null pointers at the C/Rust API boundary return an error rather than being dereferenced.
7. Add **at least two tests of your own** in the marked section of `tests/test_codec.c`:
   - one must test rejection of a frame or payload that is invalid; and
   - one must test a boundary or representative round trip (examples: the minimum/maximum signed temperature, or `0`/maximum voltage).

Do not change the public function signatures or relax the required validations merely to make tests pass.

## Suggested workflow

1. Read `include/can_frame.h` and `rust_codec/src/lib.rs` before editing. Sketch where each field ends up in the eight-byte payload.
2. Start with Rust encode/decode logic. Cast only after deciding how the bytes should be arranged; avoid assuming the host machine's endianness.
3. Implement C's CAN ID/DLC validation and delegation to Rust.
4. Run `make test` frequently.
5. Add your two tests and ensure they fail when you deliberately break the behavior they cover.
6. Complete `CAN_LAB_REFLECTION.md` in your own words.

## Definition of done

You are finished when:

- `make test` exits successfully;
- all starter and your two added tests pass;
- you can explain why byte order matters, why `i16` needs different decode handling from `u16`, and which side of the C/Rust boundary owns which responsibility; and
- `CAN_LAB_REFLECTION.md` is complete.

## Using AI

AI is allowed. Use it to explain an error, review a small piece of code, discuss FFI concepts, or help you design a test. Treat its output as a suggestion: compile it, test it, and understand it before keeping it.

Be prepared to explain every changed line, the test cases you wrote, and why your validation belongs where it does. Do not submit code you cannot walk through yourself.
