"""Compile the stereo SSGI pipeline for embedding."""
import argparse
import subprocess
from pathlib import Path

SOURCE_DIR = 'package/Shaders/Community/ScreenSpaceGI'
# Runtime Pass order: (embedded name, source, defines). The runtime indexes
# kScreenSpaceGIShaders with Runtime::Pass.
PASSES = (
    ('PrepareCS', 'PrepareCS', ()),
    ('PrefilterPositionCS', 'PrefilterPositionCS', ()),
    ('ReprojectCS', 'ReprojectCS', ()),
    ('PrefilterRadianceCS', 'PrefilterRadianceCS', ()),
    ('TraceCS', 'TraceCS', ()),
    ('BlurCS', 'BlurCS', ()),
    ('CaptureCS', 'CaptureCS', ()),
    ('TraceDiffuseCS', 'TraceCS', ('CSAH_SSGI_SPECULAR_TRACE=0',)),
)


def compile_shader(root, fxc, output, name, source, profile, entry, defines=()):
    binary = output.parent / (name + '.dxbc')
    source_path = root / SOURCE_DIR / (source + '.hlsl')
    flags = [part for define in defines for part in ('/D', define)]
    subprocess.run([str(fxc), '/nologo', '/T', profile, '/E', entry, '/O3', '/Ges', '/WX', *flags, '/Fo', str(binary), str(source_path)], check=True)
    return binary.read_bytes()


def array_rows(symbol, data):
    rows = [f'inline constexpr unsigned char {symbol}[]={{']
    for start in range(0, len(data), 16):
        rows.append(','.join(f'0x{b:02x}' for b in data[start:start + 16]) + ',')
    rows.append('};')
    return rows


def generate(root, fxc, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    rows = ['#pragma once', '#include <cstddef>', 'struct ScreenSpaceGIShader {const unsigned char* data;std::size_t size;};']
    for index, (name, source, defines) in enumerate(PASSES):
        rows.extend(array_rows(f'kScreenSpaceGI{index}', compile_shader(root, fxc, output, name, source, 'cs_5_0', 'main', defines)))
    rows.append('inline constexpr ScreenSpaceGIShader kScreenSpaceGIShaders[]={')
    rows.extend(f'{{kScreenSpaceGI{i},sizeof(kScreenSpaceGI{i})}},' for i in range(len(PASSES)))
    rows.append('};')
    rows.extend(array_rows('kScreenSpaceGIComposeVSData', compile_shader(root, fxc, output, 'ComposeOcclusionVS', 'ComposeOcclusion', 'vs_5_0', 'VSMain')))
    rows.extend(array_rows('kScreenSpaceGIComposePSData', compile_shader(root, fxc, output, 'ComposeOcclusionPS', 'ComposeOcclusion', 'ps_5_0', 'PSMain')))
    rows.extend(array_rows('kScreenSpaceGIComposeScalePSData', compile_shader(root, fxc, output, 'ComposeOcclusionScalePS', 'ComposeOcclusion', 'ps_5_0', 'PSScale')))
    rows.append('inline constexpr ScreenSpaceGIShader kScreenSpaceGIComposeVS{kScreenSpaceGIComposeVSData,sizeof(kScreenSpaceGIComposeVSData)};')
    rows.append('inline constexpr ScreenSpaceGIShader kScreenSpaceGIComposePS{kScreenSpaceGIComposePSData,sizeof(kScreenSpaceGIComposePSData)};')
    rows.append('inline constexpr ScreenSpaceGIShader kScreenSpaceGIComposeScalePS{kScreenSpaceGIComposeScalePSData,sizeof(kScreenSpaceGIComposeScalePSData)};')
    output.write_text('\n'.join(rows) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--fxc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    generate(args.root, args.fxc, args.output)
