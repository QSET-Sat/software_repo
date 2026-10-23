#![no_std]

// The protocol itself uses only core. Host builds link std for a panic runtime;
// firmware builds disable the default host-std feature.
#[cfg(feature = "host-std")]
extern crate std;
#[cfg(all(not(test), not(feature = "host-std")))]
use core::panic::PanicInfo;

pub mod common;
pub mod heartbeat;
pub mod node_status;
pub mod protocol_violation;
pub mod request_status;

// Keep the original crate-root Rust API source-compatible.
pub use common::{
    make_id, parse_id, validate_frame, CanFrame, IdParts, Node, Status, Tier, MAX_DLC,
    MAX_STANDARD_ID, REGISTERED_NODES,
};

/// The output is written only on success. Non-null pointers must reference
/// valid, correctly aligned storage of the declared type.
///
/// # Safety
///
/// `out_id` must be null or point to writable, correctly aligned `u16` storage.
#[no_mangle]
pub unsafe extern "C" fn qset_can_make_id(tier: u8, node: u8, index: u8, out_id: *mut u16) -> i32 {
    if out_id.is_null() {
        return Status::NullPointer as i32;
    }
    let tier = match Tier::try_from(tier) {
        Ok(tier) => tier,
        Err(error) => return error as i32,
    };
    match make_id(IdParts { tier, node, index }) {
        Ok(id) => {
            // SAFETY: The caller supplies valid writable storage, checked non-null above.
            unsafe { out_id.write(id) };
            Status::Ok as i32
        }
        Err(error) => error as i32,
    }
}

/// Outputs are written only on success. Non-null pointers must reference
/// valid, correctly aligned storage of the declared type.
///
/// # Safety
///
/// Each output pointer must be null or point to writable, correctly aligned
/// `u8` storage.
#[no_mangle]
pub unsafe extern "C" fn qset_can_parse_id(
    id: u16,
    out_tier: *mut u8,
    out_node: *mut u8,
    out_index: *mut u8,
) -> i32 {
    if out_tier.is_null() || out_node.is_null() || out_index.is_null() {
        return Status::NullPointer as i32;
    }
    match parse_id(id) {
        Ok(parts) => {
            // SAFETY: The caller supplies valid writable storage, checked non-null above.
            unsafe {
                out_tier.write(parts.tier as u8);
                out_node.write(parts.node);
                out_index.write(parts.index);
            }
            Status::Ok as i32
        }
        Err(error) => error as i32,
    }
}

/// The pointer must reference a valid, correctly aligned CanFrame.
///
/// # Safety
///
/// `frame` must be null or point to a valid, correctly aligned [`CanFrame`].
#[no_mangle]
pub unsafe extern "C" fn qset_can_validate_frame(frame: *const CanFrame) -> i32 {
    if frame.is_null() {
        return Status::NullPointer as i32;
    }
    // SAFETY: The caller supplies a valid CanFrame, checked non-null above.
    match validate_frame(unsafe { &*frame }) {
        Ok(_) => Status::Ok as i32,
        Err(error) => error as i32,
    }
}

// Host builds link std's panic runtime. Firmware builds use this fallback.
// Replace it here with a board-specific handler, or remove it before linking
// another Rust panic handler, once the target fault policy is defined.
#[cfg(all(not(test), not(feature = "host-std")))]
#[panic_handler]
fn panic(_: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use core::mem::{offset_of, size_of};

    #[test]
    fn c_frame_layout_matches_header() {
        assert_eq!(size_of::<CanFrame>(), 12);
        assert_eq!(offset_of!(CanFrame, id), 0);
        assert_eq!(offset_of!(CanFrame, dlc), 2);
        assert_eq!(offset_of!(CanFrame, data), 3);
    }

    #[test]
    fn registry_ids_are_unique_and_within_four_bits() {
        for (position, node) in REGISTERED_NODES.iter().enumerate() {
            assert!((1..=15).contains(&node.id));
            assert!(REGISTERED_NODES[..position]
                .iter()
                .all(|other| other.id != node.id));
        }
    }

    #[test]
    fn every_registered_id_round_trips() {
        for node in REGISTERED_NODES {
            for tier in [Tier::Heartbeat, Tier::Error, Tier::Command, Tier::Telemetry] {
                let max_index = if tier == Tier::Heartbeat { 0 } else { 15 };
                for index in 0..=max_index {
                    let parts = IdParts {
                        tier,
                        node: node.id,
                        index,
                    };
                    assert_eq!(parse_id(make_id(parts).unwrap()), Ok(parts));
                }
            }
        }
        assert_eq!(
            make_id(IdParts {
                tier: Tier::Telemetry,
                node: 3,
                index: 2
            }),
            Ok(0x432)
        );
    }

    #[test]
    fn rejects_unassigned_and_reserved_ids() {
        for id in [
            0x000, 0x010, 0x0ff, 0x100, 0x170, 0x270, 0x300, 0x3ff, 0x470, 0x500, 0x7ff, 0x800,
        ] {
            assert!(parse_id(id).is_err(), "accepted reserved ID {id:#x}");
        }
        assert_eq!(
            make_id(IdParts {
                tier: Tier::Heartbeat,
                node: 1,
                index: 20
            }),
            Err(Status::BadIndex)
        );
        assert_eq!(
            make_id(IdParts {
                tier: Tier::Error,
                node: 0,
                index: 0
            }),
            Err(Status::BadNode)
        );
        assert_eq!(
            make_id(IdParts {
                tier: Tier::Command,
                node: 1,
                index: 16
            }),
            Err(Status::BadIndex)
        );
    }

    #[test]
    fn frame_validation_checks_structure_not_payload_semantics() {
        let mut frame = CanFrame {
            id: 0x432,
            dlc: 0,
            data: [0xff; 8],
        };
        assert_eq!(validate_frame(&frame).unwrap().tier, Tier::Telemetry);
        frame.dlc = 8;
        assert!(validate_frame(&frame).is_ok());
        frame.dlc = 9;
        assert_eq!(validate_frame(&frame), Err(Status::BadDlc));
        frame.id = 0x300;
        assert_eq!(validate_frame(&frame), Err(Status::BadId));
    }
}
