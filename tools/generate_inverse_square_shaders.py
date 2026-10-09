"""Splice area-source falloff into native FO4VR local PS and tiled CS shaders.

Only the proved sqrt/div_sat/power/affine/power attenuation chain is replaced.
Radius consumers, tile culling, cone shape, shadows, BRDF and alpha stay native.
"""
import argparse
from pathlib import Path
import subprocess
import census_linear_lighting_fxp as census
from dxbc_transform import *
from water_parallax_transform import instruction, require


def transform(original, template):
    version,chunks,index,words=shader_words(original)
    if not any(words[a]&2047==OPCODE_DCL_TEMPS for a,b in instructions(words)): return None
    temps,start,body=shader_declarations_and_body(words)
    pattern=[0x4b,0x0e,0x2f,0x38,0x19,0x32,0,0x2f,0x38,0x19]
    matches=[]
    for i in range(len(body)-9):
        if [words[a]&2047 for a,b in body[i:i+10]]!=pattern: continue
        a,b=body[i+8]
        if 0x400ccccd not in words[a:b]: continue # native final 2.2 exponent
        if not words[body[i+1][0]]&(1<<13) or not words[body[i+5][0]]&(1<<13): continue
        matches.append(i)
    if not matches: return None
    require(len(matches)==1,'ambiguous local-light attenuation')
    site=matches[0]
    def ops(i):
        a,b=body[i];return executable_operands(words,a,b)
    attenuation=ops(site+9)[0]
    require(attenuation.operand_type==OPERAND_TEMP,'attenuation no longer in a temporary')
    register=attenuation.immediate_indices[0];mask=(words[attenuation.start]>>4)&15
    require(mask in (1,2,4,8),'attenuation no longer scalar')
    for i in range(site+1,site+10):
        op=ops(i)[0]
        require(op.operand_type==OPERAND_TEMP and op.immediate_indices==(register,) and (words[op.start]>>4)&15==mask,'attenuation data flow changed')
    div=ops(site+1)
    distance=words[div[1].start:div[1].end];radius=words[div[2].start:div[2].end]
    # Capture before div overwrites either the distance or dynamic eye index.
    scratch=words[temps[0]+1];first=scratch+1
    _,_,_,tw=shader_words(template);tt,ts,tb=shader_declarations_and_body(tw)
    prefix=words[:temps[0]];private=[]
    for a,b in instructions(words):
        if a>=start: break
        if words[a]&2047==OPCODE_DCL_RESOURCE:
            require(executable_operands(words,a,b)[0].immediate_indices!=(78,),'native shader owns t78')
    for a,b in instructions(tw):
        if a>=ts: break
        op=tw[a]&2047
        if op==OPCODE_DCL_RESOURCE:
            require(executable_operands(tw,a,b)[0].immediate_indices==(78,),'inverse-square resource changed')
            private+=tw[a:b]
        require(op not in (OPCODE_DCL_CONSTANT_BUFFER,OPCODE_DCL_SAMPLER,OPCODE_CUSTOMDATA),'unexpected inverse-square template declaration')
    require(private,'missing inverse-square settings declaration')
    prefix+=private+instruction(OPCODE_DCL_TEMPS,[first+tw[tt[0]+1]])+words[temps[1]:start]

    def remap(ins):
        def operand(op):
            if op.operand_type in (OPERAND_TEMP,OPERAND_INPUT,OPERAND_OUTPUT):
                require(len(op.immediate_indices)==1 and op.immediate_indices[0] is not None,'indirect temporary')
                n=op.immediate_indices[0]
                if op.operand_type==OPERAND_OUTPUT:
                    require(n in (0,1) and (ins[op.start]>>4)&15==1,'template outputs must be scalar')
                    out=replace_operand_with_temp(ins,op,register if n==0 else scratch);out[0]=(out[0]&~0xf0)|((mask if n==0 else 8)<<4);return out
                if op.operand_type==OPERAND_INPUT:
                    require(n==0,'unexpected template input');return replace_operand_with_temp(ins,op,scratch)
                return replace_operand_with_temp(ins,op,first+n)
            result=[];cursor=op.start
            for child in op.relative_operands:
                result+=ins[cursor:child.start]+operand(child);cursor=child.end
            return result+ins[cursor:op.end]
        result=ins[:opcode_extension_end(ins,0,len(ins))]
        for op in executable_operands(ins,0,len(ins)):result+=operand(op)
        result[0]=(result[0]&~(127<<24))|(len(result)<<24);return result
    snippet=[]
    for a,b in tb:
        if tw[a]&2047==OPCODE_RET: require(b==len(tw),'template early return')
        else: snippet+=remap(tw[a:b])
    lane=mask.bit_length()-1
    cutoffSite=None
    if words[0]>>16==0: # pixel shaders retain their native early-out predicate
        for i in range(site+10,min(len(body),site+20)):
            a,b=body[i]
            if words[a]&2047!=0x1d or 0x3a83126f not in words[a:b]: continue
            operands=ops(i)
            if len(operands)==3 and operands[2].operand_type==OPERAND_TEMP and operands[2].immediate_indices==(register,):
                require(cutoffSite is None,'ambiguous local-light cutoff');cutoffSite=i
    if words[0]>>16==0: require(cutoffSite is not None, "native local-light cutoff missing")
    for i,(a,b) in enumerate(body):
        if i==site+1:
            prefix+=instruction(0x36,[0x00100012,scratch],distance)
            prefix+=instruction(0x36,[0x00100022,scratch],radius)
        if i==cutoffSite:
            operands=ops(i);threshold=operands[1]
            require(threshold.operand_type==OPERAND_IMMEDIATE32,'local-light threshold changed')
            ins=words[a:threshold.start]+[0x0010003a,scratch]+words[threshold.end:b]
            ins[0]=(ins[0]&~(127<<24))|(len(ins)<<24);prefix+=ins
        else: prefix+=words[a:b]
        if i==site+9:
            prefix+=instruction(0x36,[0x00100042,scratch],[0x0010000a|(lane<<4),register])+snippet
    prefix[1]=len(prefix);out=list(chunks);out[index]=DxbcChunk(chunks[index].tag,pack_words(prefix))
    return build_dxbc(version,out)


def generate(root,fxc,output):
    output.parent.mkdir(parents=True,exist_ok=True)
    template=output.parent/'Attenuation.dxbc'
    subprocess.run([str(fxc),'/nologo','/T','ps_5_0','/E','main','/O3','/Ges','/WX','/Fo',str(template),str(root/'package/Shaders/Community/InverseSquareLighting/Attenuation.hlsl')],check=True)
    inventory=census.parse_fxp((root/'Shaders012_VR.fxp').read_bytes())
    pairs={}
    for item in inventory.containers:
        if (item.family,item.stage) not in (('DFLight','PS'),('DFTiledLighting','CS')):continue
        candidate=transform(item.data,template.read_bytes())
        if candidate is not None:pairs[item.data]=(candidate,item.stage,item.key)
    require(any(stage=='CS' for _,stage,_ in pairs.values()) and any(stage=='PS' for _,stage,_ in pairs.values()),'local-light family coverage missing')
    rows=['#pragma once','#include <cstddef>','struct InverseSquareShader {const unsigned char* original;std::size_t originalSize;const unsigned char* replacement;std::size_t replacementSize;bool compute;};']
    for i,(original,(candidate,stage,key)) in enumerate(pairs.items()):
        for label,data in [('Original',original),('Replacement',candidate)]:
            rows.append(f'inline constexpr unsigned char kInverseSquare{label}{i}[]={{')
            rows.extend(','.join(f'0x{x:02x}' for x in data[a:a+16])+',' for a in range(0,len(data),16));rows.append('};')
        (output.parent/f'{stage}_{key:08X}_original.dxbc').write_bytes(original)
        (output.parent/f'{stage}_{key:08X}_replacement.dxbc').write_bytes(candidate)
    rows.append('inline constexpr InverseSquareShader kInverseSquareShaders[]={')
    for i,(_,(_,stage,key)) in enumerate(pairs.items()):rows.append(f'{{kInverseSquareOriginal{i},sizeof(kInverseSquareOriginal{i}),kInverseSquareReplacement{i},sizeof(kInverseSquareReplacement{i}),'+('true' if stage=='CS' else 'false')+'},')
    rows.append('};');output.write_text('\n'.join(rows)+'\n')
    print(f'Inverse-square contracts: {sum(s=="PS" for _,s,_ in pairs.values())} pixel and {sum(s=="CS" for _,s,_ in pairs.values())} compute shaders')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True);p.add_argument('--fxc',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();generate(a.root,a.fxc,a.output)
