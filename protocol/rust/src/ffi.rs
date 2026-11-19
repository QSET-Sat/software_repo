//! Fixed-width C message types and message-specific ABI entry points.

use core::mem::{offset_of, size_of};

use crate::common::{make_id, CanFrame, IdParts, NodeState, Status, Tier};
use crate::{heartbeat, node_status, protocol_violation, request_status};

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CHeartbeat {
    pub source_node: u8,
    pub state: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CProtocolViolation {
    pub source_node: u8,
    pub reason: u8,
    pub offending_id: u16,
    pub observed_dlc: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CRequestStatus {
    pub destination_node: u8,
    pub sequence: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CNodeStatus {
    pub source_node: u8,
    pub sequence: u8,
    pub state: u8,
    pub uptime_s: u32,
    pub active_faults: u8,
}

// These match the C11 _Static_assert checks in test_c_api.c. A layout change
// fails compilation before either language can ship an incompatible ABI.
const _: () = {
    assert!(size_of::<CanFrame>() == 12);
    assert!(offset_of!(CanFrame, id) == 0);
    assert!(offset_of!(CanFrame, dlc) == 2);
    assert!(offset_of!(CanFrame, data) == 3);

    assert!(size_of::<CHeartbeat>() == 2);
    assert!(offset_of!(CHeartbeat, source_node) == 0);
    assert!(offset_of!(CHeartbeat, state) == 1);

    assert!(size_of::<CProtocolViolation>() == 6);
    assert!(offset_of!(CProtocolViolation, source_node) == 0);
    assert!(offset_of!(CProtocolViolation, reason) == 1);
    assert!(offset_of!(CProtocolViolation, offending_id) == 2);
    assert!(offset_of!(CProtocolViolation, observed_dlc) == 4);

    assert!(size_of::<CRequestStatus>() == 2);
    assert!(offset_of!(CRequestStatus, destination_node) == 0);
    assert!(offset_of!(CRequestStatus, sequence) == 1);

    assert!(size_of::<CNodeStatus>() == 12);
    assert!(offset_of!(CNodeStatus, source_node) == 0);
    assert!(offset_of!(CNodeStatus, sequence) == 1);
    assert!(offset_of!(CNodeStatus, state) == 2);
    assert!(offset_of!(CNodeStatus, uptime_s) == 4);
    assert!(offset_of!(CNodeStatus, active_faults) == 8);
};

fn check_node(tier: Tier, node: u8) -> Result<(), Status> {
    make_id(IdParts {
        tier,
        node,
        index: 0,
    })
    .map(|_| ())
}

// SAFETY: Callers must pass null or valid, correctly aligned pointers to the
// declared types. The result is calculated before the output is touched.
unsafe fn encode_message<T>(
    message: *const T,
    out_frame: *mut CanFrame,
    encode: impl FnOnce(&T) -> Result<CanFrame, Status>,
) -> i32 {
    if message.is_null() || out_frame.is_null() {
        return Status::NullPointer as i32;
    }
    match encode(unsafe { &*message }) {
        Ok(frame) => {
            unsafe { out_frame.write(frame) };
            Status::Ok as i32
        }
        Err(error) => error as i32,
    }
}

// SAFETY: The same pointer contract as encode_message applies. The decoded
// object is complete before writing to the C output buffer.
unsafe fn decode_message<T>(
    frame: *const CanFrame,
    out_message: *mut T,
    decode: impl FnOnce(&CanFrame) -> Result<T, Status>,
) -> i32 {
    if frame.is_null() || out_message.is_null() {
        return Status::NullPointer as i32;
    }
    match decode(unsafe { &*frame }) {
        Ok(message) => {
            unsafe { out_message.write(message) };
            Status::Ok as i32
        }
        Err(error) => error as i32,
    }
}

/// # Safety
/// Pointers must be null or valid, aligned objects of their declared types.
#[no_mangle]
pub unsafe extern "C" fn qset_can_encode_heartbeat(
    message: *const CHeartbeat,
    out_frame: *mut CanFrame,
) -> i32 {
    unsafe {
        encode_message(message, out_frame, |input| {
            check_node(Tier::Heartbeat, input.source_node)?;
            heartbeat::encode(heartbeat::Heartbeat {
                source_node: input.source_node,
                state: NodeState::try_from(input.state)?,
            })
        })
    }
}

/// # Safety
/// Pointers must be null or valid, aligned objects of their declared types.
#[no_mangle]
pub unsafe extern "C" fn qset_can_decode_heartbeat(
    frame: *const CanFrame,
    out_message: *mut CHeartbeat,
) -> i32 {
    unsafe {
        decode_message(frame, out_message, |input| {
            let message = heartbeat::decode(input)?;
            Ok(CHeartbeat {
                source_node: message.source_node,
                state: message.state.into(),
            })
        })
    }
}

/// # Safety
/// Pointers must be null or valid, aligned objects of their declared types.
#[no_mangle]
pub unsafe extern "C" fn qset_can_encode_protocol_violation(
    message: *const CProtocolViolation,
    out_frame: *mut CanFrame,
) -> i32 {
    unsafe {
        encode_message(message, out_frame, |input| {
            check_node(Tier::Error, input.source_node)?;
            protocol_violation::encode(protocol_violation::ProtocolViolation {
                source_node: input.source_node,
                reason: protocol_violation::ProtocolViolationReason::try_from(input.reason)?,
                offending_id: input.offending_id,
                observed_dlc: input.observed_dlc,
            })
        })
    }
}

/// # Safety
/// Pointers must be null or valid, aligned objects of their declared types.
#[no_mangle]
pub unsafe extern "C" fn qset_can_decode_protocol_violation(
    frame: *const CanFrame,
    out_message: *mut CProtocolViolation,
) -> i32 {
    unsafe {
        decode_message(frame, out_message, |input| {
            let message = protocol_violation::decode(input)?;
            Ok(CProtocolViolation {
                source_node: message.source_node,
                reason: message.reason.into(),
                offending_id: message.offending_id,
                observed_dlc: message.observed_dlc,
            })
        })
    }
}

/// # Safety
/// Pointers must be null or valid, aligned objects of their declared types.
#[no_mangle]
pub unsafe extern "C" fn qset_can_encode_request_status(
    message: *const CRequestStatus,
    out_frame: *mut CanFrame,
) -> i32 {
    unsafe {
        encode_message(message, out_frame, |input| {
            request_status::encode(request_status::RequestStatus {
                destination_node: input.destination_node,
                sequence: input.sequence,
            })
        })
    }
}

/// # Safety
/// Pointers must be null or valid, aligned objects of their declared types.
#[no_mangle]
pub unsafe extern "C" fn qset_can_decode_request_status(
    frame: *const CanFrame,
    out_message: *mut CRequestStatus,
) -> i32 {
    unsafe {
        decode_message(frame, out_message, |input| {
            let message = request_status::decode(input)?;
            Ok(CRequestStatus {
                destination_node: message.destination_node,
                sequence: message.sequence,
            })
        })
    }
}

/// # Safety
/// Pointers must be null or valid, aligned objects of their declared types.
#[no_mangle]
pub unsafe extern "C" fn qset_can_encode_node_status(
    message: *const CNodeStatus,
    out_frame: *mut CanFrame,
) -> i32 {
    unsafe {
        encode_message(message, out_frame, |input| {
            check_node(Tier::Telemetry, input.source_node)?;
            node_status::encode(node_status::NodeStatus {
                source_node: input.source_node,
                sequence: input.sequence,
                state: NodeState::try_from(input.state)?,
                uptime_s: input.uptime_s,
                active_faults: if input.active_faults == node_status::FAULT_COUNT_UNAVAILABLE {
                    None
                } else {
                    Some(input.active_faults)
                },
            })
        })
    }
}

/// # Safety
/// Pointers must be null or valid, aligned objects of their declared types.
#[no_mangle]
pub unsafe extern "C" fn qset_can_decode_node_status(
    frame: *const CanFrame,
    out_message: *mut CNodeStatus,
) -> i32 {
    unsafe {
        decode_message(frame, out_message, |input| {
            let message = node_status::decode(input)?;
            Ok(CNodeStatus {
                source_node: message.source_node,
                sequence: message.sequence,
                state: message.state.into(),
                uptime_s: message.uptime_s,
                active_faults: message
                    .active_faults
                    .unwrap_or(node_status::FAULT_COUNT_UNAVAILABLE),
            })
        })
    }
}
