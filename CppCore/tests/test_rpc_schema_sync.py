#!/usr/bin/env python3
"""Contract check for rgpot RPC schema copies."""

from __future__ import annotations

import difflib
import sys
from pathlib import Path


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.cwd()
    cpp_schema = root / "CppCore" / "rgpot" / "rpc" / "Potentials.capnp"
    rust_schema = root / "rgpot-core" / "schema" / "Potentials.capnp"

    cpp_lines = cpp_schema.read_text(encoding="utf-8").splitlines()
    rust_lines = rust_schema.read_text(encoding="utf-8").splitlines()
    schema_id = "@0xbd1f89fa17369103;"
    for schema_path, lines in ((cpp_schema, cpp_lines), (rust_schema, rust_lines)):
        if "enum CPMDSectionKind {" not in lines:
            rel = schema_path.relative_to(root)
            raise AssertionError(f"{rel} must expose CPMDSectionKind")
        if schema_id not in lines:
            rel = schema_path.relative_to(root)
            raise AssertionError(f"{rel} has the wrong Cap'n Proto schema identity")

    if cpp_lines == rust_lines:
        return 0

    diff = difflib.unified_diff(
        rust_lines,
        cpp_lines,
        fromfile=str(rust_schema.relative_to(root)),
        tofile=str(cpp_schema.relative_to(root)),
        lineterm="",
        n=3,
    )
    print("\n".join(list(diff)[:120]))
    raise AssertionError("rgpot-core bundled schema must match C++ RPC schema")


if __name__ == "__main__":
    raise SystemExit(main())
