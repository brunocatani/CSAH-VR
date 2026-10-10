"""DXBC input substitution must preserve the compiler's source semantics."""
import sys
import unittest
from pathlib import Path
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import generate_ibl_material_contracts as ibl


def evaluate(source, values):
    token = source[0]
    mode = (token >> 2) & 3
    components = [((token >> 4) & 3)] * 4 if mode == 2 else [
        (token >> (4 + i * 2)) & 3 for i in range(4)]
    result = [values[c] for c in components]
    modifier = (source[1] >> 6) & 3 if token & 0x80000000 else 0
    if modifier in (2, 3):
        result = [abs(v) for v in result]
    if modifier in (1, 3):
        result = [-v for v in result]
    return result


class TemplateInputs(unittest.TestCase):
    def test_source_semantics(self):
        # Nonuniform signed coordinates expose direction flips, scalar array
        # index errors and the yyzx swizzle used by reconstructed PBR normals.
        values = [-0.25, 0.5, -0.75, 3.0]
        selections = [6 | (s << 4) for s in range(256)]
        selections += [10 | (c << 4) for c in range(4)]
        for register in (0, 1):
            for selection in selections:
                for modifier in range(4):
                    source = [0x00101000 | selection]
                    if modifier:
                        source[0] |= 0x80000000
                        source.append(1 | (modifier << 6))
                    source.append(register)
                    instruction = [ibl.MOV_OPCODE, 0x001000F2, 0, *source]
                    instruction[0] |= len(instruction) << 24
                    mapped = ibl.remap_template_instruction(
                        instruction, 80, 81, 20, 1, 90, 91)
                    output = mapped[3:]
                    self.assertEqual((output[0] >> 12) & 255, ibl.OPERAND_TEMP)
                    self.assertEqual(output[-1], 80 + register)
                    self.assertEqual(evaluate(output, values), evaluate(source, values))
                    self.assertEqual(mapped[0] >> 24, len(mapped))


if __name__ == "__main__":
    unittest.main()
