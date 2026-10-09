"""Embed the caustics DDS mip chain without runtime disk I/O or image decoding."""
import argparse,struct
from pathlib import Path
def generate(source,output):
    data=source.read_bytes()
    if len(data)<148 or data[:4]!=b'DDS ' or data[84:88]!=b'DX10': raise ValueError('Expected a DX10 DDS caustics image')
    height,width=struct.unpack_from('<II',data,12);mips=struct.unpack_from('<I',data,28)[0]
    fmt,dimension,flags,array,alpha=struct.unpack_from('<5I',data,128)
    if fmt!=80 or dimension!=3 or array!=1 or flags!=0 or not(1<=mips<=16 and width>0 and height>0): raise ValueError('Expected a single BC4_UNORM 2D mip chain')
    payload=data[148:];offset=0;levels=[]
    for mip in range(mips):
        w=max(1,width>>mip);h=max(1,height>>mip);pitch=((w+3)//4)*8;size=pitch*((h+3)//4)
        levels.append((offset,pitch));offset+=size
    if offset!=len(payload): raise ValueError('DDS mip payload size is inconsistent')
    rows=['#pragma once','#include <cstdint>',f'inline constexpr unsigned kWaterCausticWidth={width},kWaterCausticHeight={height};',
        'struct WaterCausticMip {unsigned offset,pitch;};','inline constexpr WaterCausticMip kWaterCausticMips[]={']
    rows += [f'{{{start},{pitch}}},' for start,pitch in levels];rows+=['};','inline constexpr unsigned char kWaterCausticBytes[]={']
    rows += [','.join(f'0x{x:02x}' for x in payload[i:i+16])+',' for i in range(0,len(payload),16)];rows+=['};']
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text('\n'.join(rows)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();generate(args.source,args.output)
