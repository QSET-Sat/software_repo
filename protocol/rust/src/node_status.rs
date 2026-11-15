//! Version 1 node-status message definitions.

use crate::common::{make_id, validate_frame, CanFrame, IdParts, NodeState, Status, Tier, MAX_DLC};

/// Telemetry-tier message index assigned to node status.
pub const INDEX: u8 = 0;
/// Exact payload length for a node-status frame.
pub const DLC: u8 = 7;
/// Request sequence used for unsolicited status telemetry.
pub const UNSOLICITED_SEQUENCE: u8 = 0;
/// Active-fault-count value used when the count is unavailable.
pub const FAULT_COUNT_UNAVAILABLE: u8 = 0xff;

//field positions in frame, uptime is bytes 2 to 5
const SEQUENCE_BYTE: usize = 0;
const STATE_BYTE: usize = 1;
const UPTIME_START: usize = 2;
const FAULTS_BYTE: usize = 6;

//struct for node status
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct NodeStatus {
    //node sending its status
    pub source_node: u8,
    //sequence from the request, 0 if unsolicited
    pub sequence: u8,
    //current state of the node
    pub state: NodeState,
    //seconds since the node started
    pub uptime_s: u32,
    //active fault count 0 to 254, None if unavailable
    pub active_faults: Option<u8>,
}

impl NodeStatus {
    //true if the node sent this on its own, not as a reply
    pub fn is_unsolicited(&self) -> bool {
        self.sequence == UNSOLICITED_SEQUENCE
    }
}

//encode function, builds CAN frame
//returns BadNode if node is unregistered
pub fn encode(node_status: NodeStatus) -> Result<CanFrame, Status> {
    //node made with make_id to ensure node gets checked
    let id = make_id(IdParts {
        tier: Tier::Telemetry,
        node: node_status.source_node,
        index: INDEX, //always zero
    })?;

    //255 means unavailable
    let faults = match node_status.active_faults {
        Some(FAULT_COUNT_UNAVAILABLE) => return Err(Status::BadValue),
        Some(count) => count,
        None => FAULT_COUNT_UNAVAILABLE,
    };

    //uptime is sent little-endian, lowest byte first
    let [up0, up1, up2, up3] = node_status.uptime_s.to_le_bytes();

    //all zeros so unused bytes are 0
    let mut frame = CanFrame {
        id,
        dlc: DLC,
        data: [0; MAX_DLC as usize],
    };

    frame.data[SEQUENCE_BYTE] = node_status.sequence;
    frame.data[STATE_BYTE] = u8::from(node_status.state);
    frame.data[UPTIME_START] = up0;
    frame.data[UPTIME_START + 1] = up1;
    frame.data[UPTIME_START + 2] = up2;
    frame.data[UPTIME_START + 3] = up3;
    frame.data[FAULTS_BYTE] = faults;

    Ok(frame)
}

//decode function, reads CAN frame
//returns first failed check: BadId/BadNode, BadTier, BadIndex, BadDlc, BadValue
pub fn decode(frame: &CanFrame) -> Result<NodeStatus, Status> {
    //ID range, registered node, DLC 8 or less
    let parts = validate_frame(frame)?;

    //has to be telemetry
    if parts.tier != Tier::Telemetry {
        return Err(Status::BadTier);
    }

    //has to be telemetry index 0
    if parts.index != INDEX {
        return Err(Status::BadIndex);
    }

    //DLC has to be exactly 7
    if frame.dlc != DLC {
        return Err(Status::BadDlc);
    }

    //state has to be 0x00 to 0x04
    let state = NodeState::try_from(frame.data[STATE_BYTE])?;

    //uptime is little-endian, lowest byte first
    let uptime_s = u32::from_le_bytes([
        frame.data[UPTIME_START],
        frame.data[UPTIME_START + 1],
        frame.data[UPTIME_START + 2],
        frame.data[UPTIME_START + 3],
    ]);

    //255 means unavailable, every other value is a count
    let active_faults = match frame.data[FAULTS_BYTE] {
        FAULT_COUNT_UNAVAILABLE => None,
        count => Some(count),
    };

    //any sequence is valid, 0 means unsolicited
    Ok(NodeStatus {
        source_node: parts.node,
        sequence: frame.data[SEQUENCE_BYTE],
        state,
        uptime_s,
        active_faults,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::REGISTERED_NODES;

    //all 5 valid node states
    const ALL_STATES: [NodeState; 5] = [
        NodeState::Starting,
        NodeState::Nominal,
        NodeState::Degraded,
        NodeState::Safe,
        NodeState::Stopping,
    ];

    //golden vector: ADCS, sequence 42, nominal, one day uptime, no faults
    const ADCS_GOLDEN: NodeStatus = NodeStatus {
        source_node: 0x2,
        sequence: 42,
        state: NodeState::Nominal,
        uptime_s: 86_400,
        active_faults: Some(0),
    };

    //golden vector data bytes, byte 7 unused
    const GOLDEN_DATA: [u8; 8] = [0x2A, 0x01, 0x80, 0x51, 0x01, 0x00, 0x00, 0x00];

    //test frame with the given data
    fn frame(id: u16, dlc: u8, data: [u8; 8]) -> CanFrame {
        CanFrame { id, dlc, data }
    }

    //golden vector with a different ID and DLC
    fn golden_frame(id: u16, dlc: u8) -> CanFrame {
        frame(id, dlc, GOLDEN_DATA)
    }

    //checks if node is registered
    fn is_registered(node: u8) -> bool {
        REGISTERED_NODES.iter().any(|entry| entry.id == node)
    }

    //encodes then decodes
    fn round_trip(status: NodeStatus) -> Result<NodeStatus, Status> {
        decode(&encode(status)?)
    }

    //constants match version 1
    #[test]
    fn wire_constants_match_version_one_catalog() {
        assert_eq!(INDEX, 0);
        assert_eq!(DLC, 7);
        assert_eq!(UNSOLICITED_SEQUENCE, 0);
        assert_eq!(FAULT_COUNT_UNAVAILABLE, 0xff);
    }

    //golden vector encodes exactly
    #[test]
    fn adcs_golden_vector_encodes_exactly() {
        assert_eq!(encode(ADCS_GOLDEN), Ok(golden_frame(0x420, 7)));
    }

    //golden vector decodes
    #[test]
    fn adcs_golden_vector_decodes() {
        assert_eq!(decode(&golden_frame(0x420, 7)), Ok(ADCS_GOLDEN));
    }

    //every node and state round trips
    #[test]
    fn every_node_and_state_round_trips() {
        for node in REGISTERED_NODES {
            for state in ALL_STATES {
                let status = NodeStatus {
                    source_node: node.id,
                    state,
                    ..ADCS_GOLDEN
                };
                let encoded = encode(status).unwrap();
                assert_eq!(encoded.id, 0x400 + u16::from(node.id) * 0x10);
                assert_eq!(encoded.dlc, DLC);
                assert_eq!(encoded.data[STATE_BYTE], u8::from(state));
                assert_eq!(encoded.data[7], 0);
                assert_eq!(decode(&encoded), Ok(status));
            }
        }
    }

    //sequence 0 accepted as unsolicited
    #[test]
    fn unsolicited_sequence_zero_is_accepted() {
        let status = NodeStatus {
            sequence: UNSOLICITED_SEQUENCE,
            ..ADCS_GOLDEN
        };
        assert_eq!(encode(status).unwrap().data[SEQUENCE_BYTE], 0);
        assert_eq!(round_trip(status), Ok(status));
        assert!(status.is_unsolicited());
        assert!(!ADCS_GOLDEN.is_unsolicited());
    }

    //every sequence round trips
    #[test]
    fn every_sequence_round_trips() {
        for sequence in u8::MIN..=u8::MAX {
            let status = NodeStatus {
                sequence,
                ..ADCS_GOLDEN
            };
            assert_eq!(round_trip(status), Ok(status), "seq {sequence}");
        }
    }

    //uptime 0 and max round trip
    #[test]
    fn boundary_uptimes_round_trip() {
        for (uptime_s, bytes) in [(0, [0x00; 4]), (u32::MAX, [0xff; 4])] {
            let status = NodeStatus {
                uptime_s,
                ..ADCS_GOLDEN
            };
            let encoded = encode(status).unwrap();
            assert_eq!(encoded.data[2..6], bytes, "uptime {uptime_s:#x}");
            assert_eq!(decode(&encoded), Ok(status), "uptime {uptime_s:#x}");
        }
    }

    //uptime is little-endian
    #[test]
    fn uptime_is_little_endian() {
        let status = NodeStatus {
            uptime_s: 0x0102_0304,
            ..ADCS_GOLDEN
        };
        assert_eq!(encode(status).unwrap().data[2..6], [0x04, 0x03, 0x02, 0x01]);
    }

    //fault counts 0, 254 and unavailable round trip
    #[test]
    fn boundary_fault_counts_round_trip() {
        for (active_faults, wire) in [(Some(0), 0x00), (Some(254), 0xfe), (None, 0xff)] {
            let status = NodeStatus {
                active_faults,
                ..ADCS_GOLDEN
            };
            let encoded = encode(status).unwrap();
            assert_eq!(encoded.data[FAULTS_BYTE], wire, "faults {active_faults:?}");
            assert_eq!(decode(&encoded), Ok(status), "faults {active_faults:?}");
        }
    }

    //every fault byte decodes, 255 is unavailable
    #[test]
    fn every_fault_byte_decodes() {
        for wire in u8::MIN..=u8::MAX {
            let mut data = GOLDEN_DATA;
            data[FAULTS_BYTE] = wire;
            let expected = if wire == 0xff { None } else { Some(wire) };
            assert_eq!(
                decode(&frame(0x420, 7, data)).map(|status| status.active_faults),
                Ok(expected),
                "faults {wire:#x}"
            );
        }
    }

    //fault count Some(255) rejected on encode
    #[test]
    fn encode_rejects_fault_count_255() {
        let status = NodeStatus {
            active_faults: Some(FAULT_COUNT_UNAVAILABLE),
            ..ADCS_GOLDEN
        };
        assert_eq!(encode(status), Err(Status::BadValue));
    }

    //unregistered nodes rejected on encode
    #[test]
    fn encode_rejects_unregistered_nodes() {
        for node in u8::MIN..=u8::MAX {
            if !is_registered(node) {
                let status = NodeStatus {
                    source_node: node,
                    ..ADCS_GOLDEN
                };
                assert_eq!(encode(status), Err(Status::BadNode), "node {node:#x}");
            }
        }
    }

    //unregistered nodes rejected on decode
    #[test]
    fn decode_rejects_unregistered_nodes() {
        for node in 0x0..=0xf_u16 {
            if !is_registered(node as u8) {
                let id = 0x400 + node * 0x10;
                assert_eq!(
                    decode(&golden_frame(id, 7)),
                    Err(Status::BadNode),
                    "id {id:#x}"
                );
            }
        }
    }

    //IDs outside every tier rejected
    #[test]
    fn decode_rejects_ids_outside_every_tier() {
        for id in [0x010, 0x0ff, 0x300, 0x3ff, 0x500, 0x7ff, 0x800, 0xffff] {
            assert_eq!(
                decode(&golden_frame(id, 7)),
                Err(Status::BadId),
                "id {id:#x}"
            );
        }
    }

    //other tiers rejected
    #[test]
    fn decode_rejects_frames_from_other_tiers() {
        for id in [0x002, 0x120, 0x12f, 0x220, 0x22f] {
            assert_eq!(
                decode(&golden_frame(id, 7)),
                Err(Status::BadTier),
                "id {id:#x}"
            );
        }
    }

    //telemetry indexes 1 to 15 rejected
    #[test]
    fn decode_rejects_other_telemetry_indexes() {
        for index in 1..=0xf_u16 {
            let id = 0x420 + index;
            assert_eq!(
                decode(&golden_frame(id, 7)),
                Err(Status::BadIndex),
                "id {id:#x}"
            );
        }
    }

    //every DLC except 7 rejected
    #[test]
    fn decode_requires_exact_dlc() {
        for dlc in u8::MIN..=u8::MAX {
            if dlc != DLC {
                assert_eq!(
                    decode(&golden_frame(0x420, dlc)),
                    Err(Status::BadDlc),
                    "dlc {dlc}"
                );
            }
        }
    }

    //reserved and invalid states rejected
    #[test]
    fn decode_rejects_reserved_and_invalid_states() {
        for state in 0x05..=u8::MAX {
            let mut data = GOLDEN_DATA;
            data[STATE_BYTE] = state;
            assert_eq!(
                decode(&frame(0x420, 7, data)),
                Err(Status::BadValue),
                "state {state:#x}"
            );
        }
    }

    //bytes past DLC ignored
    #[test]
    fn decode_ignores_bytes_beyond_dlc() {
        let mut data = GOLDEN_DATA;
        data[7] = 0xff;
        assert_eq!(decode(&frame(0x420, 7, data)), Ok(ADCS_GOLDEN));
    }

    //first failed check is returned
    #[test]
    fn decode_reports_the_first_failed_check() {
        let mut bad_state = GOLDEN_DATA;
        bad_state[STATE_BYTE] = 0xff;
        //node before DLC
        assert_eq!(decode(&frame(0x470, 9, bad_state)), Err(Status::BadNode));
        //tier before index
        assert_eq!(decode(&frame(0x221, 8, bad_state)), Err(Status::BadTier));
        //index before DLC
        assert_eq!(decode(&frame(0x421, 8, bad_state)), Err(Status::BadIndex));
        //DLC before state
        assert_eq!(decode(&frame(0x420, 8, bad_state)), Err(Status::BadDlc));
    }

    //node before fault count on encode
    #[test]
    fn encode_reports_bad_node_before_bad_fault_count() {
        let status = NodeStatus {
            source_node: 0,
            active_faults: Some(FAULT_COUNT_UNAVAILABLE),
            ..ADCS_GOLDEN
        };
        assert_eq!(encode(status), Err(Status::BadNode));
    }

    //failed decode changes nothing
    #[test]
    fn failed_decode_leaves_caller_data_unchanged() {
        let mut latest = ADCS_GOLDEN;
        let mut data = GOLDEN_DATA;
        data[STATE_BYTE] = 0x05;
        let received = frame(0x420, 7, data);
        let before = received;

        match decode(&received) {
            Ok(status) => latest = status,
            Err(error) => assert_eq!(error, Status::BadValue),
        }

        assert_eq!(latest, ADCS_GOLDEN);
        assert_eq!(received, before);
    }
}
