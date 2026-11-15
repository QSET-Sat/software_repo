//! Version 1 request-status message definitions.

use crate::common::{make_id, validate_frame, CanFrame, IdParts, Status, Tier, MAX_DLC};

/// Command-tier message index assigned to status requests.
pub const INDEX: u8 = 0;
/// Exact payload length for a request-status frame.
pub const DLC: u8 = 1;
/// Sequence 0 is reserved for unsolicited node status.
pub const RESERVED_SEQUENCE: u8 = 0;

//sequence position in frame
const SEQUENCE_BYTE: usize = 0;

//struct for request status
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RequestStatus {
    //destination node for status request
    pub destination_node: u8,
    // "ticket number" for each status request. used by OBC to determine which reply corresponds to each request
    pub sequence: u8,
}

//encode function, builds CAN frame
//returns BadNode if node is unregistered, BadValue if sequence is 0
pub fn encode(request_status: RequestStatus) -> Result<CanFrame, Status> {
    //node made with make_id to ensure node gets checked
    let id = make_id(IdParts {
        tier: Tier::Command,
        node: request_status.destination_node,
        index: INDEX, //always zero
    })?;

    //sequence 0 is invalid in a request
    if request_status.sequence == RESERVED_SEQUENCE {
        return Err(Status::BadValue);
    }

    //all zeros so unused bytes are 0
    let mut frame = CanFrame {
        id,
        dlc: DLC,
        data: [0; MAX_DLC as usize],
    };

    frame.data[SEQUENCE_BYTE] = request_status.sequence;

    Ok(frame)
}

//decode function, reads CAN frame
//returns first failed check: BadId/BadNode, BadTier, BadIndex, BadDlc, BadValue
pub fn decode(frame: &CanFrame) -> Result<RequestStatus, Status> {
    //ID range, registered node, DLC 8 or less
    let parts = validate_frame(frame)?;

    //has to be a command
    if parts.tier != Tier::Command {
        return Err(Status::BadTier);
    }

    //has to be command index 0
    if parts.index != INDEX {
        return Err(Status::BadIndex);
    }

    //DLC has to be exactly 1
    if frame.dlc != DLC {
        return Err(Status::BadDlc);
    }

    //sequence 0 is invalid in a request
    let sequence = frame.data[SEQUENCE_BYTE];
    if sequence == RESERVED_SEQUENCE {
        return Err(Status::BadValue);
    }

    Ok(RequestStatus {
        destination_node: parts.node,
        sequence,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::REGISTERED_NODES;

    //golden vector: ADCS, sequence 42
    const ADCS_SEQ_42: RequestStatus = RequestStatus {
        destination_node: 0x2,
        sequence: 42,
    };

    //test frame, sequence in byte 0
    fn frame(id: u16, dlc: u8, sequence: u8) -> CanFrame {
        CanFrame {
            id,
            dlc,
            data: [sequence, 0, 0, 0, 0, 0, 0, 0],
        }
    }

    //checks if node is registered
    fn is_registered(node: u8) -> bool {
        REGISTERED_NODES.iter().any(|entry| entry.id == node)
    }

    //constants match version 1
    #[test]
    fn wire_constants_match_version_one_catalog() {
        assert_eq!(INDEX, 0);
        assert_eq!(DLC, 1);
        assert_eq!(RESERVED_SEQUENCE, 0);
    }

    //golden vector encodes exactly
    #[test]
    fn adcs_golden_vector_encodes_exactly() {
        assert_eq!(
            encode(ADCS_SEQ_42),
            Ok(CanFrame {
                id: 0x220,
                dlc: 1,
                data: [0x2A, 0, 0, 0, 0, 0, 0, 0],
            })
        );
    }

    //golden vector decodes
    #[test]
    fn adcs_golden_vector_decodes() {
        assert_eq!(decode(&frame(0x220, 1, 0x2A)), Ok(ADCS_SEQ_42));
    }

    //sequences 1 and 255 round trip
    #[test]
    fn boundary_sequences_round_trip_for_every_node() {
        for node in REGISTERED_NODES {
            for sequence in [1, 255] {
                let request = RequestStatus {
                    destination_node: node.id,
                    sequence,
                };
                let encoded = encode(request).unwrap();
                assert_eq!(encoded.id, 0x200 + u16::from(node.id) * 0x10);
                assert_eq!(encoded.dlc, DLC);
                assert_eq!(encoded.data[SEQUENCE_BYTE], sequence);
                assert_eq!(encoded.data[1..], [0; 7]);
                assert_eq!(decode(&encoded), Ok(request));
            }
        }
    }

    //every valid sequence round trips
    #[test]
    fn every_valid_sequence_round_trips() {
        for sequence in 1..=u8::MAX {
            let request = RequestStatus {
                destination_node: ADCS_SEQ_42.destination_node,
                sequence,
            };
            assert_eq!(
                decode(&encode(request).unwrap()),
                Ok(request),
                "seq {sequence}"
            );
        }
    }

    //sequence 0 rejected on encode
    #[test]
    fn encode_rejects_sequence_zero() {
        for node in REGISTERED_NODES {
            let request = RequestStatus {
                destination_node: node.id,
                sequence: 0,
            };
            assert_eq!(encode(request), Err(Status::BadValue));
        }
    }

    //sequence 0 rejected on decode
    #[test]
    fn decode_rejects_sequence_zero() {
        assert_eq!(decode(&frame(0x220, 1, 0x00)), Err(Status::BadValue));
    }

    //unregistered nodes rejected on encode
    #[test]
    fn encode_rejects_unregistered_nodes() {
        for node in u8::MIN..=u8::MAX {
            if !is_registered(node) {
                let request = RequestStatus {
                    destination_node: node,
                    sequence: 42,
                };
                assert_eq!(encode(request), Err(Status::BadNode), "node {node:#x}");
            }
        }
    }

    //unregistered nodes rejected on decode
    #[test]
    fn decode_rejects_unregistered_nodes() {
        for node in 0x0..=0xf_u16 {
            if !is_registered(node as u8) {
                let id = 0x200 + node * 0x10;
                assert_eq!(
                    decode(&frame(id, 1, 0x2A)),
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
                decode(&frame(id, 1, 0x2A)),
                Err(Status::BadId),
                "id {id:#x}"
            );
        }
    }

    //other tiers rejected
    #[test]
    fn decode_rejects_frames_from_other_tiers() {
        for id in [0x002, 0x120, 0x12f, 0x420, 0x42f] {
            assert_eq!(
                decode(&frame(id, 1, 0x2A)),
                Err(Status::BadTier),
                "id {id:#x}"
            );
        }
    }

    //command indexes 1 to 15 rejected
    #[test]
    fn decode_rejects_other_command_indexes() {
        for index in 1..=0xf_u16 {
            let id = 0x220 + index;
            assert_eq!(
                decode(&frame(id, 1, 0x2A)),
                Err(Status::BadIndex),
                "id {id:#x}"
            );
        }
    }

    //every DLC except 1 rejected
    #[test]
    fn decode_requires_exact_dlc() {
        for dlc in u8::MIN..=u8::MAX {
            if dlc != DLC {
                assert_eq!(
                    decode(&frame(0x220, dlc, 0x2A)),
                    Err(Status::BadDlc),
                    "dlc {dlc}"
                );
            }
        }
    }

    //bytes past DLC ignored
    #[test]
    fn decode_ignores_bytes_beyond_dlc() {
        let received = CanFrame {
            id: 0x220,
            dlc: 1,
            data: [0x2A, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff],
        };
        assert_eq!(decode(&received), Ok(ADCS_SEQ_42));
    }

    //first failed check is returned
    #[test]
    fn decode_reports_the_first_failed_check() {
        //node before DLC
        assert_eq!(decode(&frame(0x270, 9, 0x00)), Err(Status::BadNode));
        //tier before index
        assert_eq!(decode(&frame(0x420, 7, 0x00)), Err(Status::BadTier));
        //index before DLC
        assert_eq!(decode(&frame(0x221, 2, 0x00)), Err(Status::BadIndex));
        //DLC before sequence
        assert_eq!(decode(&frame(0x220, 2, 0x00)), Err(Status::BadDlc));
    }

    //node before sequence on encode
    #[test]
    fn encode_reports_bad_node_before_bad_sequence() {
        let request = RequestStatus {
            destination_node: 0,
            sequence: 0,
        };
        assert_eq!(encode(request), Err(Status::BadNode));
    }

    //failed decode changes nothing
    #[test]
    fn failed_decode_leaves_caller_data_unchanged() {
        let mut latest = ADCS_SEQ_42;
        let received = frame(0x220, 1, 0x00);
        let before = received;

        match decode(&received) {
            Ok(request) => latest = request,
            Err(error) => assert_eq!(error, Status::BadValue),
        }

        assert_eq!(latest, ADCS_SEQ_42);
        assert_eq!(received, before);
    }
}