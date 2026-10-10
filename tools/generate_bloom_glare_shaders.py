from __future__ import annotations

import argparse
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class ShaderSpec:
    symbol: str
    source: str
    entry: str
    defines: tuple[str, ...] = ()
    required_assembly: tuple[str, ...] = ()


SHADERS = (
    ShaderSpec(
        "kBloomThreshold",
        "BloomCS.hlsl",
        "CS_Threshold",
        required_assembly=(
            "dcl_resource_texture2d",
            "dcl_uav_typed_texture2d",
            "dcl_constantbuffer CB13[2]",
            "dcl_thread_group 8, 8, 1",
        ),
    ),
    ShaderSpec(
        "kBloomDownsample",
        "BloomCS.hlsl",
        "CS_Downsample",
        required_assembly=(
            "dcl_sampler s0, mode_default",
            "dcl_resource_texture2d",
            "dcl_uav_typed_texture2d",
            "dcl_thread_group 8, 8, 1",
        ),
    ),
    ShaderSpec(
        "kBloomUpsample",
        "BloomCS.hlsl",
        "CS_Upsample",
        required_assembly=(
            "dcl_sampler s0, mode_default",
            "dcl_resource_texture2d",
            "dcl_uav_typed_texture2d",
            "dcl_thread_group 8, 8, 1",
        ),
    ),
    ShaderSpec(
        "kGlareThreshold",
        "GlareThresholdCS.hlsl",
        "CS_Threshold",
        required_assembly=(
            "dcl_resource_texture2d",
            "dcl_uav_typed_texture2d",
            "dcl_constantbuffer CB13[4]",
            "dcl_thread_group 8, 8, 1",
        ),
    ),
    ShaderSpec(
        "kGlareAperture",
        "GlareApertureCS.hlsl",
        "CS_Aperture",
        required_assembly=(
            "dcl_uav_typed_texture2d",
            "dcl_constantbuffer CB13[6]",
            "dcl_thread_group 8, 8, 1",
        ),
    ),
    ShaderSpec(
        "kGlarePsfRed",
        "GlarePsfCS.hlsl",
        "CS_Psf",
        ("GLARE_CHANNEL=0",),
        (
            "dcl_sampler s0, mode_default",
            "dcl_resource_texture2d",
            "dcl_uav_typed_texture2d",
            "dcl_thread_group 8, 8, 1",
        ),
    ),
    ShaderSpec(
        "kGlarePsfGreen",
        "GlarePsfCS.hlsl",
        "CS_Psf",
        ("GLARE_CHANNEL=1",),
        (
            "dcl_sampler s0, mode_default",
            "dcl_resource_texture2d",
            "dcl_uav_typed_texture2d",
            "dcl_thread_group 8, 8, 1",
        ),
    ),
    ShaderSpec(
        "kGlarePsfBlue",
        "GlarePsfCS.hlsl",
        "CS_Psf",
        ("GLARE_CHANNEL=2",),
        (
            "dcl_sampler s0, mode_default",
            "dcl_resource_texture2d",
            "dcl_uav_typed_texture2d",
            "dcl_thread_group 8, 8, 1",
        ),
    ),
    ShaderSpec(
        "kGlareFftRowForward",
        "GlareFftCS.hlsl",
        "CS_Fft",
        ("ROW_PASS=1", "FORWARD=1"),
        (
            "dcl_tgsm_structured g0, 8, 512",
            "dcl_tgsm_structured g1, 8, 512",
            "dcl_thread_group 512, 1, 1",
        ),
    ),
    ShaderSpec(
        "kGlareFftColumnForward",
        "GlareFftCS.hlsl",
        "CS_Fft",
        ("COL_PASS=1", "FORWARD=1"),
        (
            "dcl_tgsm_structured g0, 8, 512",
            "dcl_tgsm_structured g1, 8, 512",
            "dcl_thread_group 512, 1, 1",
        ),
    ),
    ShaderSpec(
        "kGlareConvolveRowInverse",
        "GlareFftCS.hlsl",
        "CS_Fft",
        ("ROW_PASS=1", "INVERSE=1", "MULTIPLY_PSF=1"),
        (
            "dcl_tgsm_structured g0, 8, 512",
            "dcl_tgsm_structured g1, 8, 512",
            "dcl_thread_group 512, 1, 1",
        ),
    ),
    ShaderSpec(
        "kGlareFftColumnInverse",
        "GlareFftCS.hlsl",
        "CS_Fft",
        ("COL_PASS=1", "INVERSE=1"),
        (
            "dcl_tgsm_structured g0, 8, 512",
            "dcl_tgsm_structured g1, 8, 512",
            "dcl_thread_group 512, 1, 1",
        ),
    ),
    ShaderSpec(
        "kGlareComposite",
        "GlareCompositeCS.hlsl",
        "CS_Composite",
        required_assembly=(
            "dcl_sampler s0, mode_default",
            "dcl_resource_texture2d",
            "dcl_uav_typed_texture2d",
            "dcl_thread_group 8, 8, 1",
        ),
    ),
)


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compile FO4VR stereo bloom and physical-glare shaders."
    )
    parser.add_argument("--fxc", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
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
        raise RuntimeError(
            f"{label} failed: {(result.stdout + result.stderr).strip()}"
        )


def compile_shader(
    fxc: Path,
    source_dir: Path,
    temporary: Path,
    spec: ShaderSpec,
) -> bytes:
    output = temporary / f"{spec.symbol}.dxbc"
    assembly = temporary / f"{spec.symbol}.asm.txt"
    command = [
        str(fxc),
        "/nologo",
        "/T",
        "cs_5_0",
        "/E",
        spec.entry,
        "/O3",
        "/Ges",
        "/WX",
        "/I",
        str(source_dir),
    ]
    command.extend(f"/D{define}" for define in spec.defines)
    command.extend(
        (
            "/Fo",
            str(output),
            "/Fc",
            str(assembly),
            str(source_dir / spec.source),
        )
    )
    run(command, spec.symbol)
    assembly_text = assembly.read_text(encoding="utf-8")
    for contract in spec.required_assembly:
        if contract not in assembly_text:
            raise RuntimeError(
                f"{spec.symbol} assembly contract changed: {contract}"
            )
    if "ret " not in assembly_text:
        raise RuntimeError(f"{spec.symbol} does not terminate")
    return output.read_bytes()


def write_header(path: Path, shaders: list[tuple[ShaderSpec, bytes]]) -> None:
    rows = [
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstdint>",
        "",
        "namespace csah::bloom_glare::generated",
        "{",
    ]
    for spec, data in shaders:
        rows.append(
            f"    inline constexpr std::array<std::uint8_t, {len(data)}> "
            f"{spec.symbol}{{{{"
        )
        for offset in range(0, len(data), 16):
            values = ", ".join(
                f"0x{value:02x}" for value in data[offset : offset + 16]
            )
            rows.append(f"        {values},")
        rows.extend(("    }};", ""))
    rows.extend(("}", ""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(rows), encoding="utf-8")


def main() -> int:
    args = arguments()
    with tempfile.TemporaryDirectory(prefix="fo4vr-bloom-glare-") as folder:
        temporary = Path(folder)
        compiled = [
            (
                spec,
                compile_shader(
                    args.fxc.resolve(),
                    args.source_dir.resolve(),
                    temporary,
                    spec,
                ),
            )
            for spec in SHADERS
        ]
    write_header(args.header.resolve(), compiled)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
