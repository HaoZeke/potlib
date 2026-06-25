# @brief RPC schema for distributed potential evaluations.
#
# This schema defines the binary communication contract between the light client
# and the RPC server components. The design derives from the C-style structures
# in the eOn [1] project (at the v4 writeup).
#
# # References
# [1] eOn Development Team. eOn Documentation. https://eondocs.org.

@0xbd1f89fa17369103;

# @struct ForceInput
# @brief Input configuration for a potential energy evaluation.
# @field lengthUnit Unit expression for positions/box (default "angstrom").
# @field energyUnit Unit expression for energy output (default "eV").
# Unit strings are parsed by rgpot::units::unit_conversion_factor().
# Examples: "angstrom", "bohr", "eV", "hartree", "kJ/mol", "kcal/mol".
struct ForceInput {
  pos        @0 :List(Float64); # @brief Flat array of atomic coordinates [natoms * 3].
  atmnrs     @1 :List(Int32);   # @brief Array of atomic numbers [natoms].
  box        @2 :List(Float64); # @brief Simulation cell vectors [9] (row-major 3x3).
  lengthUnit @3 :Text = "angstrom"; # @brief Unit for positions and box vectors.
  energyUnit @4 :Text = "eV";       # @brief Unit for energy and forces output.
}

# @struct PotentialResult
# @brief Results returned from a potential energy evaluation.
struct PotentialResult {
  energy @0 :Float64;       # @brief The calculated potential energy.
  forces @1 :List(Float64); # @brief Flat array of atomic forces [natoms * 3].
}

# @struct NWChemParams
# @brief NWChem-specific knobs (one arm of PotentialConfig / rgpot params).
struct NWChemParams {
  basis        @0 :Text = "sto-3g";
  theory       @1 :Text = "scf";
  scfType      @2 :Text = "rhf";
  charge       @3 :Int32 = 0;
  multiplicity @4 :Int32 = 1;
  enginePath   @5 :Text = "";
  nwchemRoot   @6 :Text = "";
}

# @struct PotentialConfig
# @brief **rgpot user parameters** — extensible union of backend options (Cap'n Proto only).
# NWChem is one arm; add MetatomicParams / XTBParams / … as other potentials need knobs.
struct PotentialConfig {
  union {
    none   @0 :Void;
    nwchem @1 :NWChemParams;
  }
}

# @interface Potential
# @brief The RPC interface for remote calculations.
interface Potential {
  # @brief Executes the potential and force calculation.
  # @param fip The input atomic configuration.
  # @return The resulting energy and force vector.
  calculate @0 (fip :ForceInput) -> (result :PotentialResult);

  # @brief Apply backend-specific configuration before calculate().
  # @param config Tagged parameters (e.g. NWChemParams).
  # @return ok=false if the backend rejects or cannot apply config.
  configure @1 (config :PotentialConfig) -> (ok :Bool, message :Text);
}
