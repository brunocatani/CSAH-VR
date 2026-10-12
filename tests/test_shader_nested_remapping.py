"""A relative camera index must follow its temporary through shader insertion."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from dxbc_transform import executable_operands, OPERAND_CONSTANT_BUFFER, OPERAND_TEMP
from generate_ibl_material_contracts import remap_template_instruction
from generate_contact_shadow_shader import remap_pbr_instruction, remap_sun_instruction, remap_transform_instruction

# mov r2.xyzw, cb12[r0.x + 32].xyzw. The nested operand is deliberately r0:
# an unshifted index can silently read a plausible but wrong native camera row.
source = [0x08000036, 0x001000F2, 2,
          0x00208E46 | (3 << 25), 12, 32, 0x0010000A, 0]
transforms = (
    lambda words: remap_template_instruction(words, 0, 1, 100, 3, 103, 104),
    lambda words: remap_transform_instruction(words, 100, 3, 103),
    lambda words: remap_pbr_instruction(words, 100, 3, 103),
    lambda words: remap_sun_instruction(words, 100, 104, 105, 106, 107, 108),
)
for transform in transforms:
    output = transform(source)
    destination, camera = executable_operands(output, 0, len(output))
    assert destination.operand_type == OPERAND_TEMP and destination.immediate_indices == (102,)
    assert camera.operand_type == OPERAND_CONSTANT_BUFFER and camera.immediate_indices[0] == 12
    assert len(camera.relative_operands) == 1
    index = camera.relative_operands[0]
    assert index.operand_type == OPERAND_TEMP and index.immediate_indices == (100,)
    # Independent evaluation with distinct native and inserted temporary values.
    registers = {0: 57, 100: 4}
    row = output[camera.start + 2] + registers[index.immediate_indices[0]]
    assert row == 36, f"wrong eye's inverse projection row: {row}"
assert source[-1] == 0, "remapping modified the shared source template"
print("Nested camera indices follow relocated temporaries in IBL, contact, PBR and sun transforms")
