//! Version 1 protocol-violation message definitions.

use crate::common::Status;

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

#[cfg(test)]
mod tests {
    use super::*;

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
