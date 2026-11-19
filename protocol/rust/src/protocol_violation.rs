//! Version 1 protocol-violation message definitions.

use crate::common::{
    make_id, validate_frame, CanFrame, IdParts, Status, Tier, MAX_DLC, MAX_STANDARD_ID,
};

/// Error-tier message index assigned to protocol violations.
pub const INDEX: u8 = 0;
/// Exact payload length for a protocol-violation frame.
pub const DLC: u8 = 4;

#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ProtocolViolationReason {
    UnassignedMessageIndex = 1,
    WrongDlc = 2,
    InvalidOrReservedFieldValue = 3,
    UnauthorizedSender = 4,
}

impl TryFrom<u8> for ProtocolViolationReason {
    type Error = Status;

    fn try_from(value: u8) -> Result<Self, Status> {
        match value {
            1 => Ok(Self::UnassignedMessageIndex),
            2 => Ok(Self::WrongDlc),
            3 => Ok(Self::InvalidOrReservedFieldValue),
            4 => Ok(Self::UnauthorizedSender),
            _ => Err(Status::BadValue),
        }
    }
}

impl From<ProtocolViolationReason> for u8 {
    fn from(value: ProtocolViolationReason) -> Self {
        value as Self
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ProtocolViolation {
    pub source_node: u8,
    pub reason: ProtocolViolationReason,
    pub offending_id: u16,
    pub observed_dlc: u8,
}

/// Encodes a protocol violation. An invalid source node takes precedence over
/// invalid payload values, as in the other message encoders.
pub fn encode(message: ProtocolViolation) -> Result<CanFrame, Status> {
    let id = make_id(IdParts {
        tier: Tier::Error,
        node: message.source_node,
        index: INDEX,
    })?;
    if message.offending_id > MAX_STANDARD_ID || message.observed_dlc > 15 {
        return Err(Status::BadValue);
    }

    let [id_low, id_high] = message.offending_id.to_le_bytes();
    let mut frame = CanFrame {
        id,
        dlc: DLC,
        data: [0; MAX_DLC as usize],
    };
    frame.data[..DLC as usize].copy_from_slice(&[
        message.reason.into(),
        id_low,
        id_high,
        message.observed_dlc,
    ]);
    Ok(frame)
}

/// Decodes only error-tier index 0 with its exact DLC and valid payload values.
/// Structural ID and DLC checks precede message-specific checks.
pub fn decode(frame: &CanFrame) -> Result<ProtocolViolation, Status> {
    let parts = validate_frame(frame)?;
    if parts.tier != Tier::Error {
        return Err(Status::BadTier);
    }
    if parts.index != INDEX {
        return Err(Status::BadIndex);
    }
    if frame.dlc != DLC {
        return Err(Status::BadDlc);
    }

    let reason = ProtocolViolationReason::try_from(frame.data[0])?;
    let offending_id = u16::from_le_bytes([frame.data[1], frame.data[2]]);
    if offending_id > MAX_STANDARD_ID || frame.data[3] > 15 {
        return Err(Status::BadValue);
    }
    Ok(ProtocolViolation {
        source_node: parts.node,
        reason,
        offending_id,
        observed_dlc: frame.data[3],
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    const GOLDEN: ProtocolViolation = ProtocolViolation {
        source_node: 2,
        reason: ProtocolViolationReason::WrongDlc,
        offending_id: 0x220,
        observed_dlc: 9,
    };

    fn golden_frame() -> CanFrame {
        CanFrame {
            id: 0x120,
            dlc: 4,
            data: [0x02, 0x20, 0x02, 0x09, 0, 0, 0, 0],
        }
    }

    #[test]
    fn golden_vector_crosses_codec() {
        assert_eq!(encode(GOLDEN), Ok(golden_frame()));
        assert_eq!(decode(&golden_frame()), Ok(GOLDEN));
    }

    #[test]
    fn valid_values_round_trip() {
        for source_node in 1..=6 {
            for reason in 1..=4 {
                for offending_id in [0, 0x220, MAX_STANDARD_ID] {
                    for observed_dlc in [0, 8, 9, 15] {
                        let message = ProtocolViolation {
                            source_node,
                            reason: ProtocolViolationReason::try_from(reason).unwrap(),
                            offending_id,
                            observed_dlc,
                        };
                        assert_eq!(decode(&encode(message).unwrap()), Ok(message));
                    }
                }
            }
        }
    }

    #[test]
    fn rejects_invalid_inputs_and_preserves_check_order() {
        let mut message = GOLDEN;
        message.source_node = 7;
        message.offending_id = 0x800;
        assert_eq!(encode(message), Err(Status::BadNode));
        message.source_node = 2;
        assert_eq!(encode(message), Err(Status::BadValue));
        message.offending_id = 0x220;
        message.observed_dlc = 16;
        assert_eq!(encode(message), Err(Status::BadValue));

        let mut frame = golden_frame();
        frame.id = 0x300;
        frame.dlc = 5;
        assert_eq!(decode(&frame), Err(Status::BadId));
        frame.id = 0x121;
        assert_eq!(decode(&frame), Err(Status::BadIndex));
        frame.id = 0x220;
        assert_eq!(decode(&frame), Err(Status::BadTier));
        frame.id = 0x120;
        assert_eq!(decode(&frame), Err(Status::BadDlc));
        frame.dlc = 4;
        frame.data[0] = 0;
        assert_eq!(decode(&frame), Err(Status::BadValue));
        frame.data[0] = 2;
        frame.data[2] = 8;
        assert_eq!(decode(&frame), Err(Status::BadValue));
        frame.data[2] = 2;
        frame.data[3] = 16;
        assert_eq!(decode(&frame), Err(Status::BadValue));
    }

    #[test]
    fn ignores_bytes_beyond_dlc() {
        let mut frame = golden_frame();
        frame.data[4..].fill(0xff);
        assert_eq!(decode(&frame), Ok(GOLDEN));
    }

    #[test]
    fn reason_wire_values_round_trip_and_reserved_values_fail() {
        let reasons = [
            ProtocolViolationReason::UnassignedMessageIndex,
            ProtocolViolationReason::WrongDlc,
            ProtocolViolationReason::InvalidOrReservedFieldValue,
            ProtocolViolationReason::UnauthorizedSender,
        ];

        for (offset, reason) in reasons.into_iter().enumerate() {
            let wire_value = offset as u8 + 1;
            assert_eq!(ProtocolViolationReason::try_from(wire_value), Ok(reason));
            assert_eq!(u8::from(reason), wire_value);
        }

        for wire_value in u8::MIN..=u8::MAX {
            if !(1..=4).contains(&wire_value) {
                assert_eq!(
                    ProtocolViolationReason::try_from(wire_value),
                    Err(Status::BadValue)
                );
            }
        }
    }

    #[test]
    fn wire_constants_match_version_one_catalog() {
        assert_eq!(INDEX, 0);
        assert_eq!(DLC, 4);
    }
}
