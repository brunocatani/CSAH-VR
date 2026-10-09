"""Preserve native DFComposite arithmetic in a bounded cubemap capture kernel.

Only input/output registers, environment/SSLR samples, and implicit gradients
change. The latter become the exact screen-pixel UV gradients from native b2.
The raster coverage mask retains clipping and depth/stencil ownership.
"""
from pathlib import Path
import argparse, re, subprocess, tempfile
from dxbc_transform import (
    OPCODE_CUSTOMDATA, OPCODE_DCL_CONSTANT_BUFFER, OPCODE_DCL_RESOURCE,
    OPCODE_DCL_SAMPLER, OPCODE_DCL_TEMPS, OPCODE_RET,
    OPERAND_CONSTANT_BUFFER, OPERAND_INPUT, OPERAND_OUTPUT, OPERAND_RESOURCE,
    OPERAND_TEMP, DxbcChunk, TransformError, build_dxbc, executable_operands,
    instructions, opcode_extension_end, pack_words, replace_operand_with_temp,
    shader_declarations_and_body, shader_words,
)
import census_linear_lighting_fxp as census

def require(condition, message):
    if not condition:
        raise TransformError(message)

def instruction(opcode, *operands):
    result=[opcode]
    for operand in operands: result.extend(operand)
    result[0] |= len(result)<<24
    return result

def temp(reg, mask=None):
    return [0x00100002 | (mask<<4) if mask is not None else 0x00100e46, reg]

def remap(ins, offset, input0, input1, output):
    def operand_words(op):
        if op.operand_type in (OPERAND_TEMP, OPERAND_INPUT, OPERAND_OUTPUT):
            require(len(op.immediate_indices)==1 and op.immediate_indices[0] is not None, 'indirect native register')
            n=op.immediate_indices[0]
            if op.operand_type==OPERAND_INPUT:
                require(n in (0, 1, 2), 'unexpected native pixel input')
                n=input0 if n==0 else input1
            elif op.operand_type==OPERAND_OUTPUT:
                require(n==0, 'unexpected native pixel output')
                n=output
            else: n+=offset
            return replace_operand_with_temp(ins,op,n)
        result=[]; cursor=op.start
        for nested in op.relative_operands:
            result+=ins[cursor:nested.start]+operand_words(nested); cursor=nested.end
        return result+ins[cursor:op.end]
    result=ins[:opcode_extension_end(ins,0,len(ins))]
    for op in executable_operands(ins,0,len(ins)): result+=operand_words(op)
    result[0]=(result[0]&~(127<<24)) | (len(result)<<24)
    return result

def convert(original, template):
    version,chunks,index,words=shader_words(original)
    tv,tc,ti,tw=shader_words(template)
    nt,ns,nb=shader_declarations_and_body(words)
    tt,ts,tb=shader_declarations_and_body(tw)
    offset=tw[tt[0]+1]; count=words[nt[0]+1]
    input0=offset+count; input1=input0+1; output=input1+1
    declarations=[]
    occupied={}
    grad_x=output+1; grad_y=output+2

    # Every implicit texture sample must still use untouched screen UVs.
    # This makes SampleGrad equivalent even for mipmapped/aniso samplers.
    first=words[nb[0][0]:nb[0][1]]
    operands=executable_operands(first,0,len(first))
    require((first[0]&2047)==0x38 and len(operands)==3, 'native UV producer changed')
    destination,position,scale=operands
    require(destination.operand_type==OPERAND_TEMP and (first[destination.start]&0xfff)==0x32 and
        position.operand_type==OPERAND_INPUT and position.immediate_indices==(0,) and
        (first[position.start]&0xfff)==0x46 and scale.operand_type==OPERAND_CONSTANT_BUFFER and
        scale.immediate_indices==(2,0) and (first[scale.start]&0xfff)==0x46, 'native UV producer changed')
    uv_register=destination.immediate_indices
    uv_untouched=True
    for a,b in nb[1:]:
        ins=words[a:b]; operands=executable_operands(ins,0,len(ins)); opcode=ins[0]&2047
        if opcode==0x45 and operands[2].immediate_indices not in ((8,),(14,)):
            coordinate=operands[1]
            require(uv_untouched and coordinate.operand_type==OPERAND_TEMP and
                coordinate.immediate_indices==uv_register and (ins[coordinate.start]&0xfff)==0x46,
                'implicit sample no longer uses screen-pixel UV gradients')
        if operands:
            destination=operands[0]; token=ins[destination.start]
            if (destination.operand_type==OPERAND_TEMP and destination.immediate_indices==uv_register and
                ((token>>2)&3)==0 and ((token>>4)&3)):
                uv_untouched=False

    def add_declaration(ins):
        op=ins[0]&2047
        if op in (OPCODE_DCL_RESOURCE, OPCODE_DCL_SAMPLER, OPCODE_DCL_CONSTANT_BUFFER):
            key=(op, executable_operands(ins,0,len(ins))[0].immediate_indices[0])
            if key in occupied:
                old=occupied[key]
                if op==OPCODE_DCL_CONSTANT_BUFFER:
                    declarations[old] |= ins[0] & (1<<11)
                    declarations[old+len(ins)-1]=max(declarations[old+len(ins)-1],ins[-1])
                else:
                    require(declarations[old:old+len(ins)]==ins, 'resource declaration conflict')
                return
            occupied[key]=len(declarations)
        declarations.extend(ins)
    for a,b in instructions(words):
        if a>=ns: break
        op=words[a]&2047
        if op in (OPCODE_DCL_RESOURCE,OPCODE_DCL_SAMPLER):
            slot=executable_operands(words,a,b)[0].immediate_indices[0]
            if slot in (8,14): continue
            if op==OPCODE_DCL_RESOURCE: require(slot not in (31,127), 'native resource conflicts with capture')
        if op==OPCODE_DCL_CONSTANT_BUFFER:
            require(executable_operands(words,a,b)[0].immediate_indices[0]!=13, 'native b13 conflicts with capture')
        if op in (0x62,0x63,0x64,0x65,0x66,0x67,OPCODE_DCL_TEMPS): continue
        add_declaration(words[a:b])
    for a,b in instructions(tw):
        if a>=ts: break
        op=tw[a]&2047
        if op==0x6a: continue # global flags already native
        require(op!=OPCODE_CUSTOMDATA, 'template must not own an immediate constant buffer')
        if op in (OPCODE_DCL_RESOURCE,OPCODE_DCL_SAMPLER):
            slot=executable_operands(tw,a,b)[0].immediate_indices[0]
            if slot in (127,15): continue
        if op==OPCODE_DCL_TEMPS: declarations+=instruction(op,[grad_y+1])
        else: add_declaration(tw[a:b])
    result=[tw[0],0]+declarations
    matched=0
    for a,b in tb:
        ins=tw[a:b]
        if (ins[0]&2047)!=0x48:
            result+=ins; continue
        ops=executable_operands(ins,0,len(ins))
        require(ops[2].operand_type==OPERAND_RESOURCE and ops[2].immediate_indices==(127,), 'unexpected template sample')
        matched+=1
        result+=instruction(0x36,temp(input0,15),ins[ops[1].start:ops[1].end])
        # Encode eye as a normal float in the marker. asfloat(1) is a
        # denormal and FXC can fold it to zero before the native insertion.
        result+=instruction(0x1c,temp(input1,15),ins[ops[4].start:ops[4].end])
        result+=instruction(0x36,temp(grad_x,15),[0x4002,0,0,0,0])
        result+=instruction(0x36,temp(grad_y,15),[0x4002,0,0,0,0])
        result+=instruction(0x36,temp(grad_x,1),[0x0020800a,2,0])
        result+=instruction(0x36,temp(grad_y,2),[0x0020801a,2,0])
        for na,nbb in nb:
            ni=words[na:nbb]; opcode=ni[0]&2047
            if opcode==OPCODE_RET:
                require(nbb==len(words), 'native shader has an early return')
                continue
            ni=remap(ni,offset,input0,input1,output)
            operands=executable_operands(ni,0,len(ni))
            if opcode in (0x45,0x48):
                slot=operands[2].immediate_indices[0]
                if slot in (8,14):
                    ni=instruction(0x36,ni[operands[0].start:operands[0].end],[0x4002,0,0,0,0])
                elif opcode==0x45:
                    require(slot in (4,9), 'unexpected native implicit texture sample')
                    ni[0]=(ni[0]&~2047)|0x49
                    ni+=temp(grad_x)+temp(grad_y)
                    ni[0]=(ni[0]&~(127<<24))|(len(ni)<<24)
            result+=ni
        result+=instruction(0x36,ins[ops[0].start:ops[0].end],temp(output))
    require(matched==1, 'template must contain exactly one native sample marker')
    result[1]=len(result)
    return build_dxbc(version,[DxbcChunk(b'SHEX',pack_words(result))])

def generate(root, write):
    fxc=census.find_fxc(None)
    shaders=root/'package/Shaders/Community'
    output=shaders/'IBLCompactCapture'
    originals=sorted((shaders/'VerifiedIBLMaterial').glob('*.dxbc'))
    if len(originals)!=41: raise TransformError('expected all 41 native material variants')
    # Check the actual embedded resource namespace, including generated
    # surface/PBR families, before the Windows resource linker sees it.
    headers='\n'.join((root/'src'/name).read_text() for name in
        ('resources.h','GeneratedSurfaceClassificationResources.h'))
    resource_ids={name:int(value) for name,value in re.findall(r'^#define\s+(\w+)\s+(\d+)\s*$',headers,re.M)}
    manifest='\n'.join((root/'src'/name).read_text() for name in
        ('resources.rc','GeneratedSurfaceClassificationResources.rc'))
    records={}
    for name,path in re.findall(r'^(\w+)\s+RCDATA\s+"([^"]+)"',manifest,re.M):
        number=int(name) if name.isdigit() else resource_ids.get(name)
        require(number is not None,f'unknown resource ID {name}')
        require(number not in records,f'duplicate embedded resource {number}')
        records[number]=path
    first=resource_ids['IDR_IBL_COMPACT_CAPTURE_FIRST_CS']
    for index,path in enumerate(originals):
        require(records.get(first+index)==f'../package/Shaders/Community/IBLCompactCapture/{path.name}',
            f'compact capture resource order differs at {index}')
    with tempfile.TemporaryDirectory(prefix='ibl_compact_') as directory:
        temporary=Path(directory)
        def compile_shader(name, target):
            asset=temporary/(name+'.dxbc')
            subprocess.run([str(fxc),'/nologo','/T',target,'/E','main','/O3','/Ges','/WX','/Fo',str(asset),str(shaders/'IBL'/name)],check=True,capture_output=True)
            return asset.read_bytes()
        template=compile_shader('CaptureRadianceCS.hlsl','cs_5_0')
        generated={output/path.name:convert(path.read_bytes(),template) for path in originals}
        generated[shaders/'IBL/CaptureCoveragePS.dxbc']=compile_shader('CaptureCoveragePS.hlsl','ps_5_0')
        for path,data in generated.items():
            if write:
                path.parent.mkdir(parents=True,exist_ok=True)
                path.write_bytes(data)
            elif not path.is_file() or path.read_bytes()!=data:
                raise TransformError(f'{path.name} does not reproduce its source')
    print('Verified 41 native compact capture kernels and native coverage shader')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    mode=parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--write',action='store_true');mode.add_argument('--check',action='store_true')
    args=parser.parse_args()
    generate(args.root.resolve(),args.write)
