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
    for schema_path, lines in ((cpp_schema, cpp_lines), (rust_schema, rust_lines)):
        rel = schema_path.relative_to(root)
        if "enum CPMDSectionKind {" not in lines:
            raise AssertionError(f"{rel} must expose CPMDSectionKind")
        text = "\n".join(lines)
        for needle in (
            "struct ExprTerm {",
            "struct ExprParams {",
            "struct PotSpec {",
            "pot  @1 :PotSpec;",
            "terms      @1 :List(ExprTerm);",
            "expr @0 :ExprParams;",
            "none @1 :Void;",
        ):
            if needle not in text:
                raise AssertionError(f"{rel} must expose recursive ExprParams/PotSpec ({needle})")
        start = text.find("struct PotentialConfig {")
        if start < 0:
            raise AssertionError(f"{rel} must expose PotentialConfig")
        end = text.find("\n}", start)
        union = text[start:end]
        for arm in ("expr", "sum", "list"):
            if f"{arm} " in union or f"{arm}\t" in union:
                raise AssertionError(
                    f"{rel} PotentialConfig must not grow a {arm} arm"
                )

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
