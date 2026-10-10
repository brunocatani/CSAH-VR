from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


class GenerationError(RuntimeError):
    pass


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compile the stereo-safe FO4VR subsurface-scattering pass."
    )
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--fxc", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    return parser.parse_args()


def run(command: list[str], label: str) -> None:
    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    if result.returncode != 0:
        raise GenerationError(
            f"{label} failed: {(result.stdout + result.stderr).strip()}"
        )


def write_header(path: Path, data: bytes) -> None:
    rows = [
        "#pragma once",
        "",
        "inline constexpr unsigned char csah_subsurface_scattering[] = {",
    ]
    for offset in range(0, len(data), 16):
        values = ", ".join(f"0x{value:02x}" for value in data[offset:offset + 16])
        rows.append(f"    {values},")
    rows.extend(("};", ""))
    path.write_text("\n".join(rows), encoding="utf-8")


def main() -> int:
    args = arguments()
    root = args.root.resolve()
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "SubsurfaceScattering"
        / "SubsurfaceScatteringCS.hlsl"
    )
    args.binary.parent.mkdir(parents=True, exist_ok=True)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    assembly = args.binary.with_suffix(".asm.txt")
    run(
        [
            str(args.fxc.resolve()),
            "/nologo",
            "/T",
            "cs_5_0",
            "/E",
            "CSMain",
            "/O3",
            "/Ges",
            "/WX",
            "/Fo",
            str(args.binary),
            "/Fc",
            str(assembly),
            str(source),
        ],
        "subsurface-scattering compute compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_constantbuffer CB0[4], immediateIndexed",
        "dcl_resource_texture2d (float,float,float,float) t0",
        "dcl_resource_texture2d (float,float,float,float) t1",
        "dcl_resource_texture2d (float,float,float,float) t2",
        "dcl_resource_texture2d (float,float,float,float) t3",
        "dcl_uav_typed_texture2d (float,float,float,float) u0",
        "dcl_thread_group 8, 8, 1",
        "round_ne",
        "l(0.381171, 0.094142, 0.001699",
    ):
        if required not in text:
            raise GenerationError(
                "subsurface-scattering assembly changed: " + required
            )
    write_header(args.header, args.binary.read_bytes())
    assembly.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GenerationError, OSError) as error:
        print(f"subsurface-scattering generation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
