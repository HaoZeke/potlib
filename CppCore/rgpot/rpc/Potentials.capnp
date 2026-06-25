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
# @brief NWChem-specific knobs (one backend arm inside PotentialConfig / rgpot params).
#
# Not a standalone "rgpot config language" — only used when the active potential
# is NWChem (or configure targets that backend). Same fields in/out via Cap'n Proto.
struct NWChemDirective {
  keyword @0 :Text;       # Directive keyword inside a block, e.g. "convergence".
  args    @1 :List(Text); # Tokenized directive arguments.
}

struct NWChemGenericStanza {
  name       @0 :Text;                   # NWChem block name, e.g. "driver".
  directives @1 :List(NWChemDirective);  # Structured block body.
}

struct NWChemSetDirective {
  key   @0 :Text; # RTDB key, e.g. "dft:avg_fon".
  value @1 :Text; # NWChem input literal, e.g. ".false.".
}

struct NWChemDftSmearing {
  sigmaHartree @0 :Float64 = 0.0; # Molecular DFT smear sigma in Hartree.
  mode         @1 :Mode = fixsz;

  enum Mode {
    fixsz   @0;
    nofixsz @1;
  }
}

struct NWChemDftStanza {
  xc         @0 :Text = "";                  # Exchange-correlation keyword.
  direct     @1 :Bool = false;               # Emit "direct".
  smearing   @2 :NWChemDftSmearing;          # Emit "smear ...".
  directives @3 :List(NWChemDirective);      # Extra structured DFT directives.
}

struct NWChemInputStanza {
  kind    @0 :Kind = generic;
  generic @1 :NWChemGenericStanza;
  dft     @2 :NWChemDftStanza;
  set     @3 :NWChemSetDirective;
  raw     @4 :Text;

  enum Kind {
    generic @0;
    dft     @1;
    set     @2;
    raw     @3;
  }
}

struct NWChemParams {
  basis        @0 :Text = "sto-3g";  # Gaussian basis (sto-3g, 6-31g*, ...).
  theory       @1 :Text = "scf";     # scf | dft | blyp | b3lyp | ...
  scfType      @2 :Text = "rhf";     # HF: rhf/uhf; DFT: xc (blyp, b3lyp, ...).
  charge       @3 :Int32 = 0;
  multiplicity @4 :Int32 = 1;        # 2S+1.
  enginePath   @5 :Text = "";        # libnwchem_engine.so (dlopen); empty => probe/env.
  nwchemRoot   @6 :Text = "";        # NWCHEM_TOP for embed; empty => env.
  task         @7 :Text = "gradient"; # energy | gradient | property; frontend usually calls gradient.
  title        @8 :Text = "";         # Optional NWChem title/start prefix.
  memoryMb     @9 :UInt32 = 0;        # 0 => NWChem defaults / environment.
  scratchDir   @10 :Text = "";        # Optional NWChem scratch directory.
  permanentDir @11 :Text = "";        # Optional NWChem permanent directory.
  inputBlocks  @12 :List(Text);       # Raw NWChem directive blocks applied before task.
  inputStanzas @13 :List(NWChemInputStanza); # Structured NWChem input stanzas.
}

# Future backend option structs (extend here, then add a PotentialConfig union arm):
#   struct XTBParams { method @0 :Text = "GFN2-xTB"; ... }
#   struct TBLiteParams { method @0 :Text = "GFN2-xTB"; ... }
#   struct MetatomicParams { modelPath @0 :Text; device @1 :Text = "cpu"; ... }

# @struct PotentialConfig
# @brief **rgpot user parameters (extensible, in/out via Cap'n Proto only).**
#
# This is the single user-facing options carrier for rgpot: pass in to configure
# a live Potential (RPC `configure` or in-process apply), and/or round-trip out
# when a backend supports get. One schema for wire + embed — no parallel
# TOML/JSON/YAML option files for backends.
#
# Tagged union: exactly one backend's options (or none). Add new arms as new
# potentials gain runtime knobs (e.g. metatomic @2 :MetatomicParams).
# `calculate` geometry stays on ForceInput; this struct is method/backend setup only.
struct PotentialConfig {
  union {
    none      @0 :Void;         # No backend-specific options (or no-op configure).
    nwchem    @1 :NWChemParams; # NWChemPot / potserv … NWChem
    # metatomic @2 :MetatomicParams;  # reserved pattern for later
    # xtb       @3 :XTBParams;
    # tblite    @4 :TBLiteParams;
  }
}

# @interface Potential
# @brief The RPC interface for remote calculations.
interface Potential {
  # @brief Executes the potential and force calculation.
  # @param fip The input atomic configuration.
  # @return The resulting energy and force vector.
  calculate @0 (fip :ForceInput) -> (result :PotentialResult);

  # @brief Apply rgpot user parameters (PotentialConfig) before calculate().
  # @param config Backend-tagged options (nwchem, future metatomic/xtb/…).
  # @return ok=false if the arm does not match the server backend or apply failed.
  configure @1 (config :PotentialConfig) -> (ok :Bool, message :Text);
}
