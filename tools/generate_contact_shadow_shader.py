from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import census_linear_lighting_fxp as census
from dxbc_transform import (
    OPCODE_DCL_CONSTANT_BUFFER,
    OPCODE_DCL_RESOURCE,
    OPCODE_DCL_SAMPLER,
    OPCODE_RET,
    OPERAND_CONSTANT_BUFFER,
    OPERAND_INPUT,
    OPERAND_OUTPUT,
    OPERAND_RESOURCE,
    OPERAND_TEMP,
    DxbcChunk,
    TransformError as ContractError,
    build_dxbc,
    executable_operands,
    flatten_operands,
    instructions,
    pack_words,
    replace_operand_with_temp,
    shader_declarations_and_body,
    shader_words,
)


EXPECTED_ALIAS_KEYS = {0x01200202, 0x01200282, 0x11200202}
EXPECTED_COMPATIBLE_SHADER_COUNT = 22
DFLIGHT_DIRECTIONAL_DESCRIPTOR_MASK = 0x00000003
DFLIGHT_CHARACTER_DESCRIPTOR_MASK = 0x04000000
CONTACT_CONSTANT_SLOT = 13
CONTACT_MASK_SLOT = 46
WRAPPED_GRASS_CONSTANT_SLOT = 11
HAIR_SPECULAR_CONSTANT_SLOT = 10
BASIC_WETNESS_CONSTANT_SLOT = 9
PBR_CONSTANT_SLOT = 7
PBR_MATERIAL_SLOT = 45
SURFACE_CLASS_SLOT = 47
DIRECTIONAL_DIAGNOSTIC_MODE_COUNT = 6
MUL_OPCODE = 0x38
DIV_OPCODE = 0x0E
MOV_OPCODE = 0x36
WRAPPED_NDOTL_INSTRUCTION = (
    0x06002036,
    0x00100012,
    0x00000001,
    0x8010003A,
    0x00000041,
    0x00000001,
)
STOCK_DIRECTIONAL_FINAL_OUTPUTS = (
    # mul o1.xyz, r0.wwww, r1.xyzx
    0x07000038,
    0x00102072,
    0x00000001,
    0x00100FF6,
    0x00000000,
    0x00100246,
    0x00000001,
    # mul r0.xyz, r0.wwww, r0.xyzx
    0x07000038,
    0x00100072,
    0x00000000,
    0x00100FF6,
    0x00000000,
    0x00100246,
    0x00000000,
    # mov r0.w, l(0)
    0x05000036,
    0x00100082,
    0x00000000,
    0x00004001,
    0x00000000,
    # div o0.xyzw, r0.xyzw, l(3, 3, 3, 3)
    0x0A00000E,
    0x001020F2,
    0x00000000,
    0x00100E46,
    0x00000000,
    0x00004002,
    0x40400000,
    0x40400000,
    0x40400000,
    0x40400000,
    # mov o1.w, l(1); ret
    0x05000036,
    0x00102082,
    0x00000001,
    0x00004001,
    0x3F800000,
    0x0100003E,
)


def directional_diagnostic_outputs(mode_index: int) -> tuple[int, ...]:
    if mode_index < 0 or mode_index > 2:
        raise ContractError("directional diagnostic mode is invalid")

    zero_sources = (
        (0x00100012, 0x00004001, 0x00000000),
        (0x00100022, 0x00004001, 0x00000000),
        (0x00100042, 0x00004001, 0x00000000),
    )
    signal_sources = (
        (0x00100012, 0x0010003A, 0x00000005),  # red = N.L in r5.w
        (0x00100022, 0x0010000A, 0x00000008),  # green = N.V in r8.x
        (0x00100042, 0x0010003A, 0x00000000),  # blue = visibility in r0.w
    )
    words: list[int] = []
    for component in range(3):
        destination, source, register = (
            signal_sources[component]
            if component == mode_index
            else zero_sources[component]
        )
        words.extend((0x05000036, destination, 0x00000000, source, register))
    words.extend(
        (
            # Preserve the native /3 DFLight accumulation scale.
            0x05000036,
            0x00100082,
            0x00000000,
            0x00004001,
            0x40400000,
            0x0A00000E,
            0x001020F2,
            0x00000000,
            0x00100E46,
            0x00000000,
            0x00004002,
            0x40400000,
            0x40400000,
            0x40400000,
            0x40400000,
            # The private diagnostic draw binds only target zero. Four NOPs
            # retain the exact stock suffix length before ret.
            0x0100003A,
            0x0100003A,
            0x0100003A,
            0x0100003A,
            0x0100003E,
        )
    )
    result = tuple(words)
    if len(result) != len(STOCK_DIRECTIONAL_FINAL_OUTPUTS):
        raise ContractError("directional diagnostic output length changed")
    return result


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate the exact FO4VR directional contact-shadow shader."
    )
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--fxc", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--compute-binary", type=Path, required=True)
    parser.add_argument("--compute-header", type=Path, required=True)
    parser.add_argument("--dispatch-binary", type=Path, required=True)
    parser.add_argument("--dispatch-header", type=Path, required=True)
    parser.add_argument("--resolve-binary", type=Path, required=True)
    parser.add_argument("--resolve-header", type=Path, required=True)
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


def original_shader(root: Path) -> bytes:
    inventory = census.parse_fxp((root / "Shaders012_VR.fxp").read_bytes())
    matches = [
        item
        for item in inventory.containers
        if item.family == "DFLight"
        and item.stage == "PS"
        and item.key in EXPECTED_ALIAS_KEYS
    ]
    keys = {int(item.key) for item in matches if item.key is not None}
    if keys != EXPECTED_ALIAS_KEYS or len(matches) != len(EXPECTED_ALIAS_KEYS):
        raise ContractError(
            "exact FO4VR directional DFLight aliases changed: "
            f"expected {sorted(EXPECTED_ALIAS_KEYS)}, found {sorted(keys)}"
        )
    return matches[0].data


def directional_pixel_shaders(root: Path) -> list[census.DxbcContainer]:
    inventory = census.parse_fxp((root / "Shaders012_VR.fxp").read_bytes())
    unique: dict[tuple[int, str], census.DxbcContainer] = {}
    for item in inventory.containers:
        if (
            item.family != "DFLight"
            or item.stage != "PS"
            or item.key is None
            or item.key & DFLIGHT_CHARACTER_DESCRIPTOR_MASK
            or not item.key & DFLIGHT_DIRECTIONAL_DESCRIPTOR_MASK
        ):
            continue
        unique.setdefault(item.identity, item)
    return sorted(
        unique.values(),
        key=lambda item: (item.size, item.checksum),
    )


def compile_template(root: Path, fxc: Path, temporary: Path) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ContactShadows"
        / "ContactShadowTransform.hlsl"
    )
    output = temporary / "ContactShadowTransform.dxbc"
    assembly = temporary / "ContactShadowTransform.asm.txt"
    run(
        [
            str(fxc),
            "/nologo",
            "/T",
            "ps_5_0",
            "/E",
            "PSMain",
            "/O3",
            "/Ges",
            "/WX",
            "/Fo",
            str(output),
            "/Fc",
            str(assembly),
            str(source),
        ],
        "contact-shadow transform compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_constantbuffer CB13[3], immediateIndexed",
        "dcl_resource_texture2d (float,float,float,float) t46",
        "dcl_input_ps_siv linear noperspective v0.xy, position",
    ):
        if required not in text:
            raise ContractError(
                "contact-shadow transform assembly changed: " + required
            )
    return output.read_bytes()


def compile_wrapped_grass_template(
    root: Path, fxc: Path, temporary: Path
) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ContactShadows"
        / "WrappedGrassTransform.hlsl"
    )
    output = temporary / "WrappedGrassTransform.dxbc"
    assembly = temporary / "WrappedGrassTransform.asm.txt"
    run(
        [
            str(fxc),
            "/nologo",
            "/T",
            "ps_5_0",
            "/E",
            "PSMain",
            "/O3",
            "/Ges",
            "/WX",
            "/Fo",
            str(output),
            "/Fc",
            str(assembly),
            str(source),
        ],
        "wrapped-grass transform compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_constantbuffer CB11[1], immediateIndexed",
        "dcl_resource_texture2d (float,float,float,float) t47",
        "dcl_input_ps linear v1.x",
        "dcl_input_ps_siv linear noperspective v0.xy, position",
    ):
        if required not in text:
            raise ContractError(
                "wrapped-grass transform assembly changed: " + required
            )
    return output.read_bytes()


def compile_hair_specular_template(
    root: Path, fxc: Path, temporary: Path
) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ContactShadows"
        / "HairSpecularTransform.hlsl"
    )
    output = temporary / "HairSpecularTransform.dxbc"
    assembly = temporary / "HairSpecularTransform.asm.txt"
    run(
        [
            str(fxc),
            "/nologo",
            "/T",
            "ps_5_0",
            "/E",
            "PSMain",
            "/O3",
            "/Ges",
            "/WX",
            "/Fo",
            str(output),
            "/Fc",
            str(assembly),
            str(source),
        ],
        "hair-specular transform compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_constantbuffer CB10[1], immediateIndexed",
        "dcl_resource_texture2d (float,float,float,float) t0",
        "dcl_resource_texture2d (float,float,float,float) t47",
        "dcl_input_ps linear v1.xyz",
        "dcl_input_ps_siv linear noperspective v0.xy, position",
    ):
        if required not in text:
            raise ContractError(
                "hair-specular transform assembly changed: " + required
            )
    return output.read_bytes()


def compile_basic_wetness_template(
    root: Path, fxc: Path, temporary: Path
) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ContactShadows"
        / "BasicWetnessTransform.hlsl"
    )
    output = temporary / "BasicWetnessTransform.dxbc"
    assembly = temporary / "BasicWetnessTransform.asm.txt"
    run(
        [
            str(fxc),
            "/nologo",
            "/T",
            "ps_5_0",
            "/E",
            "PSMain",
            "/O3",
            "/Ges",
            "/WX",
            "/Fo",
            str(output),
            "/Fc",
            str(assembly),
            str(source),
        ],
        "basic-wetness transform compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_constantbuffer CB9[1], immediateIndexed",
        "dcl_resource_texture2d (float,float,float,float) t47",
        "dcl_input_ps linear v1.xyz",
        "dcl_input_ps linear v2.xyzw",
        "dcl_input_ps_siv linear noperspective v0.xy, position",
        "dcl_output o0.xyzw",
        "dcl_output o1.xyzw",
    ):
        if required not in text:
            raise ContractError(
                "basic-wetness transform assembly changed: " + required
            )
    return output.read_bytes()


def compile_pbr_template(
    root: Path, fxc: Path, temporary: Path
) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "PBR"
        / "PbrDirectionalTransform.hlsl"
    )
    source_text = source.read_text(encoding="utf-8")
    direct_gate = (
        "if (PbrFeatureParams0.x > 1.0f / 255.0f &&\n"
        "        PbrFeatureParams0.z > 0.5f)"
    )
    if direct_gate not in source_text:
        raise ContractError(
            "PBR Direct-Light GGX no longer owns the complete direct branch"
        )
    output = temporary / "PbrDirectionalTransform.dxbc"
    assembly = temporary / "PbrDirectionalTransform.asm.txt"
    run(
        [
            str(fxc),
            "/nologo",
            "/T",
            "ps_5_0",
            "/E",
            "PSMain",
            "/O3",
            "/Ges",
            "/WX",
            "/Fo",
            str(output),
            "/Fc",
            str(assembly),
            str(source),
        ],
        "PBR directional transform compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_constantbuffer CB2[4], immediateIndexed",
        "dcl_constantbuffer CB7[4], immediateIndexed",
        "dcl_constantbuffer CB9[2], immediateIndexed",
        "dcl_resource_texture2d (float,float,float,float) t0",
        "dcl_resource_texture2d (float,float,float,float) t2",
        "dcl_resource_texture2d (float,float,float,float) t45",
        "dcl_resource_texture2d (float,float,float,float) t47",
        "dcl_output o0.xyzw",
        "dcl_output o1.xyzw",
        "l(3.141593)",
        "l(0.040000)",
    ):
        if required not in text:
            raise ContractError(
                "PBR directional transform assembly changed: " + required
            )
    if re.search(r"\bcb2\[r", text, re.IGNORECASE):
        raise ContractError(
            "PBR directional transform retained a relative b2 index"
        )
    for required_eye_constant in ("cb2[1]", "cb2[2]"):
        if required_eye_constant not in text:
            raise ContractError(
                "PBR directional transform lost static stereo light "
                + required_eye_constant
            )
    pbr_branch = text.find("if_nz")
    material_load = re.search(
        r"^\s*ld_indexable\(texture2d\).*\bt2(?:\b|\.)",
        text,
        re.MULTILINE,
    )
    surface_load = re.search(
        r"^\s*ld_indexable\(texture2d\).*\bt47(?:\b|\.)",
        text,
        re.MULTILINE,
    )
    if (
        pbr_branch < 0
        or material_load is None
        or surface_load is None
        or pbr_branch >= material_load.start()
        or pbr_branch >= surface_load.start()
    ):
        raise ContractError(
            "PBR disabled path no longer branches before material loads"
        )
    return output.read_bytes()


def compile_directional_diagnostic_templates(
    root: Path, fxc: Path, temporary: Path
) -> tuple[bytes, bytes, bytes]:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ContactShadows"
        / "DirectionalLightingDiagnosticTransform.hlsl"
    )
    results: list[bytes] = []
    for mode in range(4, 7):
        output = temporary / f"DirectionalLightingDiagnosticMode{mode}.dxbc"
        assembly = temporary / f"DirectionalLightingDiagnosticMode{mode}.asm.txt"
        run(
            [
                str(fxc),
                "/nologo",
                "/T",
                "ps_5_0",
                "/E",
                "PSMain",
                "/O3",
                "/Ges",
                "/WX",
                f"/DDIRECTIONAL_DIAGNOSTIC_MODE={mode}",
                "/Fo",
                str(output),
                "/Fc",
                str(assembly),
                str(source),
            ],
            f"directional diagnostic mode {mode} transform compilation",
        )
        text = assembly.read_text(encoding="utf-8")
        required = [
            "dcl_constantbuffer CB2[46], dynamicIndexed",
            "dcl_input_ps constant v1.x",
            "dcl_output o0.xyzw",
        ]
        if mode != 6:
            required.append("mov o0.w, l(1.000000)")
        if mode >= 5:
            required.extend(
                (
                    "dcl_constantbuffer CB12[3], immediateIndexed",
                    "cb12[0].xyzx",
                    "cb12[1].xyzx",
                    "cb12[2].xyzx",
                )
            )
        if mode == 6:
            required.extend(
                (
                    "dcl_input_ps linear v2.xyz",
                    "dp3_sat",
                    "mov o0.yzw, l(0,0,0,1.000000)",
                )
            )
        for contract in required:
            if contract not in text:
                raise ContractError(
                    f"directional diagnostic mode {mode} assembly changed: "
                    f"{contract}"
                )
        results.append(output.read_bytes())
    return tuple(results)


def replace_instruction_operands(
    instruction: list[int], replacements: dict[int, list[int]]
) -> list[int]:
    operands = {
        operand.start: operand
        for operand in flatten_operands(executable_operands(instruction, 0, len(instruction)))
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
            raise ContractError("contact-shadow operand replacement is invalid")
        result.extend(replacement)
        cursor = operand.end
    result[0] = (result[0] & ~(0x7F << 24)) | (len(result) << 24)
    return result


def template_contract(
    template: bytes,
) -> tuple[list[list[int]], list[list[int]], int]:
    _, _, _, words = shader_words(template)
    constant_declaration: list[int] | None = None
    resource_declaration: list[int] | None = None
    for start, end in instructions(words):
        opcode = words[start] & 0x7FF
        operands = executable_operands(words, start, end)
        if (
            opcode == OPCODE_DCL_CONSTANT_BUFFER
            and operands
            and operands[0].operand_type == OPERAND_CONSTANT_BUFFER
            and operands[0].immediate_indices
            and operands[0].immediate_indices[0] == CONTACT_CONSTANT_SLOT
        ):
            constant_declaration = words[start:end]
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (CONTACT_MASK_SLOT,)
        ):
            resource_declaration = words[start:end]
    if constant_declaration is None:
        raise ContractError("contact-shadow template no longer declares b13")
    if resource_declaration is None:
        raise ContractError("contact-shadow template no longer declares t46")

    temp_declaration, _, body = shader_declarations_and_body(words)
    temp_count = words[temp_declaration[0] + 1]
    transform = [words[start:end] for start, end in body]
    if not transform or (transform[-1][0] & 0x7FF) != OPCODE_RET:
        raise ContractError("contact-shadow template no longer terminates with ret")
    if any((instruction[0] & 0x7FF) == OPCODE_RET for instruction in transform[:-1]):
        raise ContractError(
            "contact-shadow template contains an early return that would exit DFLight"
        )
    return [resource_declaration, constant_declaration], transform[:-1], temp_count


def wrapped_grass_template_contract(
    template: bytes,
) -> tuple[list[list[int]], list[list[int]], int]:
    _, _, _, words = shader_words(template)
    constant_declaration: list[int] | None = None
    resource_declaration: list[int] | None = None
    for start, end in instructions(words):
        opcode = words[start] & 0x7FF
        operands = executable_operands(words, start, end)
        if (
            opcode == OPCODE_DCL_CONSTANT_BUFFER
            and operands
            and operands[0].operand_type == OPERAND_CONSTANT_BUFFER
            and operands[0].immediate_indices
            and operands[0].immediate_indices[0]
            == WRAPPED_GRASS_CONSTANT_SLOT
        ):
            constant_declaration = words[start:end]
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (SURFACE_CLASS_SLOT,)
        ):
            resource_declaration = words[start:end]
    if constant_declaration is None or resource_declaration is None:
        raise ContractError("wrapped-grass template resource contract changed")
    temp_declaration, _, body = shader_declarations_and_body(words)
    temp_count = words[temp_declaration[0] + 1]
    transform = [words[start:end] for start, end in body]
    if not transform or (transform[-1][0] & 0x7FF) != OPCODE_RET:
        raise ContractError("wrapped-grass template no longer terminates with ret")
    if any((instruction[0] & 0x7FF) == OPCODE_RET for instruction in transform[:-1]):
        raise ContractError("wrapped-grass template contains an early return")
    return [resource_declaration, constant_declaration], transform[:-1], temp_count


def hair_specular_template_contract(
    template: bytes,
) -> tuple[list[list[int]], list[list[int]], int]:
    _, _, _, words = shader_words(template)
    constant_declaration: list[int] | None = None
    owns_gbuffer_material = False
    owns_surface_class = False
    for start, end in instructions(words):
        opcode = words[start] & 0x7FF
        operands = executable_operands(words, start, end)
        if (
            opcode == OPCODE_DCL_CONSTANT_BUFFER
            and operands
            and operands[0].operand_type == OPERAND_CONSTANT_BUFFER
            and operands[0].immediate_indices
            and operands[0].immediate_indices[0]
            == HAIR_SPECULAR_CONSTANT_SLOT
        ):
            constant_declaration = words[start:end]
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (0,)
        ):
            owns_gbuffer_material = True
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (SURFACE_CLASS_SLOT,)
        ):
            owns_surface_class = True
    if (
        constant_declaration is None
        or not owns_gbuffer_material
        or not owns_surface_class
    ):
        raise ContractError("hair-specular template resource contract changed")
    temp_declaration, _, body = shader_declarations_and_body(words)
    temp_count = words[temp_declaration[0] + 1]
    transform = [words[start:end] for start, end in body]
    if not transform or (transform[-1][0] & 0x7FF) != OPCODE_RET:
        raise ContractError("hair-specular template no longer terminates with ret")
    if any((instruction[0] & 0x7FF) == OPCODE_RET for instruction in transform[:-1]):
        raise ContractError("hair-specular template contains an early return")
    # Wrapped Grass already contributes the shared t47 declaration.
    return [constant_declaration], transform[:-1], temp_count


def basic_wetness_template_contract(
    template: bytes,
) -> tuple[list[list[int]], list[list[int]], int]:
    _, _, _, words = shader_words(template)
    constant_declaration: list[int] | None = None
    owns_surface_class = False
    for start, end in instructions(words):
        opcode = words[start] & 0x7FF
        operands = executable_operands(words, start, end)
        if (
            opcode == OPCODE_DCL_CONSTANT_BUFFER
            and operands
            and operands[0].operand_type == OPERAND_CONSTANT_BUFFER
            and operands[0].immediate_indices
            and operands[0].immediate_indices[0]
            == BASIC_WETNESS_CONSTANT_SLOT
        ):
            constant_declaration = words[start:end]
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (SURFACE_CLASS_SLOT,)
        ):
            owns_surface_class = True
    if constant_declaration is None or not owns_surface_class:
        raise ContractError("basic-wetness template resource contract changed")
    temp_declaration, _, body = shader_declarations_and_body(words)
    temp_count = words[temp_declaration[0] + 1]
    transform = [words[start:end] for start, end in body]
    if not transform or (transform[-1][0] & 0x7FF) != OPCODE_RET:
        raise ContractError("basic-wetness template no longer terminates with ret")
    if any((instruction[0] & 0x7FF) == OPCODE_RET for instruction in transform[:-1]):
        raise ContractError("basic-wetness template contains an early return")
    # Wrapped Grass already contributes the shared t47 declaration.
    return [constant_declaration], transform[:-1], temp_count


def pbr_template_contract(
    template: bytes,
) -> tuple[list[list[int]], list[list[int]], int]:
    _, _, _, words = shader_words(template)
    constant_declaration: list[int] | None = None
    owns_wetness_constants = False
    owns_albedo = False
    owns_material = False
    pbr_material_declaration: list[int] | None = None
    owns_surface_class = False
    for start, end in instructions(words):
        opcode = words[start] & 0x7FF
        operands = executable_operands(words, start, end)
        if (
            opcode == OPCODE_DCL_CONSTANT_BUFFER
            and operands
            and operands[0].operand_type == OPERAND_CONSTANT_BUFFER
            and operands[0].immediate_indices
            and operands[0].immediate_indices[0] == PBR_CONSTANT_SLOT
        ):
            constant_declaration = words[start:end]
        if (
            opcode == OPCODE_DCL_CONSTANT_BUFFER
            and operands
            and operands[0].operand_type == OPERAND_CONSTANT_BUFFER
            and operands[0].immediate_indices
            and operands[0].immediate_indices[0]
            == BASIC_WETNESS_CONSTANT_SLOT
        ):
            owns_wetness_constants = True
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (0,)
        ):
            owns_albedo = True
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (2,)
        ):
            owns_material = True
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (PBR_MATERIAL_SLOT,)
        ):
            pbr_material_declaration = words[start:end]
        if (
            opcode == OPCODE_DCL_RESOURCE
            and operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (SURFACE_CLASS_SLOT,)
        ):
            owns_surface_class = True
    if (
        constant_declaration is None
        or not owns_wetness_constants
        or not owns_albedo
        or not owns_material
        or pbr_material_declaration is None
        or not owns_surface_class
    ):
        raise ContractError("PBR directional template resource contract changed")
    temp_declaration, _, body = shader_declarations_and_body(words)
    temp_count = words[temp_declaration[0] + 1]
    transform = [words[start:end] for start, end in body]
    if not transform or (transform[-1][0] & 0x7FF) != OPCODE_RET:
        raise ContractError("PBR directional template no longer terminates with ret")
    if any(
        (instruction[0] & 0x7FF) == OPCODE_RET
        for instruction in transform[:-1]
    ):
        raise ContractError("PBR directional template contains an early return")
    # t0/t2 are native DFLight resources, t47 is contributed by the surface
    # transforms, and b9 is contributed by Basic Wetness. Authored PBR adds
    # private t45 alongside its verified-free b7.
    return [constant_declaration, pbr_material_declaration], transform[:-1], temp_count


def directional_diagnostic_template_contract(
    template: bytes,
) -> tuple[list[list[int]], int]:
    _, _, _, words = shader_words(template)
    temp_declaration, _, body = shader_declarations_and_body(words)
    temp_count = words[temp_declaration[0] + 1]
    transform = [words[start:end] for start, end in body]
    if not transform or (transform[-1][0] & 0x7FF) != OPCODE_RET:
        raise ContractError(
            "directional diagnostic template no longer terminates with ret"
        )
    if any(
        (instruction[0] & 0x7FF) == OPCODE_RET
        for instruction in transform[:-1]
    ):
        raise ContractError(
            "directional diagnostic template contains an early return"
        )
    return transform[:-1], temp_count


def remap_transform_instruction(
    instruction: list[int], first_scratch: int, template_temp_count: int,
    visibility_scratch: int
) -> list[int]:
    replacements: dict[int, list[int]] = {}
    for operand in flatten_operands(executable_operands(instruction, 0, len(instruction))):
        if operand.operand_type == OPERAND_TEMP:
            if (
                len(operand.immediate_indices) != 1
                or operand.immediate_indices[0] is None
                or int(operand.immediate_indices[0]) >= template_temp_count
            ):
                raise ContractError("contact-shadow template temporary changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                first_scratch + int(operand.immediate_indices[0]),
            )
        elif operand.operand_type == OPERAND_OUTPUT:
            if operand.immediate_indices != (0,):
                raise ContractError("contact-shadow template output changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                visibility_scratch,
            )
    return replace_instruction_operands(instruction, replacements)


def remap_wrapped_grass_instruction(
    instruction: list[int], first_scratch: int, template_temp_count: int
) -> list[int]:
    replacements: dict[int, list[int]] = {}
    for operand in flatten_operands(executable_operands(instruction, 0, len(instruction))):
        if operand.operand_type == OPERAND_TEMP:
            if (
                len(operand.immediate_indices) != 1
                or operand.immediate_indices[0] is None
                or int(operand.immediate_indices[0]) >= template_temp_count
            ):
                raise ContractError("wrapped-grass template temporary changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                first_scratch + int(operand.immediate_indices[0]),
            )
        elif operand.operand_type == OPERAND_INPUT:
            if operand.immediate_indices == (1,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction,
                    operand,
                    1,
                )
            elif operand.immediate_indices != (0,):
                raise ContractError("wrapped-grass template input changed")
        elif operand.operand_type == OPERAND_OUTPUT:
            if operand.immediate_indices != (0,):
                raise ContractError("wrapped-grass template output changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                1,
            )
    return replace_instruction_operands(instruction, replacements)


def remap_hair_specular_instruction(
    instruction: list[int], first_scratch: int, template_temp_count: int
) -> list[int]:
    replacements: dict[int, list[int]] = {}
    for operand in flatten_operands(executable_operands(instruction, 0, len(instruction))):
        if operand.operand_type == OPERAND_TEMP:
            if (
                len(operand.immediate_indices) != 1
                or operand.immediate_indices[0] is None
                or int(operand.immediate_indices[0]) >= template_temp_count
            ):
                raise ContractError("hair-specular template temporary changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                first_scratch + int(operand.immediate_indices[0]),
            )
        elif operand.operand_type == OPERAND_INPUT:
            if operand.immediate_indices == (1,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction, operand, 1
                )
            elif operand.immediate_indices != (0,):
                raise ContractError("hair-specular template input changed")
        elif operand.operand_type == OPERAND_OUTPUT:
            if operand.immediate_indices != (0,):
                raise ContractError("hair-specular template output changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction, operand, 1
            )
    return replace_instruction_operands(instruction, replacements)


def remap_basic_wetness_instruction(
    instruction: list[int], first_scratch: int, template_temp_count: int
) -> list[int]:
    replacements: dict[int, list[int]] = {}
    for operand in flatten_operands(executable_operands(instruction, 0, len(instruction))):
        if operand.operand_type == OPERAND_TEMP:
            if (
                len(operand.immediate_indices) != 1
                or operand.immediate_indices[0] is None
                or int(operand.immediate_indices[0]) >= template_temp_count
            ):
                raise ContractError("basic-wetness template temporary changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                first_scratch + int(operand.immediate_indices[0]),
            )
        elif operand.operand_type == OPERAND_INPUT:
            if operand.immediate_indices == (1,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction, operand, 1
                )
            elif operand.immediate_indices == (2,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction, operand, 0
                )
            elif operand.immediate_indices != (0,):
                raise ContractError("basic-wetness template input changed")
        elif operand.operand_type == OPERAND_OUTPUT:
            if operand.immediate_indices == (0,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction, operand, 0
                )
            elif operand.immediate_indices == (1,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction, operand, 1
                )
            else:
                raise ContractError("basic-wetness template output changed")
    return replace_instruction_operands(instruction, replacements)


def remap_pbr_instruction(
    instruction: list[int], first_scratch: int, template_temp_count: int, diffuse_scale: int
) -> list[int]:
    replacements: dict[int, list[int]] = {}
    input_to_temp = {
        2: 0,  # vanilla diffuse
        3: 1,  # vanilla specular
        4: 5,  # decoded normal
        5: 2,  # view carrier; template consumes xzw
    }
    for operand in flatten_operands(executable_operands(instruction, 0, len(instruction))):
        if operand.operand_type == OPERAND_TEMP:
            if (
                len(operand.immediate_indices) != 1
                or operand.immediate_indices[0] is None
                or int(operand.immediate_indices[0]) >= template_temp_count
            ):
                raise ContractError("PBR directional template temporary changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                first_scratch + int(operand.immediate_indices[0]),
            )
        elif operand.operand_type == OPERAND_INPUT:
            if operand.immediate_indices in ((0,), (1,)):
                continue
            if (
                len(operand.immediate_indices) != 1
                or operand.immediate_indices[0] not in input_to_temp
            ):
                raise ContractError("PBR directional template input changed")
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                input_to_temp[int(operand.immediate_indices[0])],
            )
        elif operand.operand_type == OPERAND_OUTPUT:
            if operand.immediate_indices == (0,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction, operand, 0
                )
            elif operand.immediate_indices == (1,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction, operand, 1
                )
            elif operand.immediate_indices == (2,):
                replacements[operand.start] = replace_operand_with_temp(instruction, operand, diffuse_scale)
            else:
                raise ContractError("PBR directional template output changed")
    return replace_instruction_operands(instruction, replacements)


def remap_directional_diagnostic_instruction(
    instruction: list[int], first_scratch: int, template_temp_count: int
) -> list[int]:
    replacements: dict[int, list[int]] = {}
    for operand in flatten_operands(executable_operands(instruction, 0, len(instruction))):
        if operand.operand_type == OPERAND_TEMP:
            if (
                len(operand.immediate_indices) != 1
                or operand.immediate_indices[0] is None
                or int(operand.immediate_indices[0]) >= template_temp_count
            ):
                raise ContractError(
                    "directional diagnostic template temporary changed"
                )
            replacements[operand.start] = replace_operand_with_temp(
                instruction,
                operand,
                first_scratch + int(operand.immediate_indices[0]),
            )
        elif operand.operand_type == OPERAND_INPUT:
            if operand.immediate_indices == (1,):
                continue
            if operand.immediate_indices == (2,):
                replacements[operand.start] = replace_operand_with_temp(
                    instruction,
                    operand,
                    5,
                )
            else:
                raise ContractError(
                    "directional diagnostic template input changed"
                )
        elif operand.operand_type == OPERAND_OUTPUT:
            if operand.immediate_indices != (0,):
                raise ContractError(
                    "directional diagnostic template output changed"
                )
    return replace_instruction_operands(instruction, replacements)


def temp_mask(register: int, mask: int) -> list[int]:
    return [0x00100002 | (mask << 4), register]


def temp_swizzle(register: int, swizzle: int) -> list[int]:
    return [0x00100000 | swizzle, register]


def temp_scalar(register: int, component: int = 0) -> list[int]:
    return [0x0010000A | (component << 4), register]


def multiply_rgb(register: int, visibility_scratch: int) -> list[int]:
    result = [
        MUL_OPCODE,
        *temp_mask(register, 0x7),
        *temp_swizzle(register, 0x246),
        *temp_scalar(visibility_scratch),
    ]
    result[0] |= len(result) << 24
    return result


def output_contract(
    words: list[int], body: list[tuple[int, int]], register: int, opcode: int
) -> tuple[int, int]:
    matches: list[tuple[int, int]] = []
    for start, end in body:
        if (words[start] & 0x7FF) != opcode:
            continue
        operands = executable_operands(words, start, end)
        if (
            operands
            and operands[0].operand_type == OPERAND_OUTPUT
            and operands[0].immediate_indices == (register,)
        ):
            matches.append((start, end))
    if len(matches) != 1:
        raise ContractError(
            f"directional DFLight output o{register} contract changed"
        )
    return matches[0]


def unique_sequence_offset(
    words: list[int], sequence: tuple[int, ...]
) -> int:
    matches = [
        offset
        for offset in range(0, len(words) - len(sequence) + 1)
        if tuple(words[offset : offset + len(sequence)]) == sequence
    ]
    if len(matches) != 1:
        raise ContractError(
            "directional DFLight final-output contract changed"
        )
    return matches[0]


def patch_directional_diagnostic(
    original: bytes,
    mode_index: int,
    templates: tuple[bytes, bytes, bytes],
) -> bytes:
    if mode_index < 0 or mode_index >= DIRECTIONAL_DIAGNOSTIC_MODE_COUNT:
        raise ContractError("directional diagnostic mode is invalid")
    version, chunks, shader_index, words = shader_words(original)
    output_offset = unique_sequence_offset(
        words,
        STOCK_DIRECTIONAL_FINAL_OUTPUTS,
    )
    if mode_index < 3:
        diagnostic = directional_diagnostic_outputs(mode_index)
        words[output_offset : output_offset + len(diagnostic)] = diagnostic
        if tuple(
            words[output_offset : output_offset + len(diagnostic)]
        ) != diagnostic:
            raise ContractError(
                "directional diagnostic output was not installed"
            )
    else:
        temp_declaration, _, _ = shader_declarations_and_body(words)
        original_temps = words[temp_declaration[0] + 1]
        if original_temps <= 5 or original_temps > 4096:
            raise ContractError(
                "directional diagnostic source temporary contract changed"
            )
        transform, template_temps = directional_diagnostic_template_contract(
            templates[mode_index - 3]
        )
        transformed: list[int] = []
        for instruction in transform:
            transformed.extend(
                remap_directional_diagnostic_instruction(
                    instruction,
                    original_temps,
                    template_temps,
                )
            )
        words[temp_declaration[0] + 1] = original_temps + template_temps
        words[output_offset:] = [*transformed, 0x0100003E]
        words[1] = len(words)
    if any(
        tuple(words[offset : offset + len(STOCK_DIRECTIONAL_FINAL_OUTPUTS)])
        == STOCK_DIRECTIONAL_FINAL_OUTPUTS
        for offset in range(
            0,
            len(words) - len(STOCK_DIRECTIONAL_FINAL_OUTPUTS) + 1,
        )
    ):
        raise ContractError("stock directional output survived diagnostics")

    patched_chunks = list(chunks)
    patched_chunks[shader_index] = DxbcChunk(
        patched_chunks[shader_index].tag,
        pack_words(words),
    )
    return build_dxbc(version, patched_chunks)


def compile_skylighting_template(root: Path, fxc: Path, temporary: Path) -> bytes:
    output = temporary / "SkylightingDirectionalTransform.dxbc"
    source = root / "package/Shaders/Community/Skylighting/SkylightingDirectionalTransform.hlsl"
    run([str(fxc), "/nologo", "/T", "ps_5_0", "/E", "PSMain", "/O3", "/Ges", "/WX", "/Fo", str(output), str(source)],
        "Skylighting directional compilation")
    return output.read_bytes()


def sun_shadow_layout(original: bytes) -> int:
    _, _, _, words = shader_words(original)
    _, _, body = shader_declarations_and_body(words)
    for start, end in body:
        if (words[start] & 0x7FF) != MOV_OPCODE: continue
        operands = executable_operands(words, start, end)
        if len(operands) != 2: continue
        destination, source = operands
        if (destination.operand_type == OPERAND_TEMP and (words[destination.start] & 0xFFF) == 0x42 and
            source.operand_type == OPERAND_CONSTANT_BUFFER and source.immediate_indices == (2, 11) and
            (words[source.start] & 0xFFF) == 0x1A):
            return 1  # one projected slice, selected by native cb2[11].y
    return 2  # split thresholds cb2[11]/[12], native slices zero and one


def sun_template_contract(template: bytes):
    _, _, _, words = shader_words(template)
    declarations = []
    resources=set()
    for start, end in instructions(words):
        opcode=words[start]&0x7ff
        if opcode not in (OPCODE_DCL_RESOURCE,OPCODE_DCL_SAMPLER): continue
        operand=executable_operands(words,start,end)[0]
        slot=operand.immediate_indices[0]
        if opcode==OPCODE_DCL_RESOURCE and slot in (52,53,54,65,66,76,77):
            declarations.append(words[start:end]);resources.add(slot)
        elif opcode==OPCODE_DCL_SAMPLER and slot==15: declarations.append(words[start:end])
    if resources!={52,53,54,65,66,76,77} or len(declarations)!=8: raise ContractError("Directional sky/water resource footprint changed")
    temps, _, body = shader_declarations_and_body(words)
    if (words[body[-1][0]] & 0x7FF) != OPCODE_RET: raise ContractError("Skylighting sun return changed")
    return declarations, [words[a:b] for a,b in body[:-1]], words[temps[0] + 1]


def remap_sun_instruction(instruction, first_temp, rim, transmission, diffuse_scale, contact, output):
    replacements = {}
    for operand in flatten_operands(executable_operands(instruction, 0, len(instruction))):
        if operand.operand_type == OPERAND_TEMP:
            replacements[operand.start] = replace_operand_with_temp(instruction, operand, first_temp + operand.immediate_indices[0])
        elif operand.operand_type == OPERAND_OUTPUT:
            if operand.immediate_indices not in ((0,),(1,)): raise ContractError("Directional sky/water output changed")
            replacements[operand.start] = replace_operand_with_temp(instruction, operand, output+operand.immediate_indices[0])
        elif operand.operand_type == OPERAND_INPUT and operand.immediate_indices[0] >= 2:
            index = operand.immediate_indices[0]
            register = {2:5, 3:rim, 4:transmission, 5:diffuse_scale, 6:0, 7:contact}.get(index)
            if register is None: raise ContractError("Skylighting sun input changed")
            replacement = replace_operand_with_temp(instruction, operand, register)
            if index == 6:
                mode = (replacement[0] >> 2) & 3
                if mode == 2: replacement[0] = (replacement[0] & ~(3 << 4)) | (3 << 4)
                elif mode == 1: replacement[0] = (replacement[0] & ~(255 << 4)) | (255 << 4)
                else: raise ContractError("native shadow input selection changed")
            replacements[operand.start] = replacement
    return replace_instruction_operands(instruction, replacements)


def sun_lobe_sites(words, body, wrapped_site, output_site):
    hard, back, forward = [], [], []
    for start, end in body:
        if (words[start] & 0x7FF) != 0x32 or not (wrapped_site[0] - 30 < start < output_site[0]): continue
        operands = executable_operands(words, start, end)
        if len(operands) != 4: continue
        destination, a, b, addend = operands
        if destination.operand_type != OPERAND_TEMP or destination.immediate_indices != (0,) or (words[destination.start] & 0xFFF) != 0x72: continue
        if addend.operand_type != OPERAND_TEMP or addend.immediate_indices != (0,): continue
        if a.operand_type == OPERAND_CONSTANT_BUFFER and a.immediate_indices == (2, 3):
            if b.operand_type == OPERAND_TEMP and (words[b.start] & 0xFFF) == 0xFF6: hard.append((start,end))
            elif b.operand_type == OPERAND_TEMP and b.immediate_indices == (1,): back.append((start,end))
        elif a.operand_type == OPERAND_TEMP and a.immediate_indices == (1,) and b.operand_type == OPERAND_TEMP:
            forward.append((start,end))
    if len(hard)!=1 or len(back)!=1 or len(forward)!=1: raise ContractError("native soft-sun lobe decomposition changed")
    return hard[0], back[0], forward[0]


def record_transmission(instruction, register, accumulate):
    operands = executable_operands(instruction, 0, len(instruction))
    result = [0x32 if accumulate else MUL_OPCODE, *temp_mask(register,7),
        *instruction[operands[1].start:operands[1].end], *instruction[operands[2].start:operands[2].end]]
    if accumulate: result.extend(temp_swizzle(register,0x246))
    result[0] |= len(result) << 24
    return result


def patch_shader(
    original: bytes,
    contact_template: bytes,
    wrapped_grass_template: bytes,
    hair_specular_template: bytes,
    basic_wetness_template: bytes,
    pbr_template: bytes,
    skylighting_template: bytes,
) -> bytes:
    version, chunks, shader_index, words = shader_words(original)
    temp_declaration, _, body = shader_declarations_and_body(words)
    original_temps = words[temp_declaration[0] + 1]
    if original_temps == 0 or original_temps > 4096:
        raise ContractError(
            f"directional DFLight temporary register count is invalid: {original_temps}"
        )
    declarations, transform, template_temps = template_contract(
        contact_template
    )
    wrapped_declarations, wrapped_transform, wrapped_template_temps = (
        wrapped_grass_template_contract(wrapped_grass_template)
    )
    hair_declarations, hair_transform, hair_template_temps = (
        hair_specular_template_contract(hair_specular_template)
    )
    wetness_declarations, wetness_transform, wetness_template_temps = (
        basic_wetness_template_contract(basic_wetness_template)
    )
    pbr_declarations, pbr_transform, pbr_template_temps = (
        pbr_template_contract(pbr_template)
    )
    for start, end in instructions(words):
        if (words[start] & 0x7FF) != OPCODE_DCL_RESOURCE:
            continue
        operands = executable_operands(words, start, end)
        if (
            operands
            and operands[0].operand_type == OPERAND_RESOURCE
            and operands[0].immediate_indices == (CONTACT_MASK_SLOT,)
        ):
            raise ContractError("directional DFLight unexpectedly owns t46")
    original_resource_slots = {
        int(operands[0].immediate_indices[0])
        for start, end in instructions(words)
        if (words[start] & 0x7FF) == OPCODE_DCL_RESOURCE
        if (operands := executable_operands(words, start, end))
        if operands[0].operand_type == OPERAND_RESOURCE
        and len(operands[0].immediate_indices) == 1
        and operands[0].immediate_indices[0] is not None
    }
    if 0 not in original_resource_slots:
        raise ContractError(
            "directional DFLight lost the G-buffer material resource at t0"
        )
    original_constant_slots = {
        int(operands[0].immediate_indices[0])
        for start, end in instructions(words)
        if (words[start] & 0x7FF) == OPCODE_DCL_CONSTANT_BUFFER
        if (operands := executable_operands(words, start, end))
        if operands[0].operand_type == OPERAND_CONSTANT_BUFFER
        and operands[0].immediate_indices
        and operands[0].immediate_indices[0] is not None
    }
    if PBR_CONSTANT_SLOT in original_constant_slots:
        raise ContractError(
            "directional DFLight unexpectedly owns the private PBR b7 slot"
        )
    original_samplers={executable_operands(words,a,b)[0].immediate_indices[0] for a,b in instructions(words) if words[a]&0x7ff==OPCODE_DCL_SAMPLER}
    if 15 in original_samplers: raise ContractError("Native directional shader owns water sampler s15")
    if original_resource_slots & {52, 53, 54, 65, 66, 76, 77}:
        raise ContractError("native directional shader owns a private Skylighting resource slot")
    first_scratch = original_temps
    visibility_scratch = first_scratch + template_temps
    wrapped_first_scratch = visibility_scratch + 1
    hair_first_scratch = wrapped_first_scratch + wrapped_template_temps
    wetness_first_scratch = hair_first_scratch + hair_template_temps
    pbr_first_scratch = wetness_first_scratch + wetness_template_temps
    sun_declarations, sun_transform, sun_temps = sun_template_contract(skylighting_template)
    diffuse_scale_scratch = pbr_first_scratch + pbr_template_temps
    rim_scratch = diffuse_scale_scratch + 1
    transmission_scratch = rim_scratch + 1
    sun_first_scratch = transmission_scratch + 1
    sun_delta_scratch = sun_first_scratch + sun_temps


    o1_write = output_contract(words, body, 1, MUL_OPCODE)
    o0_write = output_contract(words, body, 0, DIV_OPCODE)
    transformed: list[int] = []
    for instruction in transform:
        transformed.extend(
            remap_transform_instruction(
                instruction,
                first_scratch,
                template_temps,
                visibility_scratch,
            )
        )

    transformed_wrapped: list[int] = []
    for instruction in wrapped_transform:
        transformed_wrapped.extend(
            remap_wrapped_grass_instruction(
                instruction,
                wrapped_first_scratch,
                wrapped_template_temps,
            )
        )

    transformed_hair: list[int] = []
    for instruction in hair_transform:
        transformed_hair.extend(
            remap_hair_specular_instruction(
                instruction,
                hair_first_scratch,
                hair_template_temps,
            )
        )

    transformed_wetness: list[int] = []
    for instruction in wetness_transform:
        transformed_wetness.extend(
            remap_basic_wetness_instruction(
                instruction,
                wetness_first_scratch,
                wetness_template_temps,
            )
        )

    transformed_pbr: list[int] = []
    for instruction in pbr_transform:
        transformed_pbr.extend(
            remap_pbr_instruction(
                instruction,
                pbr_first_scratch,
                pbr_template_temps,
                diffuse_scale_scratch,
            )
        )

    wrapped_sites = [
        (start, end)
        for start, end in body
        if tuple(words[start:end]) == WRAPPED_NDOTL_INSTRUCTION
    ]
    if len(wrapped_sites) != 1:
        raise ContractError(
            "directional DFLight wrapped NdotL instruction changed"
        )
    wrapped_site = wrapped_sites[0]

    hard_site, back_site, forward_site = sun_lobe_sites(words, body, wrapped_site, o1_write)
    transformed_sun = []
    for instruction in sun_transform:
        transformed_sun.extend(remap_sun_instruction(instruction, sun_first_scratch, rim_scratch,
            transmission_scratch, diffuse_scale_scratch, visibility_scratch, sun_delta_scratch))
    prefix = words[: temp_declaration[0]]
    for start,end in instructions(words):
        if (words[start] & 0x7FF) == OPCODE_DCL_CONSTANT_BUFFER:
            operand = executable_operands(words, start, end)[0]
            if operand.immediate_indices[0] == 12: prefix[end-1] = max(prefix[end-1],61)
    for declaration in sun_declarations: prefix.extend(declaration)
    for declaration in declarations:
        prefix.extend(declaration)
    for declaration in wrapped_declarations:
        prefix.extend(declaration)
    for declaration in hair_declarations:
        prefix.extend(declaration)
    for declaration in wetness_declarations:
        prefix.extend(declaration)
    for declaration in pbr_declarations:
        prefix.extend(declaration)
    updated_temps = words[temp_declaration[0] : temp_declaration[1]]
    updated_temps[1] = sun_delta_scratch + 2
    rewritten: list[int] = []
    for start, end in body:
        if (start, end) == hard_site:
            rewritten.extend([0x05000036, *temp_mask(rim_scratch,7), *temp_swizzle(0,0x246)])
        if (start, end) == o1_write:
            rewritten.extend(transformed_wetness)
            rewritten.extend(transformed_pbr)
            rewritten.extend(transformed_hair)
            rewritten.extend(transformed)
            rewritten.extend(transformed_sun)
            rewritten.extend(multiply_rgb(1, visibility_scratch))
            rewritten.extend([0x07000038, *temp_mask(1,7), *temp_swizzle(1,0x246), *temp_swizzle(sun_delta_scratch+1,0x246)])
        elif (start, end) == o0_write:
            rewritten.extend(multiply_rgb(0, visibility_scratch))
            rewritten.extend([0x07000000, *temp_mask(0,7), *temp_swizzle(0,0x246), *temp_swizzle(sun_delta_scratch,0x246)])
            rewritten.extend([0x07000038, *temp_mask(0,7), *temp_swizzle(0,0x246), *temp_swizzle(sun_delta_scratch+1,0x246)])
        rewritten.extend(words[start:end])
        if (start, end) == back_site: rewritten.extend(record_transmission(words[start:end], transmission_scratch, False))
        if (start, end) == forward_site: rewritten.extend(record_transmission(words[start:end], transmission_scratch, True))
        if (start, end) == wrapped_site:
            rewritten.extend(transformed_wrapped)

    patched_words = [*prefix, *updated_temps, *rewritten]
    patched_words[1] = len(patched_words)
    patched_chunks = list(chunks)
    patched_chunks[shader_index] = DxbcChunk(
        patched_chunks[shader_index].tag,
        pack_words(patched_words),
    )
    return build_dxbc(version, patched_chunks)


def compile_compute_shader(root: Path, fxc: Path, temporary: Path) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ContactShadows"
        / "ContactShadowMaskCS.hlsl"
    )
    output = temporary / "ContactShadowMaskCS.dxbc"
    assembly = temporary / "ContactShadowMaskCS.asm.txt"
    run(
        [
            str(fxc),
            "/nologo",
            "/T",
            "cs_5_0",
            "/E",
            "CSMain",
            "/O3",
            "/Ges",
            "/WX",
            "/Fo",
            str(output),
            "/Fc",
            str(assembly),
            str(source),
        ],
        "contact-shadow mask compute compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_resource_texture2d (float,float,float,float) t0",
        "dcl_resource_structured t1, 32",
        "dcl_uav_typed_texture2d (unorm,unorm,unorm,unorm) u0",
        "dcl_constantbuffer CB2[46], dynamicIndexed",
        "dcl_constantbuffer CB8[1], immediateIndexed",
        "dcl_constantbuffer CB12[51], dynamicIndexed",
        "dcl_constantbuffer CB13[3], immediateIndexed",
        "dcl_tgsm_structured g0, 4, 384",
        "dcl_tgsm_structured g1, 4, 384",
        "dcl_thread_group 64, 1, 1",
    ):
        if required not in text:
            raise ContractError(
                "contact-shadow mask compute assembly changed: " + required
            )
    return output.read_bytes()


def compile_dispatch_shader(root: Path, fxc: Path, temporary: Path) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ContactShadows"
        / "ContactShadowDispatchCS.hlsl"
    )
    output = temporary / "ContactShadowDispatchCS.dxbc"
    assembly = temporary / "ContactShadowDispatchCS.asm.txt"
    run(
        [
            str(fxc),
            "/nologo",
            "/T",
            "cs_5_0",
            "/E",
            "CSMain",
            "/O3",
            "/Ges",
            "/WX",
            "/Fo",
            str(output),
            "/Fc",
            str(assembly),
            str(source),
        ],
        "contact-shadow dispatch compute compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_constantbuffer CB2[3], immediateIndexed",
        "dcl_constantbuffer CB12[12], immediateIndexed",
        "dcl_resource_texture2d (float,float,float,float) t0",
        "dcl_uav_structured u0, 32",
        "dcl_uav_raw u1",
        "dcl_thread_group 1, 1, 1",
    ):
        if required not in text:
            raise ContractError(
                "contact-shadow dispatch compute assembly changed: " + required
            )
    return output.read_bytes()


def compile_resolve_shader(root: Path, fxc: Path, temporary: Path) -> bytes:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "ContactShadows"
        / "ContactShadowResolveCS.hlsl"
    )
    output = temporary / "ContactShadowResolveCS.dxbc"
    assembly = temporary / "ContactShadowResolveCS.asm.txt"
    run(
        [
            str(fxc),
            "/nologo",
            "/T",
            "cs_5_0",
            "/E",
            "CSMain",
            "/O3",
            "/Ges",
            "/WX",
            "/Fo",
            str(output),
            "/Fc",
            str(assembly),
            str(source),
        ],
        "contact-shadow directional resolve compilation",
    )
    text = assembly.read_text(encoding="utf-8")
    for required in (
        "dcl_resource_texture2d (float,float,float,float) t0",
        "dcl_resource_texture2d (float,float,float,float) t1",
        "dcl_resource_texturecube (float,float,float,float) t2",
        "dcl_uav_typed_texture2d (unorm,unorm,unorm,unorm) u0",
        "dcl_constantbuffer CB2[46], dynamicIndexed",
        "dcl_constantbuffer CB8[1], immediateIndexed",
        "dcl_constantbuffer CB12[48], dynamicIndexed",
        "dcl_constantbuffer CB13[4], immediateIndexed",
        "dcl_sampler s0, mode_default",
        "dcl_thread_group 8, 8, 1",
    ):
        if required not in text:
            raise ContractError(
                "contact-shadow resolve assembly changed: " + required
            )
    return output.read_bytes()


def write_header(path: Path, data: bytes, symbol: str) -> None:
    rows = [
        "#pragma once",
        "",
        f"inline constexpr unsigned char {symbol}[] = {{",
    ]
    for offset in range(0, len(data), 16):
        values = ", ".join(f"0x{value:02x}" for value in data[offset:offset + 16])
        rows.append(f"    {values},")
    rows.extend(("};", ""))
    path.write_text("\n".join(rows), encoding="utf-8")


def write_shader_family_header(
    path: Path,
    candidates: list[
        tuple[census.DxbcContainer, bytes, tuple[bytes, ...]]
    ],
) -> None:
    rows = [
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstddef>",
        "",
    ]
    symbols: list[str] = []
    diagnostic_symbols: list[tuple[str, ...]] = []
    for index, (_, data, diagnostics) in enumerate(candidates):
        symbol = f"csah_contact_shadows_dflight_{index:03d}"
        symbols.append(symbol)
        rows.append(f"inline constexpr unsigned char {symbol}[] = {{")
        for offset in range(0, len(data), 16):
            values = ", ".join(
                f"0x{value:02x}" for value in data[offset:offset + 16]
            )
            rows.append(f"    {values},")
        rows.extend(("};", ""))
        current_diagnostic_symbols: list[str] = []
        for mode_index, diagnostic in enumerate(diagnostics):
            diagnostic_symbol = (
                f"csah_directional_diagnostic_dflight_"
                f"{index:03d}_{mode_index}"
            )
            current_diagnostic_symbols.append(diagnostic_symbol)
            rows.append(
                f"inline constexpr unsigned char {diagnostic_symbol}[] = {{"
            )
            for offset in range(0, len(diagnostic), 16):
                values = ", ".join(
                    f"0x{value:02x}"
                    for value in diagnostic[offset : offset + 16]
                )
                rows.append(f"    {values},")
            rows.extend(("};", ""))
        diagnostic_symbols.append(tuple(current_diagnostic_symbols))

    rows.extend(
        (
            "struct Fo4vrCsContactShadowShaderContract",
            "{",
            "    std::size_t originalSize;",
            "    std::array<unsigned char, 16> originalChecksum;",
            "    const unsigned char* replacementBytecode;",
            "    std::size_t replacementBytecodeLength;",
            "    std::array<const unsigned char*, 6> diagnosticBytecode;",
            "    std::array<std::size_t, 6> diagnosticBytecodeLength;",
            "    unsigned sunShadowLayout;",
            "};",
            "",
            "inline constexpr std::array<",
            "    Fo4vrCsContactShadowShaderContract,",
            f"    {len(candidates)}> csah_contact_shadow_dflight_contracts{{{{",
        )
    )
    for (item, _, _), symbol, diagnostics in zip(
        candidates,
        symbols,
        diagnostic_symbols,
        strict=True,
    ):
        checksum = ", ".join(
            f"0x{value:02x}" for value in bytes.fromhex(item.checksum)
        )
        diagnostic_pointers = ", ".join(diagnostics)
        diagnostic_lengths = ", ".join(
            f"sizeof({diagnostic})" for diagnostic in diagnostics
        )
        rows.append(
            "    Fo4vrCsContactShadowShaderContract{ "
            f"{item.size}, {{ {checksum} }}, {symbol}, sizeof({symbol}), "
            f"{{ {diagnostic_pointers} }}, {{ {diagnostic_lengths} }}, {sun_shadow_layout(item.data)} }},"
        )
    rows.extend(("}};", ""))
    path.write_text("\n".join(rows), encoding="utf-8")


def main() -> int:
    args = arguments()
    root = args.root.resolve()
    args.binary.parent.mkdir(parents=True, exist_ok=True)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.compute_binary.parent.mkdir(parents=True, exist_ok=True)
    args.compute_header.parent.mkdir(parents=True, exist_ok=True)
    args.dispatch_binary.parent.mkdir(parents=True, exist_ok=True)
    args.dispatch_header.parent.mkdir(parents=True, exist_ok=True)
    args.resolve_binary.parent.mkdir(parents=True, exist_ok=True)
    args.resolve_header.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="fo4vr-contact-shadows-") as folder:
        temporary = Path(folder)
        canonical_original = original_shader(root)
        originals = directional_pixel_shaders(root)
        template = compile_template(root, args.fxc.resolve(), temporary)
        wrapped_grass_template = compile_wrapped_grass_template(
            root,
            args.fxc.resolve(),
            temporary,
        )
        hair_specular_template = compile_hair_specular_template(
            root,
            args.fxc.resolve(),
            temporary,
        )
        basic_wetness_template = compile_basic_wetness_template(
            root,
            args.fxc.resolve(),
            temporary,
        )
        pbr_template = compile_pbr_template(
            root,
            args.fxc.resolve(),
            temporary,
        )
        directional_diagnostic_templates = (
            compile_directional_diagnostic_templates(
                root,
                args.fxc.resolve(),
                temporary,
            )
        )
        skylighting_template = compile_skylighting_template(root, args.fxc.resolve(), temporary)
        compute = compile_compute_shader(root, args.fxc.resolve(), temporary)
        dispatch = compile_dispatch_shader(
            root,
            args.fxc.resolve(),
            temporary,
        )
        resolve = compile_resolve_shader(
            root,
            args.fxc.resolve(),
            temporary,
        )
        candidates: list[
            tuple[census.DxbcContainer, bytes, tuple[bytes, ...]]
        ] = []
        for original in originals:
            try:
                candidate = patch_shader(
                    original.data,
                    template,
                    wrapped_grass_template,
                    hair_specular_template,
                    basic_wetness_template,
                    pbr_template,
                    skylighting_template,
                )
                diagnostics = tuple(
                    patch_directional_diagnostic(
                        original.data,
                        mode_index,
                        directional_diagnostic_templates,
                    )
                    for mode_index in range(DIRECTIONAL_DIAGNOSTIC_MODE_COUNT)
                )
            except ContractError:
                continue
            candidates.append((original, candidate, diagnostics))

        if len(candidates) != EXPECTED_COMPATIBLE_SHADER_COUNT:
            raise ContractError(
                "structurally compatible directional coverage changed: "
                f"expected {EXPECTED_COMPATIBLE_SHADER_COUNT}, found {len(candidates)}")

        required_assembly = (
            "dcl_constantbuffer CB13[3], immediateIndexed",
            "dcl_constantbuffer CB11[1], immediateIndexed",
            "dcl_constantbuffer CB10[1], immediateIndexed",
            "dcl_constantbuffer CB9[1], immediateIndexed",
            "dcl_constantbuffer CB7[4], immediateIndexed",
            "dcl_resource_texture2d (float,float,float,float) t45",
            "dcl_resource_texture2d (float,float,float,float) t46",
            "dcl_resource_texture2d (float,float,float,float) t47",
            "l(0.212600, 0.715200, 0.072200",
            "l(0.140000)",
            "dcl_output o0.xyzw",
            "dcl_output o1.xyzw",
        )
        canonical_candidate: bytes | None = None
        for index, (original, candidate, diagnostics) in enumerate(candidates):
            candidate_path = temporary / f"ContactShadowsDFLight_{index:03d}.dxbc"
            assembly_path = temporary / f"ContactShadowsDFLight_{index:03d}.asm.txt"
            candidate_path.write_bytes(candidate)
            run(
                [
                    str(args.fxc.resolve()),
                    "/dumpbin",
                    "/nologo",
                    "/Fc",
                    str(assembly_path),
                    str(candidate_path),
                ],
                f"contact-shadow candidate {index} validation",
            )
            assembly = assembly_path.read_text(encoding="utf-8")
            for required in required_assembly:
                if required not in assembly:
                    raise ContractError(
                        f"contact-shadow candidate {index} validation failed: "
                        + required
                    )
            if sum(
                line.strip() == "ret" for line in assembly.splitlines()
            ) != 1:
                raise ContractError(
                    f"contact-shadow candidate {index} contains an injected early return"
                )
            legacy_diagnostic_signals = (
                ("mov r0.x, r5.w", "mov r0.y, l(0)", "mov r0.z, l(0)"),
                ("mov r0.x, l(0)", "mov r0.y, r8.x", "mov r0.z, l(0)"),
                ("mov r0.x, l(0)", "mov r0.y, l(0)", "mov r0.z, r0.w"),
            )
            for mode_index, diagnostic in enumerate(diagnostics):
                diagnostic_path = temporary / (
                    f"DirectionalDiagnosticDFLight_{index:03d}_"
                    f"{mode_index}.dxbc"
                )
                diagnostic_assembly_path = temporary / (
                    f"DirectionalDiagnosticDFLight_{index:03d}_"
                    f"{mode_index}.asm.txt"
                )
                diagnostic_path.write_bytes(diagnostic)
                run(
                    [
                        str(args.fxc.resolve()),
                        "/dumpbin",
                        "/nologo",
                        "/Fc",
                        str(diagnostic_assembly_path),
                        str(diagnostic_path),
                    ],
                    f"directional diagnostic candidate {index}/{mode_index} validation",
                )
                diagnostic_assembly = diagnostic_assembly_path.read_text(
                    encoding="utf-8"
                )
                required_contracts: tuple[str, ...]
                if mode_index < 3:
                    required_contracts = (
                        *legacy_diagnostic_signals[mode_index],
                        "mov r0.w, l(3.000000)",
                        "div o0.xyzw, r0.xyzw, l(3.000000, 3.000000, 3.000000, 3.000000)",
                    )
                else:
                    required_contracts = (
                        "dcl_constantbuffer CB2[46], dynamicIndexed",
                        "dcl_input_ps constant v1.x",
                    )
                    if mode_index < 5:
                        required_contracts += (
                            "mad ",
                            "l(0.500000, 0.500000, 0.500000, 0.000000)",
                            "l(0.333333, 0.333333, 0.333333, 0.000000)",
                            "mov o0.w, l(1.000000)",
                        )
                    if mode_index >= 4:
                        required_contracts += (
                            "cb12[0].xyzx",
                            "cb12[1].xyzx",
                            "cb12[2].xyzx",
                        )
                    if mode_index == 5:
                        required_contracts += (
                            "dp3_sat",
                            "mov o0.yzw, l(0,0,0,1.000000)",
                        )
                for required in required_contracts:
                    if required not in diagnostic_assembly:
                        raise ContractError(
                            f"directional diagnostic candidate {index}/{mode_index} "
                            f"validation failed: {required}"
                        )
                if sum(
                    line.strip() == "ret"
                    for line in diagnostic_assembly.splitlines()
                ) != 1:
                    raise ContractError(
                        f"directional diagnostic candidate {index}/{mode_index} "
                        "does not own exactly one return"
                    )
            if original.data == canonical_original:
                canonical_candidate = candidate

        if canonical_candidate is None:
            raise ContractError(
                "canonical directional DFLight candidate left the compatible family"
            )
        args.binary.write_bytes(canonical_candidate)
        write_shader_family_header(args.header, candidates)
        args.compute_binary.write_bytes(compute)
        write_header(
            args.compute_header,
            compute,
            "csah_contact_shadow_mask",
        )
        args.dispatch_binary.write_bytes(dispatch)
        write_header(
            args.dispatch_header,
            dispatch,
            "csah_contact_shadow_dispatch",
        )
        args.resolve_binary.write_bytes(resolve)
        write_header(
            args.resolve_header,
            resolve,
            "csah_contact_shadow_resolve",
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ContractError, OSError) as error:
        print(f"contact-shadow generation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
