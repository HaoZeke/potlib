#!/usr/bin/env python3
import asyncio
import argparse
import subprocess
import sys
import os
import capnp
import numpy as np

# Load schema relative to this script
SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
SCHEMA_PATH = os.path.join(SCRIPT_DIR, "../CppCore/rgpot/rpc/Potentials.capnp")
pot_capnp = capnp.load(SCHEMA_PATH)

try:
    from cpmd_params import configure_cpmd
    from nwchem_params import configure_nwchem, make_potential_config_none
except ImportError:
    sys.path.insert(0, SCRIPT_DIR)
    from cpmd_params import configure_cpmd
    from nwchem_params import configure_nwchem, make_potential_config_none


async def run_client(port):
    # Retry connection a few times to allow server startup
    for _ in range(10):
        try:
            # Create connection (this requires the KJ loop to be active)
            connection = await capnp.AsyncIoStream.create_connection(
                host="localhost", port=port
            )
            break
        except OSError:
            await asyncio.sleep(0.5)
    else:
        print("Failed to connect to server.")
        return False

    client = capnp.TwoPartyClient(connection)
    pot = client.bootstrap().cast_as(pot_capnp.Potential)

    # Test Case: CuH2 minimal system
    fip = pot_capnp.ForceInput.new_message()

    # Cu at 0,0,0 and H at 1.5, 0, 0 (Approx distance)
    # Using semantic types from your "Pro" schema if you applied it,
    # but based on your recent "simple" checks, we'll stick to the flat list
    # unless you updated the schema.
    # Assuming you kept the struct/flat hybrid from previous steps:
    pos_data = [0.0, 0.0, 0.0, 1.5, 0.0, 0.0]
    fip.init("pos", len(pos_data))
    for i, p in enumerate(pos_data):
        fip.pos[i] = p

    fip.init("atmnrs", 2)
    fip.atmnrs[0] = 29  # Cu
    fip.atmnrs[1] = 1  # H

    box_data = [10.0, 0.0, 0.0, 0.0, 10.0, 0.0, 0.0, 0.0, 10.0]
    fip.init("box", len(box_data))
    for i, b in enumerate(box_data):
        fip.box[i] = b

    print("Sending calculation request...")
    # This await requires the KJ loop
    result = await pot.calculate(fip)

    print(f"Received Energy: {result.result.energy}")
    print(f"Received Forces: {list(result.result.forces)}")
    assert result.result.energy == -0.67880756881223303
    assert result.result.forces[0] == -7.556524918281001
    assert result.result.forces[3] == 7.556524918281001

    # Basic physical sanity check
    if result.result.energy == 0.0 or np.isnan(result.result.energy):
        print("Error: Energy is zero or NaN")
        return False

    return True


async def run_nwchem_smoke(port):
    """Optional smoke: start NWChem server, configure(), skip calculate if no engine."""
    for _ in range(10):
        try:
            connection = await capnp.AsyncIoStream.create_connection(
                host="localhost", port=port
            )
            break
        except OSError:
            await asyncio.sleep(0.5)
    else:
        print("NWChem smoke: failed to connect to server.")
        return False

    client = capnp.TwoPartyClient(connection)
    pot = client.bootstrap().cast_as(pot_capnp.Potential)

    # configure(none) should succeed
    cfg_none = make_potential_config_none(pot_capnp)
    r0 = await pot.configure(cfg_none)
    print(f"NWChem configure(none): ok={r0.ok} msg={r0.message}")

    ok, msg = await configure_nwchem(
        pot, pot_capnp, basis="sto-3g", theory="scf", scf_type="rhf"
    )
    print(f"NWChem configure(nwchem): ok={ok} msg={msg}")
    # ok may be False without engine; still a valid RPC round-trip
    print("NWChem smoke: configure RPC completed.")
    return True


async def configure_cpmd_smoke(pot, pot_capnp):
    """Configure a CPMD backend over RPC; calculate may require an engine."""
    cfg_none = make_potential_config_none(pot_capnp)
    r0 = await pot.configure(cfg_none)
    print(f"CPMD configure(none): ok={r0.ok} msg={r0.message}")

    ok, msg = await configure_cpmd(pot, pot_capnp, functional="BLYP", task="gradient")
    print(f"CPMD configure(cpmd): ok={ok} msg={msg}")
    ok_sections, msg_sections = await configure_cpmd(
        pot,
        pot_capnp,
        functional="PBE0",
        task="gradient",
        input_sections=[
            {
                "kind": "generic",
                "name": "PIMD",
                "directives": [{"keyword": "TEMP", "args": ["300"]}],
            },
            {
                "kind": "cpmd",
                "optimizeGeometry": True,
                "molecularDynamics": True,
                "convergenceGeometry": 1.0e-4,
                "maxStep": 8,
                "maxIter": 12,
                "electronMass": 450.0,
                "directives": [{"keyword": "PRINT", "args": ["FORCES", "ON"]}],
            },
            {
                "kind": "dft",
                "functional": "PBE0",
                "lsd": True,
                "gcCutoff": 1.0e-8,
                "xcDriver": "LIBXC",
                "libxc": "GGA_X_PBE GGA_C_PBE",
                "lrKernel": "PBE",
                "refunct": "PBE",
                "mtsHighFunc": "PBE0",
                "mtsLowFunc": "PBE",
                "hfx": True,
                "hfxScreening": "0.2",
                "hubbard": "U 1 4.0",
                "alpha": 0.25,
                "beta": 0.75,
                "oldCode": True,
                "newCode": True,
                "correlation": "LYP",
                "exchange": "B88",
                "becke88": True,
                "directives": [{"keyword": "HFX", "args": ["SCREENING"]}],
            },
            {"kind": "raw", "text": "&VDW\n  DISPERSION\n&END"},
        ],
    )
    print(f"CPMD configure(cpmd sections): ok={ok_sections} msg={msg_sections}")
    print("CPMD smoke: configure RPC completed.")
    return True


async def run_cpmd_smoke(port):
    """Optional smoke: start CPMD server, configure(), skip calculate if no engine."""
    for _ in range(10):
        try:
            connection = await capnp.AsyncIoStream.create_connection(
                host="localhost", port=port
            )
            break
        except OSError:
            await asyncio.sleep(0.5)
    else:
        print("CPMD smoke: failed to connect to server.")
        return False

    client = capnp.TwoPartyClient(connection)
    pot = client.bootstrap().cast_as(pot_capnp.Potential)
    return await configure_cpmd_smoke(pot, pot_capnp)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--server-bin", required=True, help="Path to potserv executable"
    )
    parser.add_argument("--port", type=int, default=12345)
    parser.add_argument(
        "--nwchem-smoke",
        action="store_true",
        help="Also run NWChem configure() smoke on a second server port",
    )
    parser.add_argument(
        "--cpmd-smoke",
        action="store_true",
        help="Also run CPMD configure() smoke on a separate server port",
    )
    args = parser.parse_args()

    # 1. Start Server
    print(f"Starting server: {args.server_bin} {args.port} CuH2")
    server_proc = subprocess.Popen(
        [args.server_bin, str(args.port), "CuH2"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    try:
        #  Run Client
        success = asyncio.run(capnp.run(run_client(args.port)))
    except Exception as e:
        print(f"Exception during test: {e}")
        # Print server stderr to help debug
        print("Server stderr:")
        print(server_proc.stderr.read().decode())
        success = False
    finally:
        # Cleanup
        print("Killing server...")
        server_proc.kill()
        server_proc.wait()

    if not success:
        sys.exit(1)
    print("Integration test passed.")

    if args.nwchem_smoke:
        nw_port = args.port + 1
        print(f"Starting NWChem smoke server: {args.server_bin} {nw_port} NWChem")
        nw_proc = subprocess.Popen(
            [args.server_bin, str(nw_port), "NWChem"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            nw_ok = asyncio.run(capnp.run(run_nwchem_smoke(nw_port)))
        except Exception as e:
            print(f"NWChem smoke exception: {e}")
            print("Server stderr:")
            print(nw_proc.stderr.read().decode())
            nw_ok = False
        finally:
            nw_proc.kill()
            nw_proc.wait()
        if not nw_ok:
            sys.exit(1)
        print("NWChem smoke passed.")

    if args.cpmd_smoke:
        cp_port = args.port + 1 + int(args.nwchem_smoke)
        print(f"Starting CPMD smoke server: {args.server_bin} {cp_port} CPMD")
        cp_proc = subprocess.Popen(
            [args.server_bin, str(cp_port), "CPMD"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            cp_ok = asyncio.run(capnp.run(run_cpmd_smoke(cp_port)))
        except Exception as e:
            print(f"CPMD smoke exception: {e}")
            print("Server stderr:")
            print(cp_proc.stderr.read().decode())
            cp_ok = False
        finally:
            cp_proc.kill()
            cp_proc.wait()
        if not cp_ok:
            sys.exit(1)
        print("CPMD smoke passed.")


if __name__ == "__main__":
    main()
