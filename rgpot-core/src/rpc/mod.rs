// MIT License
// Copyright 2023--present rgpot developers

//! Cap'n Proto RPC module for distributed potential calculations.
//!
//! This module is only compiled when the `rpc` Cargo feature is enabled.
//! It reuses the existing `CppCore/rgpot/rpc/Potentials.capnp` schema that
//! the C++ server also uses, ensuring full wire-format compatibility between
//! Rust and C++ endpoints.
//!
//! ## Schema
//!
//! The [`schema`] submodule contains the Rust code generated from
//! `Potentials.capnp` by `capnpc` during `build.rs`. The schema defines:
//!
//! - `ForceInput` — positions, atomic numbers, simulation cell.
//! - `PotentialResult` — energy and forces.
//! - `Potential` interface — a single `calculate` RPC method.
//!
//! ## Client
//!
//! [`client::RpcClient`] connects to a remote rgpot server and provides a
//! synchronous `calculate()` method. Internally it owns a tokio runtime so
//! that the blocking C API can drive async I/O. Exposed to C via
//! `rgpot_rpc_client_new` / `rgpot_rpc_calculate` / `rgpot_rpc_client_free`.
//!
//! ## Server
//!
//! [`server::rgpot_rpc_server_start`] accepts a `rgpot_potential_t` handle
//! (callback-backed) and listens for incoming Cap'n Proto RPC connections.
//! Each `calculate` call is dispatched to the callback. The server blocks the
//! calling thread.

/// Re-export of the generated Cap'n Proto schema from the crate root.
///
/// The generated code uses `crate::Potentials_capnp` internally, so the
/// module must live at the crate root.  This re-export provides a
/// convenient `rpc::schema` alias.
pub use crate::Potentials_capnp as schema;

/// Stable family name for the rgpot Cap'n Proto protocol.
pub const PROTOCOL_FAMILY: &str = "rgpot.potentials";
/// Wire-incompatible protocol revision.
pub const PROTOCOL_MAJOR: u16 = 1;
/// Additive wire-compatible protocol revision.
pub const PROTOCOL_MINOR: u16 = 0;
/// Cap'n Proto schema id declared by `Potentials.capnp`.
pub const SCHEMA_ID: &str = "bd1f89fa17369103";

/// Return whether a remote endpoint can speak this protocol family.
pub fn protocol_compatible(family: &str, major: u16, minor: u16) -> bool {
    family == PROTOCOL_FAMILY && major == PROTOCOL_MAJOR && minor <= PROTOCOL_MINOR
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_additive_minor_revisions() {
        assert!(protocol_compatible(PROTOCOL_FAMILY, PROTOCOL_MAJOR, 0));
    }

    #[test]
    fn rejects_other_families_and_major_revisions() {
        assert!(!protocol_compatible("other.protocol", PROTOCOL_MAJOR, 0));
        assert!(!protocol_compatible(PROTOCOL_FAMILY, PROTOCOL_MAJOR + 1, 0));
        assert!(!protocol_compatible(PROTOCOL_FAMILY, PROTOCOL_MAJOR, PROTOCOL_MINOR + 1));
    }
}

pub mod client;
pub mod server;
