"""Compile the stereo SSGI pipeline for embedding."""
import argparse
import subprocess
from pathlib import Path

SOURCE_DIR = 'package/Shaders/Community/ScreenSpaceGI'
# Dispatch order; the runtime indexes kScreenSpaceGIShaders with ScreenSpaceGIPass.
PASSES = ('PrepareCS', 'PrefilterPositionCS', 'ReprojectCS', 'PrefilterRadianceCS', 'TraceCS', 'BlurCS', 'CaptureCS')


def compile_shader(root, fxc, output, name, source, profile, entry):
    binary = output.parent / (name + '.dxbc')
    source_path = root / SOURCE_DIR / (source + '.hlsl')
    subprocess.run([str(fxc), '/nologo', '/T', profile, '/E', entry, '/O3', '/Ges', '/WX', '/Fo', str(binary), str(source_path)], check=True)
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
    for index, name in enumerate(PASSES):
        rows.extend(array_rows(f'kScreenSpaceGI{index}', compile_shader(root, fxc, output, name, name, 'cs_5_0', 'main')))
    rows.append('inline constexpr ScreenSpaceGIShader kScreenSpaceGIShaders[]={')
    rows.extend(f'{{kScreenSpaceGI{i},sizeof(kScreenSpaceGI{i})}},' for i in range(len(PASSES)))
    rows.append('};')
    rows.extend(array_rows('kScreenSpaceGIComposeVSData', compile_shader(root, fxc, output, 'ComposeOcclusionVS', 'ComposeOcclusion', 'vs_5_0', 'VSMain')))
    rows.extend(array_rows('kScreenSpaceGIComposePSData', compile_shader(root, fxc, output, 'ComposeOcclusionPS', 'ComposeOcclusion', 'ps_5_0', 'PSMain')))
    rows.append('inline constexpr ScreenSpaceGIShader kScreenSpaceGIComposeVS{kScreenSpaceGIComposeVSData,sizeof(kScreenSpaceGIComposeVSData)};')
    rows.append('inline constexpr ScreenSpaceGIShader kScreenSpaceGIComposePS{kScreenSpaceGIComposePSData,sizeof(kScreenSpaceGIComposePSData)};')
    output.write_text('\n'.join(rows) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--fxc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    generate(args.root, args.fxc, args.output)
