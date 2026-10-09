from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from dxbc_transform import (
    OPCODE_DCL_CONSTANT_BUFFER,
    OPCODE_DCL_RESOURCE,
    OPCODE_DCL_SAMPLER,
    OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW,
    OPCODE_RET,
    OPERAND_CONSTANT_BUFFER,
    OPERAND_INPUT,
    OPERAND_OUTPUT,
    OPERAND_RESOURCE,
    OPERAND_SAMPLER,
    OPERAND_TEMP,
    OPERAND_UNORDERED_ACCESS_VIEW,
    DxbcChunk,
    TransformError as ContractError,
    build_dxbc,
    executable_operands,
    flatten_operands,
    instructions,
    opcode_extension_end,
    pack_words,
    parse_operand,
    replace_operand_with_temp,
    shader_declarations_and_body,
    shader_words,
)


SKYLIGHTING_CONSTANT_SLOT = 13
SKYLIGHTING_PROBE_SLOT = 50
SKYLIGHTING_FAR_PROBE_SLOT = 51
SKYLIGHTING_DIAGNOSTIC_SLOT = 7
REQUIRED_NATIVE_RESOURCE_SLOTS = {1, 2, 3}
REQUIRED_NATIVE_CONSTANT_SLOTS = {2, 8, 12}
REQUIRED_NATIVE_SAMPLER_SLOTS = {1, 2, 3}
MUL_OPCODE = 0x38
MOV_OPCODE = 0x36
VANILLA_GAMMA = 2.2
EXPECTED_GAMMA_FLOATS = 6


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Generate FO4VR Skylighting probe-update and ambient shader "
            "contracts."
        )
    )
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--fxc", type=Path, required=True)
    parser.add_argument("--ambient-header", type=Path, required=True)
    parser.add_argument("--compute-binary", type=Path, required=True)
    parser.add_argument("--compute-header", type=Path, required=True)
    parser.add_argument("--sun-compute-binary", type=Path, required=True)
    parser.add_argument("--sun-compute-header", type=Path, required=True)
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
        raise ContractError(
            f"{label} failed: {(result.stdout + result.stderr).strip()}"
        )


def compile_shader(
    fxc: Path,
    source: Path,
    target: str,
    entry: str,
    output: Path,
    assembly: Path,
    label: str,
) -> tuple[bytes, str]:
    run(
        [
            str(fxc),
            "/nologo",
            "/T",
            target,
            "/E",
            entry,
            "/O3",
            "/Ges",
            "/WX",
            *(["/Gis"] if source.name == "UpdateSunProbesCS.hlsl" else []),
            "/Fo",
            str(output),
            "/Fc",
            str(assembly),
            str(source),
        ],
        label,
    )
    return (
        output.read_bytes(),
        assembly.read_text(encoding="utf-8"),
    )


def replace_instruction_operands(
    instruction: list[int], replacements: dict[int, list[int]]
) -> list[int]:
    operands = {
        operand.start: operand
        for operand in flatten_operands(
            executable_operands(instruction, 0, len(instruction))
        )
    }
    result: list[int] = []
    cursor = 0
    while cursor < len(instruction):
        replacement = replacements.get(cursor)
        if replacement is None:
            result.append(instruction[cursor])
            cursor += 1
            continue
        operand = operands.get(cursor)
        if operand is None:
            raise ContractError("Skylighting operand replacement is invalid")
        result.extend(replacement)
        cursor = operand.end
    result[0] = (result[0] & ~(0x7F << 24)) | (len(result) << 24)
    return result


def declaration_contract(
    template: bytes,
) -> tuple[list[list[int]], list[list[int]], int]:
    _, _, _, words = shader_words(template)
    constant_declaration: list[int] | None = None
    resource_declaration: list[int] | None = None
    far_resource_declaration: list[int] | None = None
    diagnostic_declaration: list[int] | None = None
    gi_declarations = []
    for start, end in instructions(words):
        opcode = words[start] & 0x7FF
        operands = executable_operands(words, start, end)
        if (
            opcode == OPCODE_DCL_CONSTANT_BUFFER
            and operands
            and operands[0].operand_type == OPERAND_CONSTANT_BUFFER
            and operands[0].immediate_indices
            and operands[0].immediate_indices[0]
            == SKYLIGHTING_CONSTANT_SLOT
        ):
            constant_declaration = words[start:end]
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (SKYLIGHTING_PROBE_SLOT,)
        ):
            resource_declaration = words[start:end]
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices ==
                (SKYLIGHTING_FAR_PROBE_SLOT,)
        ):
            far_resource_declaration = words[start:end]
        if opcode == OPCODE_DCL_RESOURCE and operands and operands[0].immediate_indices[0] in range(57,65):
            gi_declarations.append(words[start:end])
        if (
            opcode == OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW
            and operands
            and operands[0].operand_type == OPERAND_UNORDERED_ACCESS_VIEW
            and operands[0].immediate_indices
            and operands[0].immediate_indices[0]
            == SKYLIGHTING_DIAGNOSTIC_SLOT
        ):
            diagnostic_declaration = words[start:end]
    if constant_declaration is None:
        raise ContractError("Skylighting template no longer declares b13")
    if resource_declaration is None:
        raise ContractError("Skylighting template no longer declares t50")
    if far_resource_declaration is None:
        raise ContractError("Skylighting template no longer declares t51")
    if diagnostic_declaration is None:
        raise ContractError("Skylighting template no longer declares u7")

    temp_declaration, _, body = shader_declarations_and_body(words)
    temp_count = words[temp_declaration[0] + 1]
    transform = [words[start:end] for start, end in body]
    if not transform or (transform[-1][0] & 0x7FF) != OPCODE_RET:
        raise ContractError("Skylighting template no longer terminates with ret")
    if any(
        (instruction[0] & 0x7FF) == OPCODE_RET
        for instruction in transform[:-1]
    ):
        raise ContractError(
            "Skylighting template contains an early return"
        )
    return (
        [
            resource_declaration,
            far_resource_declaration,
            constant_declaration,
            diagnostic_declaration,
            *gi_declarations,
        ],
        transform[:-1],
        temp_count,
    )


def remap_original_output_instruction(
    instruction: list[int], output_temps: tuple[int, int]
) -> list[int]:
    first_operand = opcode_extension_end(instruction, 0, len(instruction))
    if first_operand >= len(instruction):
        return instruction
    operand = parse_operand(instruction, first_operand, len(instruction))
    if operand.operand_type != OPERAND_OUTPUT:
        return instruction
    if (
        len(operand.immediate_indices) != 1
        or operand.immediate_indices[0] not in (0, 1)
    ):
        raise ContractError("ambient output register contract changed")
    replacement = replace_operand_with_temp(
        instruction,
        operand,
        output_temps[int(operand.immediate_indices[0])],
    )
    result = [
        *instruction[: operand.start],
        *replacement,
        *instruction[operand.end :],
    ]
    result[0] = (result[0] & ~(0x7F << 24)) | (len(result) << 24)
    return result


def remap_template_instruction(
    instruction: list[int],
    first_template_temp: int,
    template_temp_count: int,
    diffuse_visibility_temp: int,
    specular_visibility_temp: int,
) -> list[int]:
    replacements: dict[int, list[int]] = {}
    for operand in flatten_operands(
        executable_operands(instruction, 0, len(instruction))
    ):
        if operand.operand_type == OPERAND_TEMP:
            if (
                len(operand.immediate_indices) != 1
                or operand.immediate_indices[0] is None
                or int(operand.immediate_indices[0]) >= template_temp_count
            ):
                raise ContractError(
                    "Skylighting template temporary contract changed"
                )
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                first_template_temp + int(operand.immediate_indices[0]),
            )
        elif operand.operand_type == OPERAND_OUTPUT:
            if operand.immediate_indices == (0,):
                target = diffuse_visibility_temp
            elif operand.immediate_indices == (1,):
                target = specular_visibility_temp
            elif operand.immediate_indices in ((2,), (3,)):
                target = specular_visibility_temp + operand.immediate_indices[0] - 1
            else:
                raise ContractError(
                    "Skylighting template output contract changed"
                )
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                target,
            )
        elif operand.operand_type == OPERAND_INPUT:
            if operand.immediate_indices not in ((0,), (1,)):
                raise ContractError(
                    "Skylighting template input contract changed"
                )
    result = replace_instruction_operands(instruction, replacements)
    allowed_temps = set(
        range(first_template_temp, first_template_temp + template_temp_count)
    )
    allowed_temps.update(
        (diffuse_visibility_temp, specular_visibility_temp, specular_visibility_temp+1, specular_visibility_temp+2)
    )
    for operand in flatten_operands(
        executable_operands(result, 0, len(result))
    ):
        if operand.operand_type != OPERAND_TEMP:
            continue
        if (
            len(operand.immediate_indices) != 1
            or operand.immediate_indices[0] is None
            or int(operand.immediate_indices[0]) not in allowed_temps
        ):
            raise ContractError(
                "Skylighting template retained an unmapped temporary operand"
            )
    return result


def output_mask(register: int, mask: int) -> list[int]:
    return [0x00102002 | (mask << 4), register]


def temp_mask(register: int, mask: int) -> list[int]:
    return [0x00100002 | (mask << 4), register]


def temp_swizzle(register: int, swizzle: int) -> list[int]:
    return [0x00100000 | swizzle, register]


def temp_scalar(register: int, component: int = 0) -> list[int]:
    return [0x0010000A | (component << 4), register]


def compose_output_rgb(
    output_register: int,
    value_temp: int,
    visibility_temp: int,
) -> list[int]:
    result = [
        0x32,  # mad: native ambient * visibility + screen-space irradiance
        *output_mask(output_register, 0x7),
        *temp_swizzle(value_temp, 0x246),
        *temp_swizzle(visibility_temp, 0x246),
        *temp_swizzle(visibility_temp + 2, 0x246),
    ]
    result[0] |= len(result) << 24
    return result


def move_output_alpha(output_register: int, value_temp: int) -> list[int]:
    result = [
        MOV_OPCODE,
        *output_mask(output_register, 0x8),
        *temp_scalar(value_temp, 3),
    ]
    result[0] |= len(result) << 24
    return result


def declared_slots(
    words: list[int], opcode: int, operand_type: int
) -> set[int]:
    result: set[int] = set()
    for start, end in instructions(words):
        if (words[start] & 0x7FF) != opcode:
            continue
        operands = executable_operands(words, start, end)
        if (
            operands
            and operands[0].operand_type == operand_type
            and operands[0].immediate_indices
            and operands[0].immediate_indices[0] is not None
        ):
            result.add(int(operands[0].immediate_indices[0]))
    return result


def patch_ambient_shader(original: bytes, template: bytes) -> bytes:
    version, chunks, shader_index, words = shader_words(original)
    temp_declaration, _, body = shader_declarations_and_body(words)
    original_temps = words[temp_declaration[0] + 1]
    if original_temps == 0 or original_temps > 4096:
        raise ContractError(
            f"ambient temporary count is invalid: {original_temps}"
        )

    resources = declared_slots(
        words,
        OPCODE_DCL_RESOURCE,
        OPERAND_RESOURCE,
    )
    constants = declared_slots(
        words,
        OPCODE_DCL_CONSTANT_BUFFER,
        OPERAND_CONSTANT_BUFFER,
    )
    samplers = declared_slots(
        words,
        OPCODE_DCL_SAMPLER,
        OPERAND_SAMPLER,
    )
    if not REQUIRED_NATIVE_RESOURCE_SLOTS.issubset(resources):
        raise ContractError("ambient native t1/t2/t3 contract changed")
    if not REQUIRED_NATIVE_CONSTANT_SLOTS.issubset(constants):
        raise ContractError("ambient native b2/b8/b12 contract changed")
    if not REQUIRED_NATIVE_SAMPLER_SLOTS.issubset(samplers):
        raise ContractError("ambient native s1/s2/s3 contract changed")
    if resources.intersection(range(57,65)):
        raise ContractError("ambient shader owns a Screen Space GI slot")
    if SKYLIGHTING_PROBE_SLOT in resources:
        raise ContractError("ambient shader unexpectedly owns t50")
    if SKYLIGHTING_FAR_PROBE_SLOT in resources:
        raise ContractError("ambient shader unexpectedly owns t51")
    if SKYLIGHTING_CONSTANT_SLOT in constants:
        raise ContractError("ambient shader unexpectedly owns b13")

    declarations, transform, template_temps = declaration_contract(template)
    output_temps = (original_temps, original_temps + 1)
    first_template_temp = original_temps + len(output_temps)
    diffuse_visibility_temp = first_template_temp + template_temps
    specular_visibility_temp = diffuse_visibility_temp + 1

    transformed: list[int] = []
    for instruction in transform:
        transformed.extend(
            remap_template_instruction(
                instruction,
                first_template_temp,
                template_temps,
                diffuse_visibility_temp,
                specular_visibility_temp,
            )
        )

    prefix = words[: temp_declaration[0]]
    for start, end in instructions(words):
        if (words[start] & 0x7FF) == OPCODE_DCL_CONSTANT_BUFFER:
            operand = executable_operands(words, start, end)[0]
            if operand.immediate_indices[0] == 12:
                prefix[end - 1] = max(prefix[end - 1], 61)
    for declaration in declarations:
        prefix.extend(declaration)
    updated_temps = words[temp_declaration[0] : temp_declaration[1]]
    updated_temps[1] = specular_visibility_temp + 3

    rewritten: list[int] = []
    ret_count = 0
    for start, end in body:
        instruction = words[start:end]
        if (instruction[0] & 0x7FF) == OPCODE_RET:
            ret_count += 1
            rewritten.extend(transformed)
            rewritten.extend(
                compose_output_rgb(
                    0,
                    output_temps[0],
                    diffuse_visibility_temp,
                )
            )
            rewritten.extend(move_output_alpha(0, output_temps[0]))
            rewritten.extend(
                compose_output_rgb(
                    1,
                    output_temps[1],
                    specular_visibility_temp,
                )
            )
            rewritten.extend(move_output_alpha(1, output_temps[1]))
            rewritten.extend(instruction)
        else:
            rewritten.extend(
                remap_original_output_instruction(instruction, output_temps)
            )
    if ret_count != 1:
        raise ContractError(
            f"ambient shader has {ret_count} return instructions"
        )

    patched_words = [*prefix, *updated_temps, *rewritten]
    patched_words[1] = len(patched_words)
    patched_chunks = list(chunks)
    patched_chunks[shader_index] = DxbcChunk(
        patched_chunks[shader_index].tag,
        pack_words(patched_words),
    )
    return build_dxbc(version, patched_chunks)


def gamma_offsets(data: bytes) -> list[int]:
    needle = struct.pack("<f", VANILLA_GAMMA)
    offsets: list[int] = []
    cursor = 0
    while True:
        offset = data.find(needle, cursor)
        if offset < 0:
            break
        offsets.append(offset)
        cursor = offset + len(needle)
    if len(offsets) != EXPECTED_GAMMA_FLOATS:
        raise ContractError(
            f"expected {EXPECTED_GAMMA_FLOATS} transformed gamma floats, "
            f"found {len(offsets)}"
        )
    return offsets


def write_byte_array(rows: list[str], symbol: str, data: bytes) -> None:
    rows.append(f"inline constexpr unsigned char {symbol}[] = {{")
    for offset in range(0, len(data), 16):
        values = ", ".join(
            f"0x{value:02x}" for value in data[offset : offset + 16]
        )
        rows.append(f"    {values},")
    rows.extend(("};", ""))


def write_compute_header(path: Path, data: bytes, symbol: str = "csah_skylighting_update_probes") -> None:
    rows = ["#pragma once", ""]
    write_byte_array(rows, symbol, data)
    path.write_text("\n".join(rows), encoding="utf-8")


def write_ambient_header(
    path: Path,
    candidates: list[tuple[bytes, list[int]]],
) -> None:
    rows = [
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
    ]
    symbols: list[str] = []
    for index, (candidate, _) in enumerate(candidates):
        symbol = f"csah_skylighting_ambient_{index:03d}"
        symbols.append(symbol)
        write_byte_array(rows, symbol, candidate)

    rows.extend(
        (
            "struct Fo4vrCsSkylightingAmbientContract",
            "{",
            "    const unsigned char* replacementBytecode;",
            "    std::size_t replacementBytecodeLength;",
            "    std::array<std::uint32_t, 6> gammaOffsets;",
            "};",
            "",
            "inline constexpr std::array<",
            "    Fo4vrCsSkylightingAmbientContract,",
            f"    {len(candidates)}> csah_skylighting_ambient_contracts{{{{",
        )
    )
    for symbol, (_, offsets) in zip(symbols, candidates, strict=True):
        offset_text = ", ".join(f"0x{value:08X}u" for value in offsets)
        rows.append(
            "    Fo4vrCsSkylightingAmbientContract{ "
            f"{symbol}, sizeof({symbol}), {{ {offset_text} }} }},"
        )
    rows.extend(("}};", ""))
    path.write_text("\n".join(rows), encoding="utf-8")


def ambient_sources(root: Path) -> list[Path]:
    manifest_path = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "DFLightAmbientContracts.json"
    )
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    contracts = manifest.get("contracts")
    if not isinstance(contracts, list) or len(contracts) != 39:
        raise ContractError("DFLight ambient manifest no longer has 39 contracts")
    directory = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "VerifiedDFLightAmbient"
    )
    result: list[Path] = []
    for contract in contracts:
        if not isinstance(contract, dict) or not isinstance(
            contract.get("file"), str
        ):
            raise ContractError("DFLight ambient manifest entry is invalid")
        path = directory / contract["file"]
        if not path.is_file():
            raise ContractError(f"ambient source is missing: {path}")
        result.append(path)
    return result


def validate_candidate(
    fxc: Path,
    temporary: Path,
    index: int,
    candidate: bytes,
) -> None:
    binary = temporary / f"SkylightingAmbient_{index:03d}.dxbc"
    assembly = temporary / f"SkylightingAmbient_{index:03d}.asm.txt"
    binary.write_bytes(candidate)
    run(
        [
            str(fxc),
            "/dumpbin",
            "/nologo",
            "/Fc",
            str(assembly),
            str(binary),
        ],
        f"Skylighting ambient candidate {index} validation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_constantbuffer CB13[20], immediateIndexed",
        "dcl_resource_texture3d (float,float,float,float) t50",
        "dcl_resource_texture3d (float,float,float,float) t51",
        "dcl_uav_raw u7",
        "dcl_input_ps_siv linear noperspective v0.xy, position",
        "dcl_input_ps constant v1.x",
        "dcl_output o0.xyzw",
        "dcl_output o1.xyzw",
    ):
        if required not in text:
            raise ContractError(
                f"Skylighting ambient candidate {index} lost: {required}"
            )
    if sum(line.strip() == "ret" for line in text.splitlines()) != 1:
        raise ContractError(
            f"Skylighting ambient candidate {index} has an invalid return count"
        )


def main() -> int:
    args = arguments()
    root = args.root.resolve()
    args.ambient_header.parent.mkdir(parents=True, exist_ok=True)
    args.compute_binary.parent.mkdir(parents=True, exist_ok=True)
    args.compute_header.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="fo4vr-skylighting-") as folder:
        temporary = Path(folder)
        shader_root = (
            root / "package" / "Shaders" / "Community" / "Skylighting"
        )
        template, template_assembly = compile_shader(
            args.fxc.resolve(),
            shader_root / "SkylightingAmbientTransform.hlsl",
            "ps_5_0",
            "PSMain",
            temporary / "SkylightingAmbientTransform.dxbc",
            temporary / "SkylightingAmbientTransform.asm.txt",
            "Skylighting ambient transform compilation",
        )
        for required in (
            "dcl_constantbuffer CB13[20], immediateIndexed",
            "dcl_resource_texture3d (float,float,float,float) t50",
            "dcl_resource_texture3d (float,float,float,float) t51",
            "dcl_uav_raw u7",
            "dcl_input_ps_siv linear noperspective v0.xy, position",
            "dcl_input_ps constant v1.x",
            "dcl_output o0.xyzw",
            "dcl_output o1.xyzw",
        ):
            if required not in template_assembly:
                raise ContractError(
                    "Skylighting ambient template changed: " + required
                )

        compute, compute_assembly = compile_shader(
            args.fxc.resolve(),
            shader_root / "UpdateProbesCS.hlsl",
            "cs_5_0",
            "CSMain",
            temporary / "UpdateProbesCS.dxbc",
            temporary / "UpdateProbesCS.asm.txt",
            "Skylighting probe-update compilation",
        )
        for required in (
            "dcl_resource_texture2d (float,float,float,float) t0",
            "dcl_uav_typed_texture3d (float,float,float,float) u0",
            "dcl_uav_typed_texture3d (uint,uint,uint,uint) u1",
            "dcl_uav_raw u2",
            "dcl_constantbuffer CB13[19], immediateIndexed",
            "dcl_thread_group 8, 8, 1",
        ):
            if required not in compute_assembly:
                raise ContractError(
                    "Skylighting probe-update assembly changed: " + required
                )

        sun, _ = compile_shader(
            args.fxc.resolve(), shader_root / "UpdateSunProbesCS.hlsl",
            "cs_5_0", "CSMain", temporary / "UpdateSunProbesCS.dxbc",
            temporary / "UpdateSunProbesCS.asm.txt", "Skylighting sun history compilation")
        args.sun_compute_binary.write_bytes(sun)
        write_compute_header(args.sun_compute_header, sun, "csah_skylighting_update_sun")

        candidates: list[tuple[bytes, list[int]]] = []
        for index, source in enumerate(ambient_sources(root)):
            try:
                candidate = patch_ambient_shader(
                    source.read_bytes(),
                    template,
                )
                offsets = gamma_offsets(candidate)
                validate_candidate(
                    args.fxc.resolve(),
                    temporary,
                    index,
                    candidate,
                )
            except ContractError as error:
                raise ContractError(f"{source.name}: {error}") from error
            candidates.append((candidate, offsets))
        if len(candidates) != 39:
            raise ContractError(
                f"Skylighting generated {len(candidates)} ambient contracts"
            )

        args.compute_binary.write_bytes(compute)
        write_compute_header(args.compute_header, compute)
        write_ambient_header(args.ambient_header, candidates)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ContractError, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"Skylighting generation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
