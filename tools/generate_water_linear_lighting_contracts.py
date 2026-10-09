from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

import census_linear_lighting_fxp as census
import water_parallax_transform
import water_reflection_transform


class ContractError(RuntimeError):
    pass


EXPECTED_WATER_SLOT_COUNT = 92
EXPECTED_WATER_IDENTITY_COUNT = 38
EXPECTED_WATER_CONTRACT_COUNT = 31
EXPECTED_WATER_CONTRACT_DESCRIPTORS = {
    0x00000000,
    0x0000001C,
    0x0000003C,
    0x0000005E,
    0x0000007E,
    0x0000009F,
    0x000000BF,
    0x0000021C,
    0x0000023C,
    0x0000025E,
    0x0000027E,
    0x0000029F,
    0x000002BF,
    0x00001002,
    0x0000105E,
    0x0000109F,
    0x0000121E,
    0x0000125E,
    0x00001A8F,
    0x00002002,
    0x00003002,
    0x00004002,
    0x00006002,
    0x00008002,
    0x00009000,
    0x0000A002,
    0x0000C002,
    0x00010000,
    0x00010204,
    0x00018010,
    0x00020204,
}
EXPECTED_WATER_ATMOSPHERIC_FOG_DESCRIPTORS = {
    0x00000000,
    0x0000001C,
    0x0000003C,
    0x0000005E,
    0x0000007E,
    0x0000009F,
    0x000000BF,
    0x0000021C,
    0x0000023C,
    0x0000025E,
    0x0000027E,
    0x0000029F,
    0x000002BF,
    0x00010000,
    0x00010204,
    0x00020204,
}

DXBC_HEADER_SIZE = 32
DXBC_CHECKSUM_OFFSET = 4
DXBC_PAYLOAD_OFFSET = 20
DXBC_SIZE_OFFSET = 24
DXBC_CHUNK_COUNT_OFFSET = 28
DXBC_CHUNK_OFFSETS_OFFSET = 32

OPCODE_DCL_CONSTANT_BUFFER = 0x59
OPCODE_DCL_TEMPS = 0x68
OPCODE_MAD = 0x32
OPCODE_RET = 0x3E

OPERAND_TEMP = 0
OPERAND_OUTPUT = 2
OPERAND_IMMEDIATE32 = 4
OPERAND_CONSTANT_BUFFER = 8


@dataclass(frozen=True)
class DxbcChunk:
    tag: bytes
    payload: bytes


@dataclass(frozen=True)
class Operand:
    start: int
    end: int
    operand_type: int
    immediate_indices: tuple[int | None, ...]
    index_offsets: tuple[int | None, ...]


@dataclass(frozen=True)
class WaterDomainUsage:
    shallow_references: int
    deep_references: int
    sun_references: int
    fog_near_references: int
    fog_far_references: int
    point_light_references: int
    atmospheric_fog: bool

    @property
    def shallow_deep(self) -> bool:
        return self.shallow_references != 0 or self.deep_references != 0

    @property
    def sun(self) -> bool:
        return self.sun_references != 0

    @property
    def fog(self) -> bool:
        return self.fog_near_references != 0 or self.fog_far_references != 0

    @property
    def point_light(self) -> bool:
        return self.point_light_references != 0

    @property
    def active(self) -> bool:
        return (
            self.shallow_deep
            or self.sun
            or self.fog
            or self.point_light
            or self.atmospheric_fog
        )


@dataclass(frozen=True)
class AtmosphericFogBlend:
    instruction_start: int


@dataclass(frozen=True)
class TransformTemplate:
    temporary_count: int
    instructions: tuple[tuple[int, ...], ...]


def u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def pack_words(words: list[int]) -> bytes:
    return struct.pack(f"<{len(words)}I", *words)


def parse_dxbc(data: bytes) -> tuple[int, list[DxbcChunk]]:
    if len(data) < DXBC_HEADER_SIZE or data[:4] != b"DXBC":
        raise ContractError("input is not a DXBC container")
    if u32(data, DXBC_SIZE_OFFSET) != len(data):
        raise ContractError("DXBC declared size does not match its bytes")
    version = u32(data, 20)
    chunk_count = u32(data, DXBC_CHUNK_COUNT_OFFSET)
    header_size = DXBC_CHUNK_OFFSETS_OFFSET + chunk_count * 4
    if chunk_count == 0 or header_size > len(data):
        raise ContractError("DXBC chunk table is invalid")

    chunks: list[DxbcChunk] = []
    occupied: list[tuple[int, int]] = []
    for index in range(chunk_count):
        offset = u32(data, DXBC_CHUNK_OFFSETS_OFFSET + index * 4)
        if offset < header_size or offset + 8 > len(data):
            raise ContractError("DXBC chunk offset is invalid")
        size = u32(data, offset + 4)
        end = offset + 8 + size
        if end > len(data):
            raise ContractError("DXBC chunk escapes its container")
        occupied.append((offset, end))
        chunks.append(DxbcChunk(data[offset : offset + 4], data[offset + 8 : end]))
    for (_, previous_end), (current_start, _) in zip(
        sorted(occupied), sorted(occupied)[1:]
    ):
        if current_start < previous_end:
            raise ContractError("DXBC chunks overlap")
    return version, chunks


ROTATION_COUNTS = (
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
)
ROUND_CONSTANTS = (
    0xD76AA478, 0xE8C7B756, 0x242070DB, 0xC1BDCEEE,
    0xF57C0FAF, 0x4787C62A, 0xA8304613, 0xFD469501,
    0x698098D8, 0x8B44F7AF, 0xFFFF5BB1, 0x895CD7BE,
    0x6B901122, 0xFD987193, 0xA679438E, 0x49B40821,
    0xF61E2562, 0xC040B340, 0x265E5A51, 0xE9B6C7AA,
    0xD62F105D, 0x02441453, 0xD8A1E681, 0xE7D3FBC8,
    0x21E1CDE6, 0xC33707D6, 0xF4D50D87, 0x455A14ED,
    0xA9E3E905, 0xFCEFA3F8, 0x676F02D9, 0x8D2A4C8A,
    0xFFFA3942, 0x8771F681, 0x6D9D6122, 0xFDE5380C,
    0xA4BEEA44, 0x4BDECFA9, 0xF6BB4B60, 0xBEBFBC70,
    0x289B7EC6, 0xEAA127FA, 0xD4EF3085, 0x04881D05,
    0xD9D4D039, 0xE6DB99E5, 0x1FA27CF8, 0xC4AC5665,
    0xF4292244, 0x432AFF97, 0xAB9423A7, 0xFC93A039,
    0x655B59C3, 0x8F0CCC92, 0xFFEFF47D, 0x85845DD1,
    0x6FA87E4F, 0xFE2CE6E0, 0xA3014314, 0x4E0811A1,
    0xF7537E82, 0xBD3AF235, 0x2AD7D2BB, 0xEB86D391,
)


def rotate_left(value: int, count: int) -> int:
    return ((value << count) | (value >> (32 - count))) & 0xFFFFFFFF


def checksum_transform(state: list[int], block: bytes) -> None:
    words = struct.unpack("<16I", block)
    a, b, c, d = state
    for index in range(64):
        if index < 16:
            function = (b & c) | ((~b) & d)
            word_index = index
        elif index < 32:
            function = (d & b) | ((~d) & c)
            word_index = (5 * index + 1) % 16
        elif index < 48:
            function = b ^ c ^ d
            word_index = (3 * index + 5) % 16
        else:
            function = c ^ (b | (~d))
            word_index = (7 * index) % 16
        previous_d = d
        d = c
        c = b
        value = (
            a + function + ROUND_CONSTANTS[index] + words[word_index]
        ) & 0xFFFFFFFF
        b = (b + rotate_left(value, ROTATION_COUNTS[index])) & 0xFFFFFFFF
        a = previous_d
    state[0] = (state[0] + a) & 0xFFFFFFFF
    state[1] = (state[1] + b) & 0xFFFFFFFF
    state[2] = (state[2] + c) & 0xFFFFFFFF
    state[3] = (state[3] + d) & 0xFFFFFFFF


def compute_dxbc_checksum(payload: bytes) -> bytes:
    state = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476]
    complete_blocks = len(payload) // 64
    for index in range(complete_blocks):
        checksum_transform(state, payload[index * 64 : (index + 1) * 64])

    remainder = payload[complete_blocks * 64 :]
    final_block = bytearray(64)
    if len(remainder) < 56:
        struct.pack_into("<I", final_block, 0, len(payload) * 8)
        final_block[4 : 4 + len(remainder)] = remainder
        final_block[4 + len(remainder)] = 0x80
    else:
        final_block[: len(remainder)] = remainder
        final_block[len(remainder)] = 0x80
        checksum_transform(state, bytes(final_block))
        final_block = bytearray(64)
        struct.pack_into("<I", final_block, 0, len(payload) * 8)
    struct.pack_into("<I", final_block, 60, len(payload) * 2 | 1)
    checksum_transform(state, bytes(final_block))
    return struct.pack("<4I", *state)


def build_dxbc(version: int, chunks: list[DxbcChunk]) -> bytes:
    header_size = DXBC_CHUNK_OFFSETS_OFFSET + len(chunks) * 4
    output = bytearray(header_size)
    output[:4] = b"DXBC"
    struct.pack_into("<I", output, 20, version)
    struct.pack_into("<I", output, DXBC_CHUNK_COUNT_OFFSET, len(chunks))
    offsets: list[int] = []
    for chunk in chunks:
        while len(output) % 4:
            output.append(0)
        offsets.append(len(output))
        output.extend(chunk.tag)
        output.extend(struct.pack("<I", len(chunk.payload)))
        output.extend(chunk.payload)
    for index, offset in enumerate(offsets):
        struct.pack_into("<I", output, DXBC_CHUNK_OFFSETS_OFFSET + index * 4, offset)
    struct.pack_into("<I", output, DXBC_SIZE_OFFSET, len(output))
    output[DXBC_CHECKSUM_OFFSET : DXBC_CHECKSUM_OFFSET + 16] = (
        compute_dxbc_checksum(bytes(output[DXBC_PAYLOAD_OFFSET:]))
    )
    return bytes(output)


def shader_words(data: bytes) -> tuple[int, list[DxbcChunk], int, list[int]]:
    version, chunks = parse_dxbc(data)
    shader_indices = [
        index for index, chunk in enumerate(chunks) if chunk.tag in (b"SHEX", b"SHDR")
    ]
    if len(shader_indices) != 1:
        raise ContractError("DXBC must contain exactly one shader program")
    shader_index = shader_indices[0]
    payload = chunks[shader_index].payload
    if len(payload) < 8 or len(payload) % 4:
        raise ContractError("DXBC shader program is truncated")
    words = list(struct.unpack(f"<{len(payload) // 4}I", payload))
    if words[1] != len(words):
        raise ContractError("DXBC shader token length is invalid")
    return version, chunks, shader_index, words


def instructions(words: list[int]) -> list[tuple[int, int]]:
    result: list[tuple[int, int]] = []
    cursor = 2
    while cursor < len(words):
        length = (words[cursor] >> 24) & 0x7F
        if length == 0 or cursor + length > len(words):
            raise ContractError("DXBC instruction length is invalid")
        result.append((cursor, cursor + length))
        cursor += length
    if cursor != len(words):
        raise ContractError("DXBC instruction stream did not terminate exactly")
    return result


def opcode_extension_end(words: list[int], start: int, end: int) -> int:
    cursor = start + 1
    extended = (words[start] & 0x80000000) != 0
    while extended:
        if cursor >= end:
            raise ContractError("DXBC extended opcode is truncated")
        extended = (words[cursor] & 0x80000000) != 0
        cursor += 1
    return cursor


def parse_operand(words: list[int], start: int, end: int) -> Operand:
    if start >= end:
        raise ContractError("DXBC operand is truncated")
    token = words[start]
    operand_type = (token >> 12) & 0xFF
    component_count = token & 0x3
    index_dimension = (token >> 20) & 0x3
    cursor = start + 1

    extended = (token & 0x80000000) != 0
    while extended:
        if cursor >= end:
            raise ContractError("DXBC extended operand is truncated")
        extended = (words[cursor] & 0x80000000) != 0
        cursor += 1

    if operand_type == OPERAND_IMMEDIATE32:
        if index_dimension != 0:
            raise ContractError("DXBC immediate operand has indices")
        if component_count == 1:
            cursor += 1
        elif component_count == 2:
            cursor += 4
        elif component_count == 3:
            if cursor >= end:
                raise ContractError("DXBC N-component immediate is truncated")
            count = words[cursor]
            cursor += 1 + count
        if cursor > end:
            raise ContractError("DXBC immediate operand escapes its instruction")
        return Operand(start, cursor, operand_type, (), ())

    indices: list[int | None] = []
    offsets: list[int | None] = []
    for dimension in range(index_dimension):
        representation = (token >> (22 + dimension * 3)) & 0x7
        if representation == 0:
            if cursor >= end:
                raise ContractError("DXBC immediate index is truncated")
            indices.append(words[cursor])
            offsets.append(words[cursor])
            cursor += 1
        elif representation == 1:
            if cursor + 2 > end:
                raise ContractError("DXBC 64-bit index is truncated")
            indices.append(None)
            offsets.append(None)
            cursor += 2
        elif representation == 2:
            relative = parse_operand(words, cursor, end)
            indices.append(None)
            offsets.append(0)
            cursor = relative.end
        elif representation in (3, 4):
            immediate_words = 1 if representation == 3 else 2
            if cursor + immediate_words > end:
                raise ContractError("DXBC relative base index is truncated")
            offset = words[cursor] if immediate_words == 1 else None
            cursor += immediate_words
            relative = parse_operand(words, cursor, end)
            indices.append(None)
            offsets.append(offset)
            cursor = relative.end
        else:
            raise ContractError("DXBC operand uses an unsupported index representation")
    return Operand(
        start,
        cursor,
        operand_type,
        tuple(indices),
        tuple(offsets),
    )


def executable_operands(
    words: list[int], instruction_start: int, instruction_end: int
) -> list[Operand]:
    cursor = opcode_extension_end(words, instruction_start, instruction_end)
    result: list[Operand] = []
    while cursor < instruction_end:
        operand = parse_operand(words, cursor, instruction_end)
        if operand.end <= cursor:
            raise ContractError("DXBC operand parser made no progress")
        result.append(operand)
        cursor = operand.end
    if cursor != instruction_end:
        raise ContractError("DXBC operands did not fill their instruction")
    return result


def shader_declarations_and_body(
    words: list[int],
) -> tuple[tuple[int, int], int, list[tuple[int, int]]]:
    all_instructions = instructions(words)
    temp_declarations = [
        item for item in all_instructions if (words[item[0]] & 0x7FF) == OPCODE_DCL_TEMPS
    ]
    if len(temp_declarations) != 1:
        raise ContractError("DXBC shader must declare one temporary-register block")
    temp_declaration = temp_declarations[0]
    if temp_declaration[1] - temp_declaration[0] != 2:
        raise ContractError("DXBC temporary-register declaration changed shape")
    executable_start = temp_declaration[1]
    body = [item for item in all_instructions if item[0] >= executable_start]
    if not body or body[0][0] != executable_start:
        raise ContractError("DXBC declarations follow dcl_temps")
    return temp_declaration, executable_start, body


def constant_buffer_register(operand: Operand) -> tuple[int, int] | None:
    if (
        operand.operand_type != OPERAND_CONSTANT_BUFFER
        or len(operand.immediate_indices) != 2
        or operand.immediate_indices[0] is None
        or len(operand.index_offsets) != 2
        or operand.index_offsets[1] is None
    ):
        return None
    return int(operand.immediate_indices[0]), int(operand.index_offsets[1])


def water_operand_domain(operand: Operand) -> str | None:
    register = constant_buffer_register(operand)
    if register == (1, 0):
        return "shallow"
    if register == (1, 1):
        return "deep"
    if register == (0, 2):
        return "sun"
    if register == (1, 6):
        return "fog_near"
    if register == (1, 7):
        return "fog_far"
    if register == (2, 20):
        return "point_light"
    return None


def operand_output_mask(words: list[int], operand: Operand) -> int | None:
    if (
        operand.operand_type != OPERAND_OUTPUT
        or operand.immediate_indices != (0,)
        or (words[operand.start] & 0x3) != 2
        or ((words[operand.start] >> 2) & 0x3) != 0
    ):
        return None
    return (words[operand.start] >> 4) & 0xF


def operand_is_scalar_temp(words: list[int], operand: Operand) -> bool:
    if (
        operand.operand_type != OPERAND_TEMP
        or len(operand.immediate_indices) != 1
        or operand.immediate_indices[0] is None
        or (words[operand.start] & 0x3) != 2
    ):
        return False
    selection_mode = (words[operand.start] >> 2) & 0x3
    if selection_mode == 2:
        return True
    if selection_mode != 1:
        return False
    swizzle = (words[operand.start] >> 4) & 0xFF
    components = tuple((swizzle >> (index * 2)) & 0x3 for index in range(4))
    return len(set(components)) == 1


def operand_selects_only_component(
    words: list[int], operand: Operand, component: int
) -> bool:
    if not 0 <= component <= 3 or (words[operand.start] & 0x3) != 2:
        return False
    selection_mode = (words[operand.start] >> 2) & 0x3
    if selection_mode == 2:
        return ((words[operand.start] >> 4) & 0x3) == component
    if selection_mode != 1:
        return False
    swizzle = (words[operand.start] >> 4) & 0xFF
    return all(
        ((swizzle >> (index * 2)) & 0x3) == component
        for index in range(4)
    )


def atmospheric_fog_blend(words: list[int]) -> AtmosphericFogBlend | None:
    atmospheric_registers: set[int] = set()
    last_atmospheric_reference = -1
    for start, end in instructions(words):
        if (words[start] & 0x7FF) in (
            OPCODE_DCL_CONSTANT_BUFFER,
            OPCODE_DCL_TEMPS,
            OPCODE_RET,
        ):
            continue
        for operand in executable_operands(words, start, end):
            register = constant_buffer_register(operand)
            if register is None or register[0] != 12:
                continue
            if 71 <= register[1] <= 76:
                atmospheric_registers.add(register[1])
                last_atmospheric_reference = start

    if not atmospheric_registers:
        return None
    _, _, body = shader_declarations_and_body(words)
    if atmospheric_registers != set(range(71, 77)):
        raise ContractError(
            "Water atmospheric-fog frame contract changed: "
            + repr(sorted(atmospheric_registers))
        )

    candidates: list[int] = []
    for start, end in body:
        if (words[start] & 0x7FF) != OPCODE_MAD:
            continue
        operands = executable_operands(words, start, end)
        if (
            len(operands) == 4
            and operand_output_mask(words, operands[0]) == 0x7
            and operand_is_scalar_temp(words, operands[1])
            and operands[2].operand_type == OPERAND_TEMP
            and operands[3].operand_type == OPERAND_TEMP
            and start > last_atmospheric_reference
        ):
            candidates.append(start)
    if len(candidates) != 1:
        raise ContractError(
            "Water atmospheric-fog output blend changed shape: "
            + repr(candidates)
        )

    blend_start = candidates[0]
    for start, end in body:
        if start <= blend_start or (words[start] & 0x7FF) == OPCODE_RET:
            continue
        operands = executable_operands(words, start, end)
        if operands and operand_output_mask(words, operands[0]) in (1, 2, 3, 4, 5, 6, 7):
            raise ContractError(
                "Water atmospheric-fog blend is not the terminal RGB write"
            )
    return AtmosphericFogBlend(blend_start)


def water_domain_usage(data: bytes) -> WaterDomainUsage:
    _, _, _, words = shader_words(data)
    counts = {
        "shallow": 0,
        "deep": 0,
        "sun": 0,
        "fog_near": 0,
        "fog_far": 0,
        "point_light": 0,
    }
    for start, end in instructions(words):
        opcode = words[start] & 0x7FF
        if opcode in (OPCODE_DCL_CONSTANT_BUFFER, OPCODE_DCL_TEMPS, OPCODE_RET):
            continue
        for operand in executable_operands(words, start, end):
            domain = water_operand_domain(operand)
            if domain is not None:
                counts[domain] += 1
    return WaterDomainUsage(
        counts["shallow"],
        counts["deep"],
        counts["sun"],
        counts["fog_near"],
        counts["fog_far"],
        counts["point_light"],
        atmospheric_fog_blend(words) is not None,
    )


def replace_operand_with_temp(
    words: list[int], operand: Operand, register: int
) -> list[int]:
    if register < 0:
        raise ContractError("temporary register is invalid")
    token = words[operand.start]
    token &= ~(0xFF << 12)
    token |= OPERAND_TEMP << 12
    token &= ~(0x3 << 20)
    token |= 1 << 20
    token &= ~(0x3F << 22)

    cursor = operand.start + 1
    extended = (words[operand.start] & 0x80000000) != 0
    while extended:
        extended = (words[cursor] & 0x80000000) != 0
        cursor += 1
    return [token, *words[operand.start + 1 : cursor], register]


def rewrite_instruction_water_domains(
    instruction: list[int], domain_registers: dict[str, int]
) -> list[int]:
    operands = executable_operands(instruction, 0, len(instruction))
    replacements: dict[int, tuple[int, list[int]]] = {}
    for operand in operands:
        domain = water_operand_domain(operand)
        if domain is None or domain not in domain_registers:
            continue
        replacements[operand.start] = (
            operand.end,
            replace_operand_with_temp(
                instruction,
                operand,
                domain_registers[domain],
            ),
        )
    if not replacements:
        return instruction

    output: list[int] = []
    cursor = 0
    while cursor < len(instruction):
        replacement = replacements.get(cursor)
        if replacement is None:
            output.append(instruction[cursor])
            cursor += 1
        else:
            end, replacement_words = replacement
            output.extend(replacement_words)
            cursor = end
    if len(output) > 0x7F:
        raise ContractError("rewritten DXBC instruction is too long")
    output[0] = (output[0] & ~(0x7F << 24)) | (len(output) << 24)
    return output


def rewrite_instruction_operand_with_temp(
    instruction: list[int], operand: Operand, register: int
) -> list[int]:
    replacement = replace_operand_with_temp(instruction, operand, register)
    output = [
        *instruction[: operand.start],
        *replacement,
        *instruction[operand.end :],
    ]
    if len(output) > 0x7F:
        raise ContractError("rewritten DXBC instruction is too long")
    output[0] = (output[0] & ~(0x7F << 24)) | (len(output) << 24)
    return output


def remap_template_operand(
    instruction: list[int],
    operand: Operand,
    output_registers: dict[int, int],
    scratch_register: int,
    template_temporary_count: int,
    point_source: tuple[list[int], Operand] | None,
    fog_alpha_source: tuple[list[int], Operand] | None,
) -> list[int] | None:
    if operand.operand_type == OPERAND_TEMP:
        if (
            len(operand.immediate_indices) != 1
            or operand.immediate_indices[0] is None
            or int(operand.immediate_indices[0]) >= template_temporary_count
        ):
            raise ContractError("transform template uses an unexpected temporary")
        return replace_operand_with_temp(
            instruction,
            operand,
            scratch_register + int(operand.immediate_indices[0]),
        )
    if operand.operand_type == OPERAND_OUTPUT:
        if (
            len(operand.immediate_indices) != 1
            or operand.immediate_indices[0] is None
            or int(operand.immediate_indices[0]) not in output_registers
        ):
            raise ContractError("transform template uses an unexpected output")
        return replace_operand_with_temp(
            instruction,
            operand,
            output_registers[int(operand.immediate_indices[0])],
        )
    if point_source is not None and water_operand_domain(operand) == "point_light":
        source_words, source_operand = point_source
        return replace_operand_indices(
            instruction,
            operand,
            source_words,
            source_operand,
        )
    if (
        fog_alpha_source is not None
        and constant_buffer_register(operand) == (4, 0)
    ):
        source_words, source_operand = fog_alpha_source
        if not operand_is_scalar_temp(source_words, source_operand):
            raise ContractError(
                "Water atmospheric-fog source is not a scalar temporary"
            )
        return list(source_words[source_operand.start : source_operand.end])
    return None


def operand_index_start(words: list[int], operand: Operand) -> int:
    cursor = operand.start + 1
    extended = (words[operand.start] & 0x80000000) != 0
    while extended:
        if cursor >= operand.end:
            raise ContractError("DXBC extended operand is truncated")
        extended = (words[cursor] & 0x80000000) != 0
        cursor += 1
    return cursor


def replace_operand_indices(
    destination_words: list[int],
    destination: Operand,
    source_words: list[int],
    source: Operand,
) -> list[int]:
    if (
        destination.operand_type != source.operand_type
        or destination.operand_type != OPERAND_CONSTANT_BUFFER
    ):
        raise ContractError("DXBC operand index transplant changed operand type")
    destination_index_start = operand_index_start(destination_words, destination)
    source_index_start = operand_index_start(source_words, source)
    destination_token = destination_words[destination.start]
    source_token = source_words[source.start]
    index_bits = ((1 << 11) - 1) << 20
    destination_token = (
        (destination_token & ~index_bits) | (source_token & index_bits)
    )
    return [
        destination_token,
        *destination_words[destination.start + 1 : destination_index_start],
        *source_words[source_index_start : source.end],
    ]


def remap_template_instruction(
    instruction: list[int],
    output_registers: dict[int, int],
    scratch_register: int,
    template_temporary_count: int,
    point_source: tuple[list[int], Operand] | None = None,
    fog_alpha_source: tuple[list[int], Operand] | None = None,
) -> list[int]:
    operands = executable_operands(instruction, 0, len(instruction))
    replacements: dict[int, tuple[int, list[int]]] = {}
    for operand in operands:
        replacement = remap_template_operand(
            instruction,
            operand,
            output_registers,
            scratch_register,
            template_temporary_count,
            point_source,
            fog_alpha_source,
        )
        if replacement is not None:
            replacements[operand.start] = (operand.end, replacement)
    output: list[int] = []
    cursor = 0
    while cursor < len(instruction):
        replacement = replacements.get(cursor)
        if replacement is None:
            output.append(instruction[cursor])
            cursor += 1
        else:
            end, replacement_words = replacement
            output.extend(replacement_words)
            cursor = end
    if len(output) > 0x7F:
        raise ContractError("remapped transform instruction is too long")
    output[0] = (output[0] & ~(0x7F << 24)) | (len(output) << 24)
    return output


def extract_transform_template(
    data: bytes,
    expected_b5_size: int,
    expected_output_count: int,
    expected_instruction_count: int,
) -> tuple[list[int], TransformTemplate]:
    _, _, _, words = shader_words(data)
    dcl_b5: list[list[int]] = []
    for start, end in instructions(words):
        if (words[start] & 0x7FF) != OPCODE_DCL_CONSTANT_BUFFER:
            continue
        instruction = words[start:end]
        operands = executable_operands(instruction, 0, len(instruction))
        if len(operands) != 1:
            raise ContractError("template constant-buffer declaration changed shape")
        if operands[0].immediate_indices == (5, expected_b5_size):
            dcl_b5.append(instruction)
    if len(dcl_b5) != 1:
        raise ContractError(
            f"template must declare exactly b5[{expected_b5_size}]"
        )

    temp_declaration, _, body = shader_declarations_and_body(words)
    temporary_count = words[temp_declaration[0] + 1]
    if temporary_count == 0:
        raise ContractError("transform template must own temporary registers")
    body_instructions = [words[start:end] for start, end in body]
    if not body_instructions or (body_instructions[-1][0] & 0x7FF) != OPCODE_RET:
        raise ContractError("transform template must end in ret")
    transform = body_instructions[:-1]
    if len(transform) != expected_instruction_count:
        raise ContractError(
            "transform template instruction count changed: "
            f"expected {expected_instruction_count}, found {len(transform)}"
        )
    seen_outputs: set[int] = set()
    for instruction in transform:
        for operand in executable_operands(instruction, 0, len(instruction)):
            if operand.operand_type != OPERAND_OUTPUT:
                continue
            if (
                len(operand.immediate_indices) != 1
                or operand.immediate_indices[0] is None
            ):
                raise ContractError("template output is not immediate-indexed")
            seen_outputs.add(int(operand.immediate_indices[0]))
    if seen_outputs != set(range(expected_output_count)):
        raise ContractError("transform template output matrix changed")
    return dcl_b5[0], TransformTemplate(
        temporary_count,
        tuple(tuple(instruction) for instruction in transform),
    )


def patch_water_shader(
    original: bytes,
    b5_declaration: list[int],
    templates: dict[str, TransformTemplate],
    usage: WaterDomainUsage,
) -> bytes:
    version, chunks, shader_index, words = shader_words(original)
    temp_declaration, executable_start, body = shader_declarations_and_body(words)
    fog_blend = atmospheric_fog_blend(words)
    if (fog_blend is not None) != usage.atmospheric_fog:
        raise ContractError("Water atmospheric-fog qualification changed")
    original_temp_count = words[temp_declaration[0] + 1]
    if original_temp_count == 0 or original_temp_count > 4080:
        raise ContractError("Water shader temporary-register count is invalid")

    domain_registers: dict[str, int] = {}
    next_register = original_temp_count
    if usage.shallow_deep:
        domain_registers["shallow"] = next_register
        domain_registers["deep"] = next_register + 1
        next_register += 2
    if usage.sun:
        domain_registers["sun"] = next_register
        next_register += 1
    if usage.fog:
        domain_registers["fog_near"] = next_register
        domain_registers["fog_far"] = next_register + 1
        next_register += 2
    if usage.point_light:
        domain_registers["point_light"] = next_register
        next_register += 1
    if usage.atmospheric_fog:
        domain_registers["atmospheric_fog_alpha"] = next_register
        next_register += 1

    active_templates = [
        templates[name]
        for name, active in (
            ("shallow_deep", usage.shallow_deep),
            ("sun", usage.sun),
            ("fog", usage.fog),
            ("point_light", usage.point_light),
            ("fog_alpha", usage.atmospheric_fog),
        )
        if active
    ]
    if not active_templates:
        raise ContractError("Water shader has no qualified colour domain")
    scratch_register = next_register
    scratch_count = max(template.temporary_count for template in active_templates)
    replacement_temp_count = scratch_register + scratch_count
    if replacement_temp_count > 4096:
        raise ContractError("Water shader replacement temporary count overflows")

    for start, end in instructions(words):
        if (words[start] & 0x7FF) != OPCODE_DCL_CONSTANT_BUFFER:
            continue
        instruction = words[start:end]
        operands = executable_operands(instruction, 0, len(instruction))
        if operands and operands[0].immediate_indices and operands[0].immediate_indices[0] == 5:
            raise ContractError("Water shader already owns constant buffer b5")

    prologue: list[int] = []

    def append_template(
        template_name: str,
        output_registers: dict[int, int],
        point_source: tuple[list[int], Operand] | None = None,
    ) -> None:
        template = templates[template_name]
        for template_instruction in template.instructions:
            prologue.extend(
                remap_template_instruction(
                    list(template_instruction),
                    output_registers,
                    scratch_register,
                    template.temporary_count,
                    point_source,
                )
            )

    if usage.shallow_deep:
        append_template(
            "shallow_deep",
            {
                0: domain_registers["shallow"],
                1: domain_registers["deep"],
            },
        )
    if usage.sun:
        append_template("sun", { 0: domain_registers["sun"] })
    if usage.fog:
        append_template(
            "fog",
            {
                0: domain_registers["fog_near"],
                1: domain_registers["fog_far"],
            },
        )

    rewritten_body: list[int] = []
    rewritten_counts = {
        "shallow": 0,
        "deep": 0,
        "sun": 0,
        "fog_near": 0,
        "fog_far": 0,
        "point_light": 0,
    }
    for start, end in body:
        instruction = words[start:end]
        operands = [] if (instruction[0] & 0x7FF) == OPCODE_RET else (
            executable_operands(instruction, 0, len(instruction))
        )
        point_operands = [
            operand
            for operand in operands
            if water_operand_domain(operand) == "point_light"
        ]
        if len(point_operands) > 1:
            raise ContractError(
                "Water point-light instruction reads multiple colour operands"
            )
        if point_operands:
            point_template = templates["point_light"]
            for template_instruction in point_template.instructions:
                rewritten_body.extend(
                    remap_template_instruction(
                        list(template_instruction),
                        { 0: domain_registers["point_light"] },
                        scratch_register,
                        point_template.temporary_count,
                        (instruction, point_operands[0]),
                    )
                )
        if fog_blend is not None and start == fog_blend.instruction_start:
            if len(operands) != 4 or not operand_is_scalar_temp(
                instruction, operands[1]
            ):
                raise ContractError(
                    "Water atmospheric-fog output source changed shape"
                )
            fog_alpha_template = templates["fog_alpha"]
            for template_instruction in fog_alpha_template.instructions:
                rewritten_body.extend(
                    remap_template_instruction(
                        list(template_instruction),
                        { 0: domain_registers["atmospheric_fog_alpha"] },
                        scratch_register,
                        fog_alpha_template.temporary_count,
                        fog_alpha_source=(instruction, operands[1]),
                    )
                )
            instruction = rewrite_instruction_operand_with_temp(
                instruction,
                operands[1],
                domain_registers["atmospheric_fog_alpha"],
            )
            operands = executable_operands(instruction, 0, len(instruction))
        for operand in operands:
            domain = water_operand_domain(operand)
            if domain is not None:
                rewritten_counts[domain] += 1
        rewritten = rewrite_instruction_water_domains(
            instruction,
            domain_registers,
        )
        rewritten_body.extend(rewritten)

    rewritten_usage = WaterDomainUsage(
        rewritten_counts["shallow"],
        rewritten_counts["deep"],
        rewritten_counts["sun"],
        rewritten_counts["fog_near"],
        rewritten_counts["fog_far"],
        rewritten_counts["point_light"],
        usage.atmospheric_fog,
    )
    if rewritten_usage != usage:
        raise ContractError("Water colour-domain rewrite count changed")

    prefix = words[:executable_start]
    prefix[temp_declaration[0] + 1] = replacement_temp_count
    prefix[temp_declaration[0] : temp_declaration[0]] = b5_declaration
    patched_words = [*prefix, *prologue, *rewritten_body]
    patched_words[1] = len(patched_words)

    patched_chunks = list(chunks)
    patched_chunks[shader_index] = DxbcChunk(
        patched_chunks[shader_index].tag,
        pack_words(patched_words),
    )
    return build_dxbc(version, patched_chunks)


def run_fxc(arguments: list[str], label: str) -> None:
    result = subprocess.run(arguments, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        detail = (result.stdout + result.stderr).strip()
        raise ContractError(f"fxc failed for {label}: {detail}")


def compile_templates(
    root: Path,
    fxc: Path,
    output_directory: Path,
) -> tuple[list[int], dict[str, TransformTemplate]]:
    source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "WaterLinearLighting"
        / "WaterColorTransformTemplate.hlsl"
    )
    source_text = source.read_text(encoding="utf-8")
    required = (
        "float4 SunColor : packoffset(c2);",
        "float4 ShallowColor : packoffset(c0);",
        "float4 DeepColor : packoffset(c1);",
        "float4 FogNearColor : packoffset(c6);",
        "float4 FogFarColor : packoffset(c7);",
        "float4 PointLightColor : packoffset(c20);",
        "float FogAlphaSource : packoffset(c0.x);",
        "uint EnableLinearLighting : packoffset(c0.x);",
        "float LightGamma : packoffset(c0.w);",
        "float FogGamma : packoffset(c2.x);",
        "float FogAlphaGamma : packoffset(c2.y);",
        "float WaterGamma : packoffset(c3.y);",
        "float DirectionalLightMultiplier : packoffset(c4.x);",
        "float PointLightMultiplier : packoffset(c4.y);",
        "pow(abs(output.shallow.xyz), WaterGamma)",
        "pow(abs(output.deep.xyz), WaterGamma)",
        "LightGamma / NativeProducerGamma",
        "pow(abs(output.shallow.xyz), FogGamma)",
        "pow(abs(output.deep.xyz), FogGamma)",
        "pow(abs(output), FogAlphaGamma)",
    )
    for text in required:
        if text not in source_text:
            raise ContractError(f"Water transform template is missing contract: {text}")
    specifications = {
        "shallow_deep": (
            "PSMain",
            4,
            2,
            10,
            (
                "dcl_constantbuffer CB1[2], immediateIndexed",
                "dcl_constantbuffer CB5[4], immediateIndexed",
                "movc o0.xyz, cb5[0].xxxx",
                "movc o1.xyz, cb5[0].xxxx",
            ),
        ),
        "sun": (
            "PSSunMain",
            5,
            1,
            7,
            (
                "dcl_constantbuffer CB0[3], immediateIndexed",
                "dcl_constantbuffer CB5[5], immediateIndexed",
                "mul r0.x, l(0.454545), cb5[0].w",
                "mul r0.xyz, r0.xyzx, cb5[4].xxxx",
            ),
        ),
        "fog": (
            "PSFogMain",
            3,
            2,
            10,
            (
                "dcl_constantbuffer CB1[8], immediateIndexed",
                "dcl_constantbuffer CB5[3], immediateIndexed",
                "mul r0.xyz, r0.xyzx, cb5[2].xxxx",
                "movc o1.xyz, cb5[0].xxxx",
            ),
        ),
        "point_light": (
            "PSPointMain",
            5,
            1,
            7,
            (
                "dcl_constantbuffer CB2[21], immediateIndexed",
                "dcl_constantbuffer CB5[5], immediateIndexed",
                "mul r0.x, l(0.454545), cb5[0].w",
                "mul r0.xyz, r0.xyzx, cb5[4].yyyy",
            ),
        ),
        "fog_alpha": (
            "PSFogAlphaMain",
            3,
            1,
            4,
            (
                "dcl_constantbuffer CB4[1], immediateIndexed",
                "dcl_constantbuffer CB5[3], immediateIndexed",
                "mul r0.x, r0.x, cb5[2].y",
                "movc o0.xyzw, cb5[0].xxxx",
            ),
        ),
    }
    declarations: dict[str, list[int]] = {}
    templates: dict[str, TransformTemplate] = {}
    for name, (
        entry_point,
        b5_size,
        output_count,
        instruction_count,
        assembly_contracts,
    ) in specifications.items():
        output = output_directory / f"WaterColorTransformTemplate.{name}.dxbc"
        assembly = output_directory / f"WaterColorTransformTemplate.{name}.asm.txt"
        run_fxc(
            [
                str(fxc),
                "/nologo",
                "/T",
                "ps_5_0",
                "/E",
                entry_point,
                "/O3",
                "/Ges",
                "/WX",
                "/Fo",
                str(output),
                "/Fc",
                str(assembly),
                str(source),
            ],
            f"Water {name} transform template",
        )
        assembly_text = assembly.read_text(encoding="utf-8")
        for required_assembly in ("dcl_temps 1", *assembly_contracts):
            if required_assembly not in assembly_text:
                raise ContractError(
                    f"Water {name} transform template assembly changed: "
                    + required_assembly
                )
        declaration, template = extract_transform_template(
            output.read_bytes(),
            b5_size,
            output_count,
            instruction_count,
        )
        declarations[name] = declaration
        templates[name] = template
    if declarations["sun"] != declarations["point_light"]:
        raise ContractError("Water light templates disagree on b5[5] declaration")
    return declarations["sun"], templates


def signature_contract(assembly: str) -> str:
    try:
        start = assembly.index("// Input signature:")
        end = assembly.index("ps_5_0", start)
    except ValueError as error:
        raise ContractError("shader assembly is missing signature tables") from error
    return "\n".join(
        line.rstrip()
        for line in assembly[start:end].splitlines()
        if line.startswith("//")
    )


def water_records(inventory: census.FxpInventory) -> list[census.DxbcContainer]:
    records = [
        item
        for item in inventory.containers
        if item.family == "Water" and item.stage == "PS" and item.key is not None
    ]
    if (
        len(records) != EXPECTED_WATER_SLOT_COUNT
        or len({item.identity for item in records}) != EXPECTED_WATER_IDENTITY_COUNT
    ):
        raise ContractError("active FO4VR FXP Water family changed shape")
    return records


def expected_contracts(
    records: list[census.DxbcContainer],
) -> dict[
    int,
    tuple[census.DxbcContainer, tuple[int, ...], WaterDomainUsage],
]:
    by_identity: dict[tuple[int, str], list[census.DxbcContainer]] = {}
    for item in records:
        by_identity.setdefault(item.identity, []).append(item)

    result: dict[
        int,
        tuple[census.DxbcContainer, tuple[int, ...], WaterDomainUsage],
    ] = {}
    for identity_records in by_identity.values():
        ordered = sorted(identity_records, key=lambda item: int(item.key))
        usages = {water_domain_usage(item.data) for item in ordered}
        if len(usages) != 1:
            raise ContractError("aliased Water identity changed colour-domain references")
        usage = next(iter(usages))
        if not usage.active:
            continue
        descriptor = int(ordered[0].key)
        result[descriptor] = (
            ordered[0],
            tuple(int(item.key) for item in ordered[1:]),
            usage,
        )
    if (
        len(result) != EXPECTED_WATER_CONTRACT_COUNT
        or set(result) != EXPECTED_WATER_CONTRACT_DESCRIPTORS
    ):
        found = ", ".join(f"0x{value:08X}" for value in sorted(result))
        raise ContractError(
            "active FO4VR Water shallow/deep contract matrix changed: " + found
        )
    atmospheric_fog_descriptors = {
        descriptor
        for descriptor, (_, _, usage) in result.items()
        if usage.atmospheric_fog
    }
    if atmospheric_fog_descriptors != EXPECTED_WATER_ATMOSPHERIC_FOG_DESCRIPTORS:
        found = ", ".join(
            f"0x{value:08X}" for value in sorted(atmospheric_fog_descriptors)
        )
        raise ContractError(
            "active FO4VR Water atmospheric-fog matrix changed: " + found
        )
    slot_coverage = {
        "shallow_deep": sum(
            len(aliases) + 1
            for _, aliases, usage in result.values()
            if usage.shallow_deep
        ),
        "sun": sum(
            len(aliases) + 1
            for _, aliases, usage in result.values()
            if usage.sun
        ),
        "fog": sum(
            len(aliases) + 1
            for _, aliases, usage in result.values()
            if usage.fog
        ),
        "point_light": sum(
            len(aliases) + 1
            for _, aliases, usage in result.values()
            if usage.point_light
        ),
        "atmospheric_fog": sum(
            len(aliases) + 1
            for _, aliases, usage in result.values()
            if usage.atmospheric_fog
        ),
    }
    if slot_coverage != {
        "shallow_deep": 48,
        "sun": 33,
        "fog": 25,
        "point_light": 23,
        "atmospheric_fog": 33,
    }:
        raise ContractError(
            "active FO4VR Water colour-domain slot coverage changed: "
            + repr(slot_coverage)
        )
    return result


def read_manifest(
    root: Path,
    contracts: dict[
        int,
        tuple[census.DxbcContainer, tuple[int, ...], WaterDomainUsage],
    ],
) -> list[dict[str, object]]:
    path = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "WaterLinearLightingContracts.json"
    )
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, list) or len(value) != len(contracts):
        raise ContractError(f"Water manifest must contain {len(contracts)} contracts")
    names: set[str] = set()
    descriptors: set[int] = set()
    resources: set[str] = set()
    for index, entry in enumerate(value):
        if not isinstance(entry, dict):
            raise ContractError(f"Water manifest entry {index} is not an object")
        name = entry.get("name")
        descriptor = entry.get("descriptor")
        aliases = entry.get("aliases")
        resource = entry.get("resource")
        if not isinstance(name, str) or not name:
            raise ContractError(f"Water manifest entry {index} has an invalid name")
        if not isinstance(descriptor, int) or descriptor not in contracts:
            raise ContractError(f"Water manifest entry {index} has an invalid descriptor")
        if aliases != list(contracts[descriptor][1]):
            raise ContractError(f"Water manifest entry {index} has unexpected aliases")
        if not isinstance(resource, str) or not resource.startswith("IDR_"):
            raise ContractError(f"Water manifest entry {index} has an invalid resource")
        if name in names or descriptor in descriptors or resource in resources:
            raise ContractError(f"Water manifest entry {index} is duplicated")
        names.add(name)
        descriptors.add(descriptor)
        resources.add(resource)
    if descriptors != set(contracts):
        raise ContractError("Water manifest descriptor matrix is incomplete")
    return sorted(value, key=lambda item: int(item["descriptor"]))


def validate_candidate(
    fxc: Path,
    name: str,
    original: bytes,
    candidate: bytes,
    usage: WaterDomainUsage,
    output_directory: Path,
) -> None:
    original_path = output_directory / f"{name}.vanilla.dxbc"
    candidate_path = output_directory / f"{name}.dxbc"
    original_assembly = output_directory / f"{name}.vanilla.asm.txt"
    candidate_assembly = output_directory / f"{name}.asm.txt"
    original_path.write_bytes(original)
    candidate_path.write_bytes(candidate)
    run_fxc(
        [str(fxc), "/nologo", "/dumpbin", "/Fc", str(original_assembly), str(original_path)],
        f"{name} vanilla",
    )
    run_fxc(
        [str(fxc), "/nologo", "/dumpbin", "/Fc", str(candidate_assembly), str(candidate_path)],
        f"{name} candidate",
    )
    original_text = original_assembly.read_text(encoding="utf-8")
    candidate_text = candidate_assembly.read_text(encoding="utf-8")
    if signature_contract(original_text) != signature_contract(candidate_text):
        raise ContractError(f"{name} changed the exact FO4VR shader signature")
    original_declarations = census.parse_declarations(original_text)
    candidate_declarations = census.parse_declarations(candidate_text)
    original_buffers = dict(original_declarations.constant_buffers)
    candidate_buffers = dict(candidate_declarations.constant_buffers)
    if candidate_buffers.get(5) != 5:
        raise ContractError(f"{name} does not consume frame-only b5[5]")
    if 8 in candidate_buffers:
        raise ContractError(f"{name} unexpectedly consumes geometry b8")
    candidate_buffers.pop(5)
    if candidate_buffers != original_buffers:
        raise ContractError(f"{name} changed vanilla constant buffers")
    candidate_samplers=set(candidate_declarations.samplers)
    candidate_textures=set(candidate_declarations.textures)
    if 14 in candidate_samplers:
        if candidate_textures-set(original_declarations.textures)!={50,51,*range(69,75)}:
            raise ContractError(f"{name} water reflection texture footprint changed")
        candidate_samplers.remove(14);candidate_textures-={50,51,*range(69,75)}
    if candidate_samplers!=set(original_declarations.samplers) or candidate_textures!=set(original_declarations.textures):
        raise ContractError(f"{name} changed native texture/sampler bindings")

    _, _, _, candidate_words = shader_words(candidate)
    fog_alpha_gamma_references = 0
    for start, end in instructions(candidate_words):
        if (candidate_words[start] & 0x7FF) in (
            OPCODE_DCL_CONSTANT_BUFFER,
            OPCODE_DCL_TEMPS,
            OPCODE_RET,
        ):
            continue
        for operand in executable_operands(candidate_words, start, end):
            if (
                constant_buffer_register(operand) == (5, 2)
                and operand_selects_only_component(
                    candidate_words, operand, 1
                )
            ):
                fog_alpha_gamma_references += 1
    expected_fog_alpha_references = 1 if usage.atmospheric_fog else 0
    if fog_alpha_gamma_references != expected_fog_alpha_references:
        raise ContractError(
            f"{name} atmospheric-fog gamma reference count changed: "
            f"{fog_alpha_gamma_references}"
        )


def format_identity(data: bytes, indent: str) -> list[str]:
    rows = [f"{indent}{{", f"{indent}    {len(data)},", f"{indent}    {{"]
    for offset in range(4, 20, 4):
        values = ", ".join(
            f"std::byte{{ 0x{value:02X} }}" for value in data[offset : offset + 4]
        )
        rows.append(f"{indent}        {values},")
    rows.extend((f"{indent}    }},", f"{indent}}},"))
    return rows


def render_contracts(
    manifest: list[dict[str, object]],
    contracts: dict[
        int,
        tuple[census.DxbcContainer, tuple[int, ...], WaterDomainUsage],
    ],
    candidates: dict[int, bytes],
) -> str:
    rows = [
        "// Generated by tools/generate_water_linear_lighting_contracts.py.",
        "// Do not edit this file by hand.",
        f"constexpr std::array<WaterShaderContractDefinition, {len(manifest)}> "
        "kWaterShaderContracts{ {",
    ]
    for entry in manifest:
        descriptor = int(entry["descriptor"])
        rows.extend(
            (
                "    {",
                f'        "{entry["name"]}",',
                f"        {descriptor}u,",
                f'        {entry["resource"]},',
            )
        )
        rows.extend(format_identity(contracts[descriptor][0].data, "        "))
        rows.extend(format_identity(candidates[descriptor], "        "))
        rows.append("    },")
    rows.extend(("} };", ""))
    return "\n".join(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--write-assets", action="store_true")
    arguments = parser.parse_args()
    if arguments.check == arguments.write_assets:
        print("select exactly one of --check or --write-assets", file=sys.stderr)
        return 1

    try:
        root = arguments.root.resolve()
        inventory = census.parse_fxp((root / "Shaders012_VR.fxp").read_bytes())
        contracts = expected_contracts(water_records(inventory))
        manifest = read_manifest(root, contracts)
        fxc = census.find_fxc(None)
        with tempfile.TemporaryDirectory(prefix="fo4vr_water_linear_lighting_") as temporary:
            temporary_path = Path(temporary)
            b5_declaration, templates = compile_templates(
                root,
                fxc,
                temporary_path,
            )
            parallax_path=temporary_path / "WaterParallax.dxbc"
            run_fxc([str(fxc), "/nologo", "/T", "ps_5_0", "/E", "PSMain", "/O3", "/Ges", "/WX", "/Fo", str(parallax_path),
                str(root / "package/Shaders/Community/WaterEffects/WaterParallax.hlsl")], "bounded water parallax")
            parallax=parallax_path.read_bytes()
            reflection_path=temporary_path / "WaterReflections.dxbc"
            run_fxc([str(fxc), "/nologo", "/T", "ps_5_0", "/E", "PSMain", "/O3", "/Ges", "/WX", "/Fo", str(reflection_path),
                str(root / "package/Shaders/Community/WaterEffects/WaterReflections.hlsl")], "water environment reflection")
            reflection=reflection_path.read_bytes()
            candidates: dict[int, bytes] = {}
            for entry in manifest:
                descriptor = int(entry["descriptor"])
                name = str(entry["name"])
                original = contracts[descriptor][0].data
                candidate = patch_water_shader(
                    original,
                    b5_declaration,
                    templates,
                    contracts[descriptor][2],
                )
                before_parallax=candidate
                candidate=water_parallax_transform.apply(candidate,parallax)
                if candidate!=before_parallax:
                    water_parallax_transform.verify_vertex_pair(inventory,[descriptor,*entry["aliases"]])
                candidate=water_reflection_transform.apply(candidate,reflection)
                validate_candidate(
                    fxc,
                    name,
                    original,
                    candidate,
                    contracts[descriptor][2],
                    temporary_path,
                )
                candidates[descriptor] = candidate

        generated = render_contracts(manifest, contracts, candidates)
        output = arguments.output.resolve()
        asset_directory = (
            root / "package" / "Shaders" / "Community" / "WaterLinearLighting"
        )
        verified_directory = (
            root
            / "package"
            / "Shaders"
            / "Community"
            / "VerifiedWaterLinearLighting"
        )
        if arguments.write_assets:
            asset_directory.mkdir(parents=True, exist_ok=True)
            verified_directory.mkdir(parents=True, exist_ok=True)
            for entry in manifest:
                descriptor = int(entry["descriptor"])
                name = str(entry["name"])
                (asset_directory / f"{name}.dxbc").write_bytes(candidates[descriptor])
                (verified_directory / f"{name}.dxbc").write_bytes(
                    contracts[descriptor][0].data
                )
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(generated, encoding="utf-8", newline="\n")
        else:
            if not output.is_file() or output.read_text(encoding="utf-8") != generated:
                raise ContractError(f"generated Water contracts are stale: {output}")
            for entry in manifest:
                descriptor = int(entry["descriptor"])
                name = str(entry["name"])
                candidate_asset = asset_directory / f"{name}.dxbc"
                verified_asset = verified_directory / f"{name}.dxbc"
                if (
                    not candidate_asset.is_file()
                    or candidate_asset.read_bytes() != candidates[descriptor]
                ):
                    raise ContractError(f"packaged Water replacement is stale: {candidate_asset}")
                if (
                    not verified_asset.is_file()
                    or verified_asset.read_bytes() != contracts[descriptor][0].data
                ):
                    raise ContractError(f"verified Water original is stale: {verified_asset}")
    except (OSError, ContractError, census.CensusError, json.JSONDecodeError) as error:
        print(f"Water Linear Lighting contract generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
