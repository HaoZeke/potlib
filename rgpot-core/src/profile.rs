// MIT License
// Copyright 2023--present rgpot developers

//! Native loader for the minimum potential ABI profile.
//!
//! A backend prefix such as `nwchemc` or `cpmdc` selects one conforming
//! shared library. The library receives `PotentialConfig` bytes once, keeps a
//! persistent session, and receives `ForceInput` bytes for every geometry.
//! Results return as `PotentialResult` bytes in the units requested by the
//! input message.

use std::ffi::{c_char, c_int, c_void, CStr};
use std::fmt;
use std::mem::ManuallyDrop;
use std::path::Path;

use capnp::message::{Builder, ReaderOptions};
use capnp::serialize;
use libloading::Library;

use crate::Potentials_capnp::{capabilities, force_input, potential_result};

/// One atomistic evaluation sent through a profile session.
#[derive(Debug, Clone, Copy)]
pub struct ProfileRequest<'a> {
    /// Flat Cartesian coordinates, three values per atom.
    pub positions: &'a [f64],
    /// Atomic numbers in the same atom order as `positions`.
    pub atomic_numbers: &'a [i32],
    /// Optional row-major 3x3 simulation cell; molecular inputs use `None`.
    pub box_matrix: Option<&'a [f64; 9]>,
    /// Unit expression for coordinates and cell vectors.
    pub length_unit: &'a str,
    /// Unit expression requested for energy and forces.
    pub energy_unit: &'a str,
}

/// Energy and forces returned by one fused backend calculation.
#[derive(Debug, Clone, PartialEq)]
pub struct ProfileEvaluation {
    /// Energy in the `ProfileRequest::energy_unit` unit.
    pub energy: f64,
    /// Forces in energy-unit per length-unit.
    pub forces: Vec<f64>,
}

/// Error raised while loading or driving a potential profile.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProfileError {
    message: String,
    capability: Option<CapabilityMismatch>,
}

impl ProfileError {
    fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
            capability: None,
        }
    }

    fn capability(
        field: &'static str,
        expected: impl Into<String>,
        received: impl Into<String>,
    ) -> Self {
        let expected = expected.into();
        let received = received.into();
        let label = field.replace('_', " ");
        Self {
            message: format!("{label} mismatch: expected {expected}, received {received}"),
            capability: Some(CapabilityMismatch {
                field,
                expected,
                received,
            }),
        }
    }

    /// Return structured compatibility details when this is a capability error.
    pub fn capability_mismatch(&self) -> Option<&CapabilityMismatch> {
        self.capability.as_ref()
    }
}

impl fmt::Display for ProfileError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.message)
    }
}

impl std::error::Error for ProfileError {}

impl From<capnp::Error> for ProfileError {
    fn from(error: capnp::Error) -> Self {
        Self::new(error.to_string())
    }
}

/// Result type for profile loading and evaluation.
pub type ProfileResult<T> = Result<T, ProfileError>;

/// Structured expected/received values for a capability negotiation failure.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CapabilityMismatch {
    /// Stable capability field name.
    pub field: &'static str,
    /// Consumer requirement or accepted range.
    pub expected: String,
    /// Producer value received from the descriptor.
    pub received: String,
}

/// Compatibility values required from a backend capability descriptor.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CapabilityRequirements {
    /// Required Cap'n Proto protocol family.
    pub protocol_family: &'static str,
    /// Maximum wire-compatible protocol major/minor understood by this loader.
    pub protocol_major: u16,
    /// Maximum additive protocol revision understood by this loader.
    pub protocol_minor: u16,
    /// Required schema identity.
    pub schema_id: &'static str,
    /// Required eindir ABI major/minor.
    pub bridge_abi_major: u16,
    /// Maximum eindir ABI minor understood by this loader.
    pub bridge_abi_minor: u16,
    /// Required embedded-objective layout revision.
    pub bridge_layout: u32,
    /// Required DLPack major/minor.
    pub dlpack_major: u16,
    /// Maximum DLPack minor understood by this loader.
    pub dlpack_minor: u16,
    /// Required bridge capability bits.
    pub bridge_features: u64,
    /// Required backend operations, encoded by `Capabilities.Operation` ordinal.
    pub required_operations: u64,
}

impl Default for CapabilityRequirements {
    fn default() -> Self {
        Self {
            protocol_family: "rgpot.potentials",
            protocol_major: 1,
            protocol_minor: 0,
            schema_id: "bd1f89fa17369103",
            bridge_abi_major: 1,
            bridge_abi_minor: 0,
            bridge_layout: 1,
            dlpack_major: 1,
            dlpack_minor: 0,
            bridge_features: 0,
            required_operations: (1 << 0) | (1 << 1),
        }
    }
}

/// Validate a serialized backend capability descriptor before dispatch.
pub fn validate_capabilities(
    encoded: &[u8],
    requirements: &CapabilityRequirements,
) -> ProfileResult<()> {
    let mut bytes = encoded;
    let message = serialize::read_message_from_flat_slice(&mut bytes, ReaderOptions::new())?;
    let value = message.get_root::<capabilities::Reader>()?;
    let family = value
        .get_protocol_family()?
        .to_str()
        .map_err(|error| ProfileError::new(format!("invalid protocol family text: {error}")))?
        .to_owned();
    if family != requirements.protocol_family {
        return Err(ProfileError::capability(
            "protocol_family",
            requirements.protocol_family,
            family,
        ));
    }
    let major = value.get_protocol_major();
    if major != requirements.protocol_major {
        return Err(ProfileError::capability(
            "protocol_major",
            requirements.protocol_major.to_string(),
            major.to_string(),
        ));
    }
    let minor = value.get_protocol_minor();
    if minor > requirements.protocol_minor {
        return Err(ProfileError::capability(
            "protocol_minor",
            format!("<= {}", requirements.protocol_minor),
            minor.to_string(),
        ));
    }
    let schema_id = value
        .get_schema_id()?
        .to_str()
        .map_err(|error| ProfileError::new(format!("invalid schema identity text: {error}")))?
        .to_owned();
    if schema_id != requirements.schema_id {
        return Err(ProfileError::capability(
            "schema_id",
            requirements.schema_id,
            schema_id,
        ));
    }
    let bridge_major = value.get_bridge_abi_major();
    if bridge_major != requirements.bridge_abi_major {
        return Err(ProfileError::capability(
            "bridge_abi_major",
            requirements.bridge_abi_major.to_string(),
            bridge_major.to_string(),
        ));
    }
    let bridge_minor = value.get_bridge_abi_minor();
    if bridge_minor > requirements.bridge_abi_minor {
        return Err(ProfileError::capability(
            "bridge_abi_minor",
            format!("<= {}", requirements.bridge_abi_minor),
            bridge_minor.to_string(),
        ));
    }
    let layout = value.get_bridge_layout();
    if layout != requirements.bridge_layout {
        return Err(ProfileError::capability(
            "bridge_layout",
            requirements.bridge_layout.to_string(),
            layout.to_string(),
        ));
    }
    let dlpack_major = value.get_dlpack_major();
    if dlpack_major != requirements.dlpack_major {
        return Err(ProfileError::capability(
            "dlpack_major",
            requirements.dlpack_major.to_string(),
            dlpack_major.to_string(),
        ));
    }
    let dlpack_minor = value.get_dlpack_minor();
    if dlpack_minor > requirements.dlpack_minor {
        return Err(ProfileError::capability(
            "dlpack_minor",
            format!("<= {}", requirements.dlpack_minor),
            dlpack_minor.to_string(),
        ));
    }
    let features = value.get_bridge_features();
    let missing = requirements.bridge_features & !features;
    if missing != 0 {
        return Err(ProfileError::capability(
            "bridge_features",
            format!("contains 0x{missing:016x}"),
            format!("0x{features:016x}"),
        ));
    }
    let mut operations = 0u64;
    for operation in value.get_operations()?.iter() {
        let operation = operation
            .map_err(|error| ProfileError::new(format!("invalid capability operation: {error}")))?;
        operations |= 1u64 << u16::from(operation);
    }
    let missing_operations = requirements.required_operations & !operations;
    if missing_operations != 0 {
        return Err(ProfileError::capability(
            "operations",
            format!("contains 0x{missing_operations:016x}"),
            format!("0x{operations:016x}"),
        ));
    }
    Ok(())
}

/// Read the optional producer build identity from a capability descriptor.
pub fn capability_build_identity(encoded: &[u8]) -> ProfileResult<Option<String>> {
    let mut bytes = encoded;
    let message = serialize::read_message_from_flat_slice(&mut bytes, ReaderOptions::new())?;
    let value = message.get_root::<capabilities::Reader>()?;
    if !value.has_build_identity() {
        return Ok(None);
    }
    let identity = value
        .get_build_identity()?
        .to_str()
        .map_err(|error| ProfileError::new(format!("invalid build identity text: {error}")))?
        .to_owned();
    Ok((!identity.is_empty()).then_some(identity))
}

/// Ordered shared-library candidates for a backend prefix.
pub fn library_candidates(prefix: &str, explicit_path: Option<&str>) -> Vec<String> {
    let mut candidates = Vec::new();
    if let Some(path) = explicit_path.filter(|path| !path.is_empty()) {
        candidates.push(path.to_owned());
    }

    let upper = prefix.to_ascii_uppercase();
    for variable in [format!("{upper}_LIBRARY"), format!("RGPOT_{upper}_ENGINE")] {
        if let Some(path) = std::env::var_os(variable).and_then(|value| value.into_string().ok()) {
            if !path.is_empty() && !candidates.contains(&path) {
                candidates.push(path);
            }
        }
    }

    for path in [
        format!("lib{prefix}.so"),
        format!("./lib{prefix}.so"),
        format!("lib{prefix}.dylib"),
        format!("./lib{prefix}.dylib"),
        format!("{prefix}.dll"),
    ] {
        if !candidates.contains(&path) {
            candidates.push(path);
        }
    }
    candidates
}

/// Serialize a request as the shared `ForceInput` carrier.
pub fn encode_force_input(request: &ProfileRequest<'_>) -> ProfileResult<Vec<u8>> {
    let expected_positions = request.atomic_numbers.len() * 3;
    if request.positions.len() != expected_positions {
        return Err(ProfileError::new(format!(
            "ForceInput has {} coordinates for {} atoms; expected {expected_positions}",
            request.positions.len(),
            request.atomic_numbers.len()
        )));
    }
    if request.length_unit.is_empty() || request.energy_unit.is_empty() {
        return Err(ProfileError::new("ForceInput units must be non-empty"));
    }

    let mut message = Builder::new_default();
    {
        let mut input = message.init_root::<force_input::Builder>();
        let mut positions = input
            .reborrow()
            .init_pos(request.positions.len().try_into().map_err(|_| {
                ProfileError::new("ForceInput coordinate count exceeds the schema limit")
            })?);
        for (index, value) in request.positions.iter().copied().enumerate() {
            positions.set(index as u32, value);
        }

        let mut atomic_numbers =
            input
                .reborrow()
                .init_atmnrs(request.atomic_numbers.len().try_into().map_err(|_| {
                    ProfileError::new("ForceInput atom count exceeds the schema limit")
                })?);
        for (index, value) in request.atomic_numbers.iter().copied().enumerate() {
            atomic_numbers.set(index as u32, value);
        }

        if let Some(box_matrix) = request.box_matrix {
            let mut encoded_box = input.reborrow().init_box(9);
            for (index, value) in box_matrix.iter().copied().enumerate() {
                encoded_box.set(index as u32, value);
            }
        }
        input.set_length_unit(request.length_unit);
        input.set_energy_unit(request.energy_unit);
    }
    Ok(serialize::write_message_to_words(&message))
}

/// Decode the energy and force fields from a shared `PotentialResult` carrier.
pub fn decode_potential_result(
    encoded: &[u8],
    expected_force_count: usize,
) -> ProfileResult<ProfileEvaluation> {
    let mut bytes = encoded;
    let message = serialize::read_message_from_flat_slice(&mut bytes, ReaderOptions::new())?;
    let result = message.get_root::<potential_result::Reader>()?;
    let forces = result.get_forces()?;
    if forces.len() as usize != expected_force_count {
        return Err(ProfileError::new(format!(
            "PotentialResult has {} forces; expected {expected_force_count}",
            forces.len()
        )));
    }
    Ok(ProfileEvaluation {
        energy: result.get_energy(),
        forces: (0..forces.len()).map(|index| forces.get(index)).collect(),
    })
}

#[repr(C)]
#[derive(Clone, Copy)]
struct ProfileAbiResult {
    ok: c_int,
    energy_h: f64,
    message: [c_char; 512],
}

type AbiVersionFn = unsafe extern "C" fn() -> c_int;
type VersionFn = unsafe extern "C" fn() -> *const c_char;
type LastErrorFn = unsafe extern "C" fn() -> *const c_char;
type AvailableFn = unsafe extern "C" fn() -> c_int;
type FinalizeFn = unsafe extern "C" fn();
type ConfigureFn = unsafe extern "C" fn(*const c_void, usize) -> c_int;
type SessionCreateFromConfigFn = unsafe extern "C" fn(*const c_void, usize) -> *mut c_void;
type SessionConfigureFn = unsafe extern "C" fn(*mut c_void, *const c_void, usize) -> c_int;
type SessionDestroyFn = unsafe extern "C" fn(*mut c_void);
type SessionCalculateResultFn = unsafe extern "C" fn(
    *mut c_void,
    *const c_void,
    usize,
    *mut c_void,
    usize,
    *mut usize,
) -> ProfileAbiResult;
type CalculateResultFromConfigFn = unsafe extern "C" fn(
    *const c_void,
    usize,
    *const c_void,
    usize,
    *mut c_void,
    usize,
    *mut usize,
) -> ProfileAbiResult;
type PotentialResultSizeFn = unsafe extern "C" fn(*const c_void, usize) -> usize;
type CapabilitiesResultFn = unsafe extern "C" fn(*mut c_void, usize, *mut usize) -> c_int;

fn read_capabilities(function: CapabilitiesResultFn) -> ProfileResult<Vec<u8>> {
    let mut required = 0usize;
    unsafe { function(std::ptr::null_mut(), 0, &mut required) };
    if required == 0 {
        return Err(ProfileError::new(
            "capabilities_result returned an empty capability message",
        ));
    }
    let mut encoded = vec![0u8; required];
    let mut written = 0usize;
    let status = unsafe { function(encoded.as_mut_ptr().cast(), encoded.len(), &mut written) };
    if status != 0 || written == 0 || written > encoded.len() {
        return Err(ProfileError::new(format!(
            "capabilities_result failed: status {status}, required {required}, written {written}"
        )));
    }
    encoded.truncate(written);
    Ok(encoded)
}

struct ProcessLifetime<T>(ManuallyDrop<T>);

impl<T> ProcessLifetime<T> {
    fn new(value: T) -> Self {
        Self(ManuallyDrop::new(value))
    }
}

struct RuntimeLifecycle {
    session: *mut c_void,
    destroy: SessionDestroyFn,
    finalize: FinalizeFn,
}

impl Drop for RuntimeLifecycle {
    fn drop(&mut self) {
        unsafe {
            (self.destroy)(self.session);
            self.session = std::ptr::null_mut();
            (self.finalize)();
        }
    }
}

/// One persistent session loaded through the prefix-generic potential profile.
pub struct ProfileSession {
    prefix: String,
    library_path: String,
    version: String,
    abi_version: i32,
    _library: ProcessLifetime<Library>,
    lifecycle: RuntimeLifecycle,
    last_error: LastErrorFn,
    calculate: SessionCalculateResultFn,
    result_size: PotentialResultSizeFn,
    capabilities: Vec<u8>,
}

impl ProfileSession {
    /// Load a conforming backend and create one session from `PotentialConfig` bytes.
    ///
    /// # Safety
    ///
    /// The selected shared library must implement the minimum potential ABI
    /// profile for `prefix` with the C layouts defined by the profile contract.
    pub unsafe fn load(
        prefix: &str,
        explicit_path: Option<&Path>,
        config: &[u8],
    ) -> ProfileResult<Self> {
        unsafe {
            Self::load_with_requirements(
                prefix,
                explicit_path,
                config,
                &CapabilityRequirements::default(),
            )
        }
    }

    /// Load a conforming backend with caller-selected capability requirements.
    ///
    /// Capability validation completes before the backend session is created,
    /// so operation-specific bridge bits are part of the loader handshake.
    pub unsafe fn load_with_requirements(
        prefix: &str,
        explicit_path: Option<&Path>,
        config: &[u8],
        requirements: &CapabilityRequirements,
    ) -> ProfileResult<Self> {
        if prefix.is_empty()
            || !prefix
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || byte == b'_')
        {
            return Err(ProfileError::new(format!(
                "invalid potential profile prefix: {prefix:?}"
            )));
        }
        if config.is_empty() {
            return Err(ProfileError::new("PotentialConfig message is empty"));
        }

        let explicit = explicit_path.and_then(Path::to_str);
        let (library, library_path) = open_first(prefix, explicit)?;
        let abi_version = unsafe { resolve::<AbiVersionFn>(&library, prefix, "abi_version")? };
        let version_fn = unsafe { resolve::<VersionFn>(&library, prefix, "version")? };
        let last_error = unsafe { resolve::<LastErrorFn>(&library, prefix, "last_error")? };
        let available = unsafe { resolve::<AvailableFn>(&library, prefix, "available")? };
        let finalize = unsafe { resolve::<FinalizeFn>(&library, prefix, "finalize")? };
        let _configure = unsafe { resolve::<ConfigureFn>(&library, prefix, "configure")? };
        let session_create = unsafe {
            resolve::<SessionCreateFromConfigFn>(&library, prefix, "session_create_from_config")?
        };
        let _session_configure =
            unsafe { resolve::<SessionConfigureFn>(&library, prefix, "session_configure")? };
        let session_destroy =
            unsafe { resolve::<SessionDestroyFn>(&library, prefix, "session_destroy")? };
        let calculate = unsafe {
            resolve::<SessionCalculateResultFn>(&library, prefix, "session_calculate_result")?
        };
        let _one_shot = unsafe {
            resolve::<CalculateResultFromConfigFn>(
                &library,
                prefix,
                "calculate_result_from_config",
            )?
        };
        let result_size = unsafe {
            resolve::<PotentialResultSizeFn>(
                &library,
                prefix,
                "potential_result_size_for_force_input",
            )?
        };
        let capabilities_result =
            unsafe { resolve::<CapabilitiesResultFn>(&library, prefix, "capabilities_result")? };

        if unsafe { available() } == 0 {
            return Err(ProfileError::new(format!(
                "potential profile {prefix} is not available"
            )));
        }
        let capabilities = read_capabilities(capabilities_result)?;
        validate_capabilities(&capabilities, requirements)?;
        let version = c_string(unsafe { version_fn() });
        let session = unsafe { session_create(config.as_ptr().cast(), config.len()) };
        if session.is_null() {
            let message = c_string(unsafe { last_error() });
            unsafe { finalize() };
            return Err(ProfileError::new(format!(
                "{prefix}_session_create_from_config failed: {message}"
            )));
        }

        Ok(Self {
            prefix: prefix.to_owned(),
            library_path,
            version,
            abi_version: unsafe { abi_version() },
            _library: ProcessLifetime::new(library),
            lifecycle: RuntimeLifecycle {
                session,
                destroy: session_destroy,
                finalize,
            },
            last_error,
            calculate,
            result_size,
            capabilities,
        })
    }

    /// Backend prefix used to resolve the profile symbols.
    pub fn prefix(&self) -> &str {
        &self.prefix
    }

    /// Path of the shared library selected by the candidate search.
    pub fn library_path(&self) -> &str {
        &self.library_path
    }

    /// Human-readable backend version string.
    pub fn version(&self) -> &str {
        &self.version
    }

    /// Numeric ABI generation reported by the backend.
    pub fn abi_version(&self) -> i32 {
        self.abi_version
    }

    /// Serialized Cap'n Proto capability descriptor used during loading.
    pub fn capabilities(&self) -> &[u8] {
        &self.capabilities
    }

    /// Evaluate one geometry through the persistent session.
    pub fn evaluate(&mut self, request: &ProfileRequest<'_>) -> ProfileResult<ProfileEvaluation> {
        let force_input = encode_force_input(request)?;
        let required =
            unsafe { (self.result_size)(force_input.as_ptr().cast(), force_input.len()) };
        if required == 0 {
            return Err(ProfileError::new(format!(
                "{}_potential_result_size_for_force_input failed: {}",
                self.prefix,
                self.last_error_message()
            )));
        }

        let word_count = required.div_ceil(std::mem::size_of::<u64>());
        let mut output = vec![0u64; word_count];
        let capacity = output.len() * std::mem::size_of::<u64>();
        let mut written = 0usize;
        let status = unsafe {
            (self.calculate)(
                self.lifecycle.session,
                force_input.as_ptr().cast(),
                force_input.len(),
                output.as_mut_ptr().cast(),
                capacity,
                &mut written,
            )
        };
        if status.ok == 0 {
            let message = abi_message(&status).unwrap_or_else(|| self.last_error_message());
            return Err(ProfileError::new(format!(
                "{}_session_calculate_result failed: {message}",
                self.prefix
            )));
        }
        if written == 0 || written > capacity || written % std::mem::size_of::<u64>() != 0 {
            return Err(ProfileError::new(format!(
                "{}_session_calculate_result wrote invalid size {written} for capacity {capacity}",
                self.prefix
            )));
        }
        let encoded = unsafe { std::slice::from_raw_parts(output.as_ptr().cast::<u8>(), written) };
        decode_potential_result(encoded, request.positions.len())
    }

    fn last_error_message(&self) -> String {
        c_string(unsafe { (self.last_error)() })
    }
}

fn open_first(prefix: &str, explicit_path: Option<&str>) -> ProfileResult<(Library, String)> {
    let candidates = library_candidates(prefix, explicit_path);
    let mut failures = Vec::new();
    for candidate in candidates {
        match unsafe { Library::new(&candidate) } {
            Ok(library) => return Ok((library, candidate)),
            Err(error) => failures.push(format!("{candidate}: {error}")),
        }
    }
    Err(ProfileError::new(format!(
        "lib{prefix} not loaded; {}",
        failures.join("; ")
    )))
}

unsafe fn resolve<T: Copy>(library: &Library, prefix: &str, suffix: &str) -> ProfileResult<T> {
    let name = format!("{prefix}_{suffix}\0");
    unsafe { library.get::<T>(name.as_bytes()) }
        .map(|symbol| *symbol)
        .map_err(|error| ProfileError::new(format!("missing {prefix}_{suffix}: {error}")))
}

fn c_string(pointer: *const c_char) -> String {
    if pointer.is_null() {
        return String::new();
    }
    unsafe { CStr::from_ptr(pointer) }
        .to_string_lossy()
        .into_owned()
}

fn abi_message(result: &ProfileAbiResult) -> Option<String> {
    let bytes = result
        .message
        .iter()
        .map(|value| *value as u8)
        .take_while(|value| *value != 0)
        .collect::<Vec<_>>();
    if bytes.is_empty() {
        None
    } else {
        Some(String::from_utf8_lossy(&bytes).into_owned())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
    use std::sync::Arc;

    struct DropProbe(Arc<AtomicBool>);

    impl Drop for DropProbe {
        fn drop(&mut self) {
            self.0.store(true, Ordering::SeqCst);
        }
    }

    #[test]
    fn process_lifetime_handle_keeps_library_mapping() {
        let dropped = Arc::new(AtomicBool::new(false));
        let handle = ProcessLifetime::new(DropProbe(Arc::clone(&dropped)));

        drop(handle);

        assert!(!dropped.load(Ordering::SeqCst));
    }

    static LIFECYCLE_STEP: AtomicUsize = AtomicUsize::new(0);

    unsafe extern "C" fn record_destroy(_session: *mut c_void) {
        assert_eq!(LIFECYCLE_STEP.swap(1, Ordering::SeqCst), 0);
    }

    unsafe extern "C" fn record_finalize() {
        assert_eq!(LIFECYCLE_STEP.swap(2, Ordering::SeqCst), 1);
    }

    #[test]
    fn session_is_destroyed_before_runtime_finalize() {
        LIFECYCLE_STEP.store(0, Ordering::SeqCst);
        let mut token = 0u8;
        let lifecycle = RuntimeLifecycle {
            session: (&mut token as *mut u8).cast(),
            destroy: record_destroy,
            finalize: record_finalize,
        };

        drop(lifecycle);

        assert_eq!(LIFECYCLE_STEP.load(Ordering::SeqCst), 2);
    }
}
