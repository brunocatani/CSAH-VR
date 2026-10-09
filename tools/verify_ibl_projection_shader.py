from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

import census_linear_lighting_fxp as shader_tools


class VerificationError(RuntimeError):
    pass


SHADERS = (
    ("DiffuseIblProjectionCS.hlsl", "DiffuseIblProjectionCS.dxbc"),
    ("UpdateEnvironmentCS.hlsl", "UpdateEnvironmentCS.dxbc"),
    ("UpdateCompactEnvironmentCS.hlsl", "UpdateCompactEnvironmentCS.dxbc"),
    ("FilterEnvironmentCS.hlsl", "FilterEnvironmentCS.dxbc"),
    ("ReduceEnvironmentCS.hlsl", "ReduceEnvironmentCS.dxbc"),
    ("MaterialStyleCS.hlsl", "MaterialStyleCS.dxbc"),
)


def compile_shader(root: Path, source_name: str, asset_name: str) -> bytes:
    source = root / "package" / "Shaders" / "Community" / "IBL" / source_name
    if not source.is_file():
        raise VerificationError(f"IBL compute source is missing: {source}")
    fxc = shader_tools.find_fxc(None)
    with tempfile.TemporaryDirectory(prefix="fo4vr_ibl_projection_") as temporary:
        output = Path(temporary) / asset_name
        completed = subprocess.run(
            [
                str(fxc),
                "/nologo",
                "/T",
                "cs_5_0",
                "/E",
                "main",
                "/O3",
                "/Ges",
                "/WX",
                "/Fo",
                str(output),
                str(source),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        if completed.returncode != 0:
            details = (completed.stdout + completed.stderr).strip()
            raise VerificationError(f"fxc failed for {source_name}: {details}")
        bytecode = output.read_bytes()
    if len(bytecode) < 20 or bytecode[:4] != b"DXBC":
        raise VerificationError("fxc did not produce a valid DXBC container")
    return bytecode


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Rebuild and verify the FO4VR diffuse IBL projection shader."
    )
    parser.add_argument("--root", type=Path, required=True)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--check", action="store_true")
    action.add_argument("--write", action="store_true")
    arguments = parser.parse_args()

    try:
        root = arguments.root.resolve()
        shader_directory = root / "package" / "Shaders" / "Community" / "IBL"
        for source_name, asset_name in SHADERS:
            asset = shader_directory / asset_name
            generated = compile_shader(root, source_name, asset_name)
            if arguments.write:
                asset.parent.mkdir(parents=True, exist_ok=True)
                asset.write_bytes(generated)
                print(f"wrote {asset}")
                continue
            if not asset.is_file():
                raise VerificationError(f"IBL compute asset is missing: {asset}")
            if asset.read_bytes() != generated:
                raise VerificationError(
                    f"{asset_name} is stale; run this tool with --write"
                )
            print(f"{asset_name} matches its HLSL source")
        return 0
    except (OSError, VerificationError, shader_tools.CensusError) as error:
        print(f"FAILED: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
