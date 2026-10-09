"""Compile the bounded stereo AO/GI pipeline from source for embedding."""
import argparse
import subprocess
from pathlib import Path

def generate(root,fxc,output):
    output.parent.mkdir(parents=True,exist_ok=True)
    rows=['#pragma once','#include <cstddef>','struct ScreenSpaceGIShader {const unsigned char* data;std::size_t size;};']
    for index,name in enumerate(('PrepareCS','TraceCS','FilterCS','CaptureCS')):
        binary=output.parent/(name+'.dxbc')
        source=root/'package/Shaders/Community/ScreenSpaceGI'/(name+'.hlsl')
        subprocess.run([str(fxc),'/nologo','/T','cs_5_0','/E','main','/O3','/Ges','/WX','/Fo',str(binary),str(source)],check=True)
        data=binary.read_bytes()
        rows.append(f'inline constexpr unsigned char kScreenSpaceGI{index}[]={{')
        for start in range(0,len(data),16): rows.append(','.join(f'0x{b:02x}' for b in data[start:start+16])+',')
        rows.append('};')
    rows.append('inline constexpr ScreenSpaceGIShader kScreenSpaceGIShaders[]={')
    rows.extend(f'{{kScreenSpaceGI{i},sizeof(kScreenSpaceGI{i})}},' for i in range(4))
    rows.append('};')
    output.write_text('\n'.join(rows)+'\n')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--fxc',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();generate(args.root,args.fxc,args.output)
