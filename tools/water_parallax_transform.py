"""Splice bounded water-normal sampling into the native VR pixel contracts.

Original samples remain intact. A uniform selection replaces their result only
when the private parallax producer is active, preserving the disabled path.
"""
from dxbc_transform import (
    OPCODE_CUSTOMDATA, OPCODE_DCL_CONSTANT_BUFFER, OPCODE_DCL_RESOURCE,
    OPCODE_DCL_SAMPLER, OPCODE_DCL_TEMPS, OPCODE_RET,
    OPERAND_CONSTANT_BUFFER, OPERAND_INPUT, OPERAND_OUTPUT, OPERAND_RESOURCE,
    OPERAND_TEMP, DxbcChunk, TransformError, build_dxbc, executable_operands,
    instructions, opcode_extension_end, pack_words, replace_operand_with_temp,
    shader_declarations_and_body, shader_words,
    flatten_operands,
)
from generate_surface_classification_shaders import parse_signature

def require(condition,message):
    if not condition: raise TransformError(message)

def signature(chunks):
    chunk=next(c for c in chunks if c.tag==b'ISGN')
    return {(e.name.upper(),e.semantic_index):e for e in parse_signature(chunk.payload)[1]}

def verify_vertex_pair(inventory,descriptors):
    """Verify the existing VR VS producer for every parallax PS alias."""
    vertices={item.key:item for item in inventory.containers if item.family=='Water' and item.stage=='VS'}
    for descriptor in descriptors:
        require(descriptor in vertices,f'Water parallax has no native VS pair for {descriptor:08X}')
        _,_,_,words=shader_words(vertices[descriptor].data)
        reads=set()
        for a,b in instructions(words):
            if words[a]&2047 in (OPCODE_DCL_CONSTANT_BUFFER,OPCODE_CUSTOMDATA): continue
            for operand in flatten_operands(executable_operands(words,a,b)):
                if operand.operand_type==OPERAND_CONSTANT_BUFFER: reads.add(operand.immediate_indices)
        require((1,2) in reads and (2,14) in reads,
            f'Water VS {descriptor:08X} does not expose native normal scales and world-UV selection')

def instruction(opcode,*operands):
    result=[opcode]
    for operand in operands: result.extend(operand)
    result[0]|=len(result)<<24
    return result

def apply(original,template):
    version,chunks,index,words=shader_words(original)
    temps,start,body=shader_declarations_and_body(words)
    sig=signature(chunks)
    required=(('TEXCOORD',0),('TEXCOORD',1),('TEXCOORD',2))
    if not all(key in sig for key in required): return original
    samples={slot:[] for slot in (4,5,6)}
    for a,b in body:
        ins=words[a:b]
        if (ins[0]&2047)!=0x45: continue
        ops=executable_operands(ins,0,len(ins))
        if ops[2].immediate_indices[0] in samples: samples[ops[2].immediate_indices[0]].append((a,b,ops))
    if any(len(items)!=1 for items in samples.values()): return original
    for slot,items in samples.items():
        a,b,ops=items[0];coordinate=ops[1]
        expected=sig[('TEXCOORD',1 if slot<6 else 2)].register
        # Only native world-UV routes. Displacement/flow reconstruction with
        # other coordinates keeps its original sampler and shader arithmetic.
        if coordinate.operand_type!=OPERAND_INPUT or coordinate.immediate_indices!=(expected,): return original
        expected_swizzle=0xae6 if slot==5 else 0x46  # zwzz / xyxx
        if words[a+coordinate.start]&0xfff!=expected_swizzle: return original
    _,templateChunks,_,tw=shader_words(template)
    ttemps,tstart,tBody=shader_declarations_and_body(tw)
    tsig=signature(templateChunks)
    input_map={v.register:sig[k].register for k,v in tsig.items() if k in sig}
    first=words[temps[0]+1];template_count=tw[ttemps[0]+1];outputs=first+template_count
    def remap(ins):
        def operand(op):
            if op.operand_type in (OPERAND_TEMP,OPERAND_OUTPUT,OPERAND_INPUT):
                require(len(op.immediate_indices)==1 and op.immediate_indices[0] is not None,'water template indirect register')
                n=op.immediate_indices[0]
                if op.operand_type==OPERAND_INPUT:
                    require(n in input_map,'water template input missing from native signature')
                    result=list(ins[op.start:op.end]);result[-1]=input_map[n];return result
                return replace_operand_with_temp(ins,op,first+n if op.operand_type==OPERAND_TEMP else outputs+n)
            result=[];cursor=op.start
            for child in op.relative_operands:
                result+=ins[cursor:child.start]+operand(child);cursor=child.end
            return result+ins[cursor:op.end]
        result=ins[:opcode_extension_end(ins,0,len(ins))]
        for op in executable_operands(ins,0,len(ins)): result+=operand(op)
        result[0]=(result[0]&~(127<<24))|(len(result)<<24)
        return result
    occupied=set();prefix=words[:temps[0]]
    for a,b in instructions(words):
        if a>=start: break
        if words[a]&2047==OPCODE_DCL_RESOURCE:
            occupied.add(executable_operands(words,a,b)[0].immediate_indices[0])
    require(not occupied.intersection((66,67,68)),'native water owns private parallax resources')
    for a,b in instructions(tw):
        if a>=tstart: break
        opcode=tw[a]&2047
        require(opcode!=OPCODE_CUSTOMDATA,'water template introduced an immediate constant buffer')
        if opcode==OPCODE_DCL_RESOURCE:
            slot=executable_operands(tw,a,b)[0].immediate_indices[0]
            if slot in (66,67,68): prefix+=tw[a:b]
            else: require(slot in (4,5,6),'water template texture footprint changed')
        elif opcode==OPCODE_DCL_CONSTANT_BUFFER:
            require(executable_operands(tw,a,b)[0].immediate_indices==(1,10),'water template material constant footprint changed')
        elif opcode==OPCODE_DCL_SAMPLER:
            require(executable_operands(tw,a,b)[0].immediate_indices[0] in (4,5,6),'water template sampler footprint changed')
    prefix+=instruction(OPCODE_DCL_TEMPS,[outputs+4])
    transform=[]
    for a,b in tBody:
        if tw[a]&2047==OPCODE_RET:
            require(b==len(tw),'water template early return')
        else: transform+=remap(tw[a:b])
    # Keep native immediate constant buffers and other post-temp declarations.
    prefix+=words[temps[1]:start]
    prefix+=transform
    offsets={items[0][0]:slot for slot,items in samples.items()}
    for a,b in body:
        ins=words[a:b];prefix+=ins
        if a in offsets:
            slot=offsets[a];ops=executable_operands(ins,0,len(ins));destination=ops[0]
            replacement=replace_operand_with_temp(ins,ops[2],outputs+slot-4)
            register=destination.immediate_indices[0]
            prefix+=instruction(0x37,ins[destination.start:destination.end],
                [0x0010000a,outputs+3],replacement,[0x00100e46,register])
    prefix[1]=len(prefix)
    result=list(chunks);result[index]=DxbcChunk(chunks[index].tag,pack_words(prefix))
    return build_dxbc(version,result)
