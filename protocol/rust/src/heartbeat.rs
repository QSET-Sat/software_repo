//! Version 1 node-heartbeat message definitions.

/// Protocol version carried by every version 1 heartbeat.
pub const PROTOCOL_VERSION: u8 = 1;
/// Exact payload length for a heartbeat frame.
pub const DLC: u8 = 2;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wire_constants_match_version_one_catalog() {
        assert_eq!(PROTOCOL_VERSION, 1);
        assert_eq!(DLC, 2);
    }
}
