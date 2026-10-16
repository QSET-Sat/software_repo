#![no_std]

// The protocol itself uses only core. Host builds link std for a panic runtime;
// firmware builds disable the default host-std feature.
#[cfg(feature = "host-std")]
extern crate std;
#[cfg(all(not(test), not(feature = "host-std")))]
use core::panic::PanicInfo;

pub const MAX_DLC: u8 = 8;
pub const MAX_STANDARD_ID: u16 = 0x7ff;

/// Edit this registry when a node is assigned. Every active node must have a
/// distinct ID in 1..=15. ID zero remains reserved.
pub const REGISTERED_NODES: &[Node] = &[
    Node {
        id: 0x1,
        name: "OBC",
    },
    Node {
        id: 0x2,
        name: "ADCS",
    },
    Node {
        id: 0x3,
        name: "COMMS",
    },
    Node {
        id: 0x4,
        name: "PAYLOAD",
    },
    Node {
        id: 0x5,
        name: "BATT",
    },
    Node {
        id: 0x6,
        name: "MPPT",
    },
];

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Node {
    pub id: u8,
    pub name: &'static str,
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Tier {
    Heartbeat = 0,
    Error = 1,
    Command = 2,
    Telemetry = 3,
}

impl TryFrom<u8> for Tier {
    type Error = Status;

    fn try_from(value: u8) -> Result<Self, Status> {
        match value {
            0 => Ok(Self::Heartbeat),
            1 => Ok(Self::Error),
            2 => Ok(Self::Command),
            3 => Ok(Self::Telemetry),
            _ => Err(Status::BadTier),
        }
    }
}

#[repr(i32)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Status {
    Ok = 0,
    NullPointer = -1,
    BadTier = -2,
    BadNode = -3,
    BadIndex = -4,
    BadId = -5,
    BadDlc = -6,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CanFrame {
    pub id: u16,
    pub dlc: u8,
    pub data: [u8; 8],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct IdParts {
    pub tier: Tier,
    /// Source for heartbeat/error/telemetry; destination for commands.
    pub node: u8,
    /// Zero for heartbeat; 0..=15 for all other tiers.
    pub index: u8,
}

fn registered(node: u8) -> bool {
    REGISTERED_NODES.iter().any(|entry| entry.id == node)
}

pub fn make_id(parts: IdParts) -> Result<u16, Status> {
    if !registered(parts.node) {
        return Err(Status::BadNode);
    }
    if parts.index > 0x0f || (parts.tier == Tier::Heartbeat && parts.index != 0) {
        return Err(Status::BadIndex);
    }

    let id = match parts.tier {
        Tier::Heartbeat => u16::from(parts.node),
        Tier::Error => 0x100 + u16::from(parts.node) * 0x10 + u16::from(parts.index),
        Tier::Command => 0x200 + u16::from(parts.node) * 0x10 + u16::from(parts.index),
        Tier::Telemetry => 0x400 + u16::from(parts.node) * 0x10 + u16::from(parts.index),
    };
    Ok(id)
}

pub fn parse_id(id: u16) -> Result<IdParts, Status> {
    if id > MAX_STANDARD_ID {
        return Err(Status::BadId);
    }

    let (tier, node, index) = match id {
        0x000..=0x00f => (Tier::Heartbeat, id as u8, 0),
        0x100..=0x1ff => (Tier::Error, ((id - 0x100) >> 4) as u8, (id & 0x0f) as u8),
        0x200..=0x2ff => (Tier::Command, ((id - 0x200) >> 4) as u8, (id & 0x0f) as u8),
        0x400..=0x4ff => (
            Tier::Telemetry,
            ((id - 0x400) >> 4) as u8,
            (id & 0x0f) as u8,
        ),
        _ => return Err(Status::BadId),
    };
    if !registered(node) {
        return Err(Status::BadNode);
    }
    Ok(IdParts { tier, node, index })
}

pub fn validate_frame(frame: &CanFrame) -> Result<IdParts, Status> {
    let parts = parse_id(frame.id)?;
    if frame.dlc > MAX_DLC {
        return Err(Status::BadDlc);
    }
    Ok(parts)
}

/// The output is written only on success. Non-null pointers must reference
/// valid, correctly aligned storage of the declared type.
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

// The host static library has no Rust standard library. An embedded image may
// replace this handler when a target toolchain and system fault policy exist.
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
            0x000, 0x010, 0x0ff, 0x100, 0x160, 0x260, 0x300, 0x3ff, 0x460, 0x500, 0x7ff, 0x800,
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
