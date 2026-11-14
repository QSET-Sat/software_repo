// heartbeat message for version 1: struct, encode and decode
// ID = source node, DLC = 2, byte 0 = protocol version, byte 1 = node state
// only uses core, no allocation, no panics, bad input returns a Status error

use crate::common::{make_id, validate_frame, CanFrame, IdParts, NodeState, Status, Tier, MAX_DLC};

// protocol for version 1 heartbeat
pub const PROTOCOL_VERSION: u8 = 1;
// heartbeat payload length
pub const DLC: u8 = 2;

// heartbeat position fields in frame
const VERSION_BYTE: usize = 0;
const STATE_BYTE: usize = 1;

// heartbeat struct, no version field since version 1 is the only valid one
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Heartbeat {
    // node that sent the heartbeat, also the CAN ID
    pub source_node: u8,
    // current state of the node
    pub state: NodeState,
}

// CAN frame for heartbeat
// returns BadNode if the source node isn't registered
pub fn encode(heartbeat: Heartbeat) -> Result<CanFrame, Status> {
    // make the ID with make_id so the node gets checked, index is always 0
    let id = make_id(IdParts {
        tier: Tier::Heartbeat,
        node: heartbeat.source_node,
        index: 0,
    })?;

    // start with all zeros so bytes 2 to 7 are always sent as 0
    let mut frame = CanFrame {
        id,
        dlc: DLC,
        data: [0; MAX_DLC as usize],
    };

    // fill in version and state
    frame.data[VERSION_BYTE] = PROTOCOL_VERSION;
    frame.data[STATE_BYTE] = u8::from(heartbeat.state);

    Ok(frame)
}

// heartbeat from a received CAN frame
// checks run in order and the first one that fails gets returned
// BadId / BadNode: bad ID or unregistered node
// BadTier: frame is from another tier, so it's not a heartbeat
// BadDlc: DLC isn't 2
// BadValue: version isn't 0x01 or state isn't 0x00 to 0x04
// frame is only borrowed and the result is returned, so a failed decode can't change anything
pub fn decode(frame: &CanFrame) -> Result<Heartbeat, Status> {
    // shared checks: ID range, registered node, DLC 8 or less
    let parts = validate_frame(frame)?;

    // has to be a heartbeat ID (0x000 to 0x00F)
    if parts.tier != Tier::Heartbeat {
        return Err(Status::BadTier);
    }

    // heartbeat DLC has to be exactly 2
    if frame.dlc != DLC {
        return Err(Status::BadDlc);
    }

    // only version 1 is valid
    if frame.data[VERSION_BYTE] != PROTOCOL_VERSION {
        return Err(Status::BadValue);
    }

    // state has to be 0x00 to 0x04, anything else is BadValue
    let state = NodeState::try_from(frame.data[STATE_BYTE])?;

    // all checks passed
    Ok(Heartbeat {
        source_node: parts.node,
        state,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::REGISTERED_NODES;

    // all 5 valid node states
    const ALL_STATES: [NodeState; 5] = [
        NodeState::Starting,
        NodeState::Nominal,
        NodeState::Degraded,
        NodeState::Safe,
        NodeState::Stopping,
    ];

    // golden vector heartbeat from SPEC.md
    const ADCS_NOMINAL: Heartbeat = Heartbeat {
        source_node: 0x2,
        state: NodeState::Nominal,
    };

    // test frame with version and state, rest zeros
    fn frame(id: u16, dlc: u8, version: u8, state: u8) -> CanFrame {
        CanFrame {
            id,
            dlc,
            data: [version, state, 0, 0, 0, 0, 0, 0],
        }
    }

    // checks if a node is in the registry
    fn is_registered(node: u8) -> bool {
        REGISTERED_NODES.iter().any(|entry| entry.id == node)
    }

    // constants match the version 1 catalog
    #[test]
    fn wire_constants_match_version_one_catalog() {
        assert_eq!(PROTOCOL_VERSION, 1);
        assert_eq!(DLC, 2);
    }

    // golden vector: ADCS nominal = ID 0x002, DLC 2, data 01 01, rest zeros
    #[test]
    fn adcs_golden_vector_encodes_exactly() {
        assert_eq!(
            encode(ADCS_NOMINAL),
            Ok(CanFrame {
                id: 0x002,
                dlc: 2,
                data: [0x01, 0x01, 0, 0, 0, 0, 0, 0],
            })
        );
    }

    // golden vector decodes back to ADCS nominal
    #[test]
    fn adcs_golden_vector_decodes() {
        assert_eq!(decode(&frame(0x002, 2, 0x01, 0x01)), Ok(ADCS_NOMINAL));
    }

    // every node and state encodes and decodes back the same
    #[test]
    fn every_node_and_state_round_trips() {
        for node in REGISTERED_NODES {
            for state in ALL_STATES {
                let heartbeat = Heartbeat {
                    source_node: node.id,
                    state,
                };
                let encoded = encode(heartbeat).unwrap();
                assert_eq!(encoded.id, u16::from(node.id));
                assert_eq!(encoded.dlc, DLC);
                assert_eq!(encoded.data[VERSION_BYTE], PROTOCOL_VERSION);
                assert_eq!(encoded.data[STATE_BYTE], u8::from(state));
                assert_eq!(encoded.data[2..], [0; 6]);
                assert_eq!(decode(&encoded), Ok(heartbeat));
            }
        }
    }

    // unregistered nodes can't be encoded, including node 0
    #[test]
    fn encode_rejects_unregistered_nodes() {
        for node in u8::MIN..=u8::MAX {
            if !is_registered(node) {
                let heartbeat = Heartbeat {
                    source_node: node,
                    state: NodeState::Nominal,
                };
                assert_eq!(encode(heartbeat), Err(Status::BadNode), "node {node:#x}");
            }
        }
    }

    // heartbeat IDs with unregistered nodes (0x000, 0x007 to 0x00F) get rejected
    #[test]
    fn decode_rejects_unregistered_nodes() {
        for id in 0x000..=0x00f {
            if !is_registered(id as u8) {
                assert_eq!(
                    decode(&frame(id, 2, 0x01, 0x01)),
                    Err(Status::BadNode),
                    "id {id:#x}"
                );
            }
        }
    }

    // IDs outside every tier or over 11 bits get rejected
    #[test]
    fn decode_rejects_ids_outside_every_tier() {
        for id in [0x010, 0x0ff, 0x300, 0x3ff, 0x500, 0x7ff, 0x800, 0xffff] {
            assert_eq!(
                decode(&frame(id, 2, 0x01, 0x01)),
                Err(Status::BadId),
                "id {id:#x}"
            );
        }
    }

    // frames from other tiers aren't heartbeats, even with a valid looking payload
    #[test]
    fn decode_rejects_frames_from_other_tiers() {
        for id in [0x120, 0x12f, 0x220, 0x221, 0x420, 0x42f] {
            assert_eq!(
                decode(&frame(id, 2, 0x01, 0x01)),
                Err(Status::BadTier),
                "id {id:#x}"
            );
        }
    }

    // heartbeat IDs can't have an index, covers the invalid index criteria
    #[test]
    fn heartbeat_ids_cannot_carry_a_nonzero_index() {
        for index in 1..=u8::MAX {
            let parts = IdParts {
                tier: Tier::Heartbeat,
                node: ADCS_NOMINAL.source_node,
                index,
            };
            assert_eq!(make_id(parts), Err(Status::BadIndex), "index {index}");
        }
    }

    // every DLC except 2 gets rejected
    #[test]
    fn decode_requires_exact_dlc() {
        for dlc in u8::MIN..=u8::MAX {
            if dlc != DLC {
                assert_eq!(
                    decode(&frame(0x002, dlc, 0x01, 0x01)),
                    Err(Status::BadDlc),
                    "dlc {dlc}"
                );
            }
        }
    }

    // every version except 0x01 gets rejected
    #[test]
    fn decode_rejects_every_version_except_one() {
        for version in u8::MIN..=u8::MAX {
            if version != PROTOCOL_VERSION {
                assert_eq!(
                    decode(&frame(0x002, 2, version, 0x01)),
                    Err(Status::BadValue),
                    "version {version:#x}"
                );
            }
        }
    }

    // reserved (0x05 to 0xFE) and invalid (0xFF) states get rejected
    #[test]
    fn decode_rejects_reserved_and_invalid_states() {
        for state in 0x05..=u8::MAX {
            assert_eq!(
                decode(&frame(0x002, 2, 0x01, state)),
                Err(Status::BadValue),
                "state {state:#x}"
            );
        }
    }

    // bytes past the DLC are ignored
    #[test]
    fn decode_ignores_bytes_beyond_dlc() {
        let received = CanFrame {
            id: 0x002,
            dlc: 2,
            data: [0x01, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff],
        };
        assert_eq!(decode(&received), Ok(ADCS_NOMINAL));
    }

    // with multiple problems, the first failed check is returned
    #[test]
    fn decode_reports_the_first_failed_check() {
        // bad node comes before bad DLC
        assert_eq!(decode(&frame(0x007, 9, 0x00, 0xff)), Err(Status::BadNode));
        // wrong tier comes before DLC and payload
        assert_eq!(decode(&frame(0x420, 7, 0x00, 0xff)), Err(Status::BadTier));
        // bad DLC comes before bad version
        assert_eq!(decode(&frame(0x002, 3, 0x00, 0x01)), Err(Status::BadDlc));
    }

    // failed decode doesn't change the caller's data or the frame
    #[test]
    fn failed_decode_leaves_caller_data_unchanged() {
        let mut latest = ADCS_NOMINAL;
        let received = frame(0x002, 2, 0x01, 0x05);
        let before = received;

        match decode(&received) {
            Ok(heartbeat) => latest = heartbeat,
            Err(error) => assert_eq!(error, Status::BadValue),
        }

        assert_eq!(latest, ADCS_NOMINAL);
        assert_eq!(received, before);
    }
}