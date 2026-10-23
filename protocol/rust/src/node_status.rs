//! Version 1 node-status message definitions.

/// Telemetry-tier message index assigned to node status.
pub const INDEX: u8 = 0;
/// Exact payload length for a node-status frame.
pub const DLC: u8 = 7;
/// Request sequence used for unsolicited status telemetry.
pub const UNSOLICITED_SEQUENCE: u8 = 0;
/// Active-fault-count value used when the count is unavailable.
pub const FAULT_COUNT_UNAVAILABLE: u8 = 0xff;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wire_constants_match_version_one_catalog() {
        assert_eq!(INDEX, 0);
        assert_eq!(DLC, 7);
        assert_eq!(UNSOLICITED_SEQUENCE, 0);
        assert_eq!(FAULT_COUNT_UNAVAILABLE, 0xff);
    }
}
