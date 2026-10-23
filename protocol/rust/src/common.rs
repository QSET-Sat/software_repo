//! Types and structural validation shared by every protocol message.

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

#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum NodeState {
    Starting = 0,
    Nominal = 1,
    Degraded = 2,
    Safe = 3,
    Stopping = 4,
}

impl TryFrom<u8> for NodeState {
    type Error = Status;

    fn try_from(value: u8) -> Result<Self, Status> {
        match value {
            0 => Ok(Self::Starting),
            1 => Ok(Self::Nominal),
            2 => Ok(Self::Degraded),
            3 => Ok(Self::Safe),
            4 => Ok(Self::Stopping),
            _ => Err(Status::BadValue),
        }
    }
}

impl From<NodeState> for u8 {
    fn from(value: NodeState) -> Self {
        value as Self
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
    BadValue = -7,
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn node_state_wire_values_round_trip_and_reserved_values_fail() {
        let states = [
            NodeState::Starting,
            NodeState::Nominal,
            NodeState::Degraded,
            NodeState::Safe,
            NodeState::Stopping,
        ];

        for (wire_value, state) in states.into_iter().enumerate() {
            let wire_value = wire_value as u8;
            assert_eq!(NodeState::try_from(wire_value), Ok(state));
            assert_eq!(u8::from(state), wire_value);
        }

        for wire_value in 5..=u8::MAX {
            assert_eq!(NodeState::try_from(wire_value), Err(Status::BadValue));
        }
    }

    #[test]
    fn status_codes_preserve_existing_values_and_append_bad_value() {
        assert_eq!(Status::Ok as i32, 0);
        assert_eq!(Status::NullPointer as i32, -1);
        assert_eq!(Status::BadTier as i32, -2);
        assert_eq!(Status::BadNode as i32, -3);
        assert_eq!(Status::BadIndex as i32, -4);
        assert_eq!(Status::BadId as i32, -5);
        assert_eq!(Status::BadDlc as i32, -6);
        assert_eq!(Status::BadValue as i32, -7);
    }
}
