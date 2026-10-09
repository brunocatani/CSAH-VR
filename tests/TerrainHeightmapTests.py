"""Exercise real plugin framing, master remapping, overrides and native deltas."""
import sys
from pathlib import Path
import struct
import tempfile
import zlib
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from export_terrain_heightmaps import decode_height, read_plugin, export, HEADER, MISSING

def sub(tag,data): return tag+struct.pack('<H',len(data))+data
def record(tag,key,body,flags=0):
    if flags&0x40000: body=struct.pack('<I',len(body))+zlib.compress(body)
    return struct.pack('<4sIII8x',tag,len(body),flags,key)+body
def group(kind,label,body): return struct.pack('<4sIIi8x',b'GRUP',24+len(body),label,kind)+body
def plugin(height,master=None,deleted=False):
    header=record(b'TES4',0,sub(b'HEDR',struct.pack('<fII',1,3,0x800))+(sub(b'MAST',master.encode()+b'\0') if master else b''))
    world=record(b'WRLD',0x800,sub(b'EDID',b'FixtureWorld\0'))
    cell=record(b'CELL',0x801,sub(b'DATA',b'\0\0')+sub(b'XCLC',struct.pack('<iiI',-2,3,0)))
    land=record(b'LAND',0x802,sub(b'VHGT',struct.pack('<f',height)+bytes(1092)),0x20 if deleted else 0x40000)
    return header+group(0,int.from_bytes(b'WRLD','little'),world+group(1,0x800,cell+group(6,0x801,group(9,0x801,land))))

output=Path(sys.argv[1]);output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=output) as directory:
    root=Path(directory);base=root/'Base.esm';override=root/'Override.esp'
    base.write_bytes(plugin(10));override.write_bytes(plugin(20,'Base.esm'))
    worlds,cells,lands={},{},{}
    read_plugin(base,worlds,cells,lands);read_plugin(override,worlds,cells,lands)
    assert len(lands)==1 and lands[('base.esm',0x802)][2]=='Override.esp'
    report=export([base,override],root/'maps')
    data=(root/'maps/FixtureWorld.cshf').read_bytes()
    assert HEADER.unpack_from(data)==(b'CSAHHF01',9,9,-2,3,8,0)
    assert np.all(np.frombuffer(data,dtype='<f4',offset=32)==160)
    override.write_bytes(plugin(0,'Base.esm',True))
    assert not export([base,override],root/'maps')['worlds']
    assert not (root/'maps/FixtureWorld.cshf').exists()
    delta=np.zeros((33,33),dtype=np.int8);delta[0,0]=2;delta[0,1]=-3;delta[1,0]=5;delta[1,2]=4
    heights=decode_height(struct.pack('<f',10.5)+delta.tobytes()+bytes(3))
    assert heights[0,0]==96 and heights[0,1]==72 and heights[1,0]==136 and heights[1,2]==168
    assert heights[2,0]==136 # next row follows the first vertex, not the last
    try: decode_height(bytes(100))
    except ValueError: pass
    else: raise AssertionError('truncated VHGT accepted')
    broken=root/'Broken.esm';broken.write_bytes(plugin(0)[:-1])
    try: read_plugin(broken,{},{},{})
    except ValueError: pass
    else: raise AssertionError('truncated group accepted')
print('LAND decode, signed deltas, negative cells, master remapping, compressed overrides and deletion passed')
