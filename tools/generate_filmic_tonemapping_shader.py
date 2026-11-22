from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

import census_linear_lighting_fxp as census


EXPECTED_CONTRACTS = {
    "base": (
        "ImageSpace[026]",
        (1772, "83b514dd63eea60d84769343d576d4fa"),
        "dcl_constantbuffer CB2[5], immediateIndexed",
    ),
    "fade": (
        "ImageSpace[027]",
        (1848, "f1bfa042d52062c4aedfd612bbf4799d"),
        "dcl_constantbuffer CB2[6], immediateIndexed",
    ),
}


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compile the exact FO4VR HDR blend filmic replacement."
    )
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--fxc", type=Path, required=True)
    parser.add_argument("--base-binary", type=Path, required=True)
    parser.add_argument("--fade-binary", type=Path, required=True)
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


def verify_native_contract(root: Path) -> None:
    inventory = census.parse_fxp((root / "Shaders012_VR.fxp").read_bytes())
    for name, (family, identity, _) in EXPECTED_CONTRACTS.items():
        matches = [
            item
            for item in inventory.containers
            if item.family == family
            and item.stage == "PS"
            and item.key == 0
            and item.identity == identity
        ]
        all_identity_matches = [
            item for item in inventory.containers if item.identity == identity
        ]
        if len(matches) != 1 or len(all_identity_matches) != 1:
            found = [
                (item.family, item.stage, item.key, item.identity)
                for item in all_identity_matches
            ]
            raise RuntimeError(
                f"FO4VR HDR tonemap {name} identity is no longer unique at "
                f"{family}: {found}"
            )


def format_array(name: str, data: bytes) -> str:
    rows = [
        "        "
        + ", ".join(f"0x{value:02X}" for value in data[offset : offset + 12])
        + ","
        for offset in range(0, len(data), 12)
    ]
    return (
        f"    inline constexpr std::array<std::uint8_t, {len(data)}> {name}{{\n"
        + "\n".join(rows)
        + "\n    };\n"
    )


def write_header(path: Path, base_data: bytes, fade_data: bytes) -> None:
    text = (
        "#pragma once\n\n"
        "#include <array>\n"
        "#include <cstdint>\n\n"
        "namespace csah::filmic_tonemapping::generated\n"
        "{\n"
        + format_array("kBasePixelShader", base_data)
        + format_array("kFadePixelShader", fade_data)
        + "}\n"
    )
    path.write_text(text, encoding="utf-8", newline="\n")


def main() -> int:
    args = arguments()
    verify_native_contract(args.root)
    source = (
        args.root
        / "package"
        / "Shaders"
        / "Community"
        / "FilmicTonemapping"
        / "FilmicTonemappingPS.hlsl"
    )
    args.base_binary.parent.mkdir(parents=True, exist_ok=True)
    args.fade_binary.parent.mkdir(parents=True, exist_ok=True)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    compiled: dict[str, bytes] = {}
    for name, (_, _, native_cb_declaration) in EXPECTED_CONTRACTS.items():
        binary = args.base_binary if name == "base" else args.fade_binary
        with tempfile.TemporaryDirectory(prefix=f"fo4vr-filmic-{name}-") as temporary:
            assembly = Path(temporary) / "FilmicTonemappingPS.asm.txt"
            fade_define = "0" if name == "base" else "1"
            run(
                [
                    str(args.fxc),
                    "/nologo",
                    "/T",
                    "ps_5_0",
                    "/E",
                    "PSMain",
                    "/O3",
                    "/Ges",
                    "/WX",
                    "/D",
                    f"FO4VR_FILMIC_FADE={fade_define}",
                    "/Fo",
                    str(binary),
                    "/Fc",
                    str(assembly),
                    str(source),
                ],
                f"filmic tonemap {name} compilation",
            )
            disassembly = assembly.read_text(encoding="utf-8")
        for required in (
            native_cb_declaration,
            "dcl_constantbuffer CB12[7], immediateIndexed",
            "dcl_constantbuffer CB13[1], immediateIndexed",
            "dcl_sampler s0, mode_default",
            "dcl_sampler s1, mode_default",
            "dcl_sampler s2, mode_default",
            "dcl_sampler s3, mode_default",
            "dcl_sampler s4, mode_default",
            "dcl_sampler s5, mode_default",
            "dcl_resource_texture3d (float,float,float,float) t6",
            "dcl_resource_texture3d (float,float,float,float) t7",
            "dcl_resource_texture2d (float,float,float,float) t0",
            "dcl_resource_texture2d (float,float,float,float) t1",
            "dcl_resource_texture2d (float,float,float,float) t2",
            "dcl_resource_texture2d (float,float,float,float) t3",
            "dcl_resource_texture2d (float,float,float,float) t4",
            "dcl_resource_texture2d (float,float,float,float) t5",
            "dcl_input_ps linear v1.xy",
            "dcl_output o0.xyzw",
        ):
            if required not in disassembly:
                raise RuntimeError(
                    f"compiled filmic {name} shader changed its native linkage "
                    f"contract: {required}"
                )
        data = binary.read_bytes()
        if data[:4] != b"DXBC":
            raise RuntimeError(f"compiled filmic {name} shader is not DXBC")
        compiled[name] = data
    write_header(args.header, compiled["base"], compiled["fade"])
    print(
        f"generated {len(compiled['base'])}-byte base and "
        f"{len(compiled['fade'])}-byte fade filmic shaders for the exact "
        "FO4VR HDR output family"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
