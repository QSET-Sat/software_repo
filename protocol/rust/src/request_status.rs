//! Version 1 request-status message definitions.

/// Command-tier message index assigned to status requests.
pub const INDEX: u8 = 0;
/// Exact payload length for a request-status frame.
pub const DLC: u8 = 1;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wire_constants_match_version_one_catalog() {
        assert_eq!(INDEX, 0);
        assert_eq!(DLC, 1);
    }
}
