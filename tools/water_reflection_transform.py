"""Replace the native water sky-gradient value before tint, Fresnel and SSLR.

Only contracts with an existing stereo origin and a verified reflection-normal
producer participate. All later native blending and output channels are kept.
"""
from dxbc_transform import (
    OPCODE_CUSTOMDATA,OPCODE_DCL_CONSTANT_BUFFER,OPCODE_DCL_RESOURCE,OPCODE_DCL_SAMPLER,
    OPCODE_DCL_TEMPS,OPCODE_RET,OPERAND_CONSTANT_BUFFER,OPERAND_INPUT,OPERAND_OUTPUT,OPERAND_RESOURCE,
    OPERAND_TEMP,DxbcChunk,build_dxbc,executable_operands,instructions,opcode_extension_end,
    pack_words,replace_operand_with_temp,shader_declarations_and_body,shader_words,
)
from water_parallax_transform import require,signature,instruction

def apply(original,template,flagTemplate,farFlagTemplate):
    version,chunks,index,words=shader_words(original)
    temps,start,body=shader_declarations_and_body(words)
    sig=signature(chunks)
    if ('TEXCOORD',0) not in sig or ('EYEPOS',0) not in sig: return original
    if sig[('TEXCOORD',0)].read_write_mask&7!=7 or sig[('EYEPOS',0)].read_write_mask&1!=1: return original
    constants={};resources=set();samplers=set()
    for a,b in instructions(words):
        if a>=start: break
        opcode=words[a]&2047
        if opcode not in (OPCODE_DCL_CONSTANT_BUFFER,OPCODE_DCL_RESOURCE,OPCODE_DCL_SAMPLER): continue
        indices=executable_operands(words,a,b)[0].immediate_indices
        if opcode==OPCODE_DCL_CONSTANT_BUFFER: constants[indices[0]]=indices[1]
        elif opcode==OPCODE_DCL_RESOURCE: resources.add(indices[0])
        else: samplers.add(indices[0])
    if constants.get(12,0)<61: return original
    position=sig.get(('SV_POSITION',0))
    sceneAvailable=position is not None and position.read_write_mask&3==3 and {1,7}.issubset(resources)
    privateResources={50,51,52,*range(69,76),79,80}
    require(not resources.intersection(privateResources) and 14 not in samplers,'water reflection resource collision')
    def cb(op,row): return op.operand_type==OPERAND_CONSTANT_BUFFER and op.immediate_indices==(1,row)
    def temp(op): return op.immediate_indices[0] if op.operand_type==OPERAND_TEMP and len(op.immediate_indices)==1 else None
    sites=[]
    for i,(a,b) in enumerate(body):
        if words[a]&2047!=0: continue
        ops=executable_operands(words[a:b],0,b-a)
        if len(ops)==3 and cb(ops[1],3) and cb(ops[2],4): sites.append(i)
    if not sites: return original
    require(len(sites)==1,'water sky gradient is ambiguous')
    site=sites[0]
    def row(i):
        a,b=body[i];return words[a:b]
    def operands(i):
        ins=row(i);return executable_operands(ins,0,len(ins))
    firstMad=site+1;firstOps=operands(firstMad)
    require(row(firstMad)[0]&2047==0x32 and cb(firstOps[3],3),'water first sky gradient changed')
    secondSites=[i for i in range(firstMad+1,min(len(body),firstMad+8))
        if row(i)[0]&2047==0 and len(operands(i))==3 and cb(operands(i)[2],5)]
    require(len(secondSites)==1,'water second sky gradient changed')
    second=secondSites[0];lastMad=second+1;lastOps=operands(lastMad)
    require(row(lastMad)[0]&2047==0x32,'water final sky gradient changed')
    gradientFirst=temp(operands(site)[0]);gradientSecond=temp(operands(second)[0]);colour=temp(lastOps[0])
    require(colour is not None and temp(firstOps[0])==gradientFirst and temp(firstOps[2])==gradientFirst and
        temp(operands(second)[1])==gradientFirst and temp(lastOps[2])==gradientSecond and temp(lastOps[3])==gradientFirst,
        'water sky gradient data flow changed')
    def source_lane(ins,op,lane=0):
        require(temp(op) is not None,'water slope source is not a temporary')
        token=ins[op.start];mode=(token>>2)&3
        require(mode in (1,2),'water slope component selection changed')
        return temp(op),((token>>(4+(2*lane if mode==1 else 0)))&3)
    def writer(before,source):
        register,lane=source
        for i in range(before-1,-1,-1):
            op=operands(i)
            if op and temp(op[0])==register and ((row(i)[op[0].start]>>4)&15)&(1<<lane): return i
        raise RuntimeError('water slope producer missing')
    slopeA=source_lane(row(firstMad),firstOps[1]);slopeB=source_lane(row(lastMad),lastOps[1])
    wa=writer(firstMad,slopeA);wb=writer(lastMad,slopeB)
    require(row(wa)[0]&2047==0 and row(wb)[0]&2047==0x32,'water gradient slope functions changed')
    reflectionA=source_lane(row(wa),operands(wa)[1],slopeA[1]);reflectionB=source_lane(row(wb),operands(wb)[1],slopeB[1])
    reflectionSite=writer(wa,reflectionA)
    require(writer(wb,reflectionB)==reflectionSite and row(reflectionSite)[0]&2047==0x32,'water sky slopes no longer share a reflected ray')
    reflectionOps=operands(reflectionSite);normal=temp(reflectionOps[1])
    require(normal is not None and reflectionSite>=2,'water reflection normal unavailable')
    double=reflectionSite-1;dot=reflectionSite-2
    require(row(double)[0]&2047==0 and row(dot)[0]&2047==0x10,'water reflection dot/double changed')
    dotOps=operands(dot);doubleOps=operands(double)
    normalOperands=[op for op in dotOps[1:] if temp(op)==normal]
    require(len(normalOperands)==1,'water reflection normal ambiguous')
    normalOperand=normalOperands[0];normalWords=row(dot)[normalOperand.start:normalOperand.end]
    require(temp(doubleOps[1])==temp(dotOps[0]) and temp(doubleOps[2])==temp(dotOps[0]) and
        temp(reflectionOps[2])==temp(doubleOps[0]),'water reflection projection data flow changed')
    _,tc,_,tw=shader_words(template);tt,ts,tb=shader_declarations_and_body(tw);templateSig=signature(tc)
    normalScratch=words[temps[0]+1];screenScratch=normalScratch+1;fxScratch=normalScratch+2;first=normalScratch+3;templateCount=tw[tt[0]+1]
    remappedInputs={}
    for key,value in templateSig.items():
        if key==('TEXCOORD',7): remappedInputs[value.register]=('temp',screenScratch)
        elif key==('TEXCOORD',8): remappedInputs[value.register]=('temp',normalScratch)
        elif key==('TEXCOORD',9): remappedInputs[value.register]=('temp',colour)
        else:
            require(key in sig,'water reflection input missing')
            remappedInputs[value.register]=('input',sig[key].register)
    _,_,_,fw=shader_words(flagTemplate if sceneAvailable else farFlagTemplate);ft,fs,fb=shader_declarations_and_body(fw)
    def remap(ins,base=first,flag=False):
        def operand(op):
            if op.operand_type in (OPERAND_TEMP,OPERAND_INPUT,OPERAND_OUTPUT):
                require(len(op.immediate_indices)==1 and op.immediate_indices[0] is not None,'indirect water reflection temporary')
                n=op.immediate_indices[0]
                if op.operand_type==OPERAND_INPUT:
                    kind,value=remappedInputs[n]
                    if kind=='input':
                        result=list(ins[op.start:op.end]);result[-1]=value;return result
                    return replace_operand_with_temp(ins,op,value)
                if op.operand_type==OPERAND_OUTPUT:
                    if flag:
                        require(n==0 and ((ins[op.start]>>4)&15)==1,"FX flag output changed")
                        return replace_operand_with_temp(ins,op,fxScratch)
                    require((n==0 and ((ins[op.start]>>4)&15)==7) or (n==1 and ((ins[op.start]>>4)&15)==1),'water reflection output contract changed')
                    return replace_operand_with_temp(ins,op,colour if n==0 else fxScratch)
                return replace_operand_with_temp(ins,op,base+n)
            result=[];cursor=op.start
            for child in op.relative_operands:
                result+=ins[cursor:child.start]+operand(child);cursor=child.end
            return result+ins[cursor:op.end]
        result=ins[:opcode_extension_end(ins,0,len(ins))]
        for op in executable_operands(ins,0,len(ins)): result+=operand(op)
        result[0]=(result[0]&~(127<<24))|(len(result)<<24);return result
    prefix=words[:temps[0]];private=set()
    for a,b in instructions(tw):
        if a>=ts: break
        opcode=tw[a]&2047
        require(opcode!=OPCODE_CUSTOMDATA,'water reflection template introduced immediate table')
        if opcode==OPCODE_DCL_RESOURCE:
            slot=executable_operands(tw,a,b)[0].immediate_indices[0]
            require(slot in privateResources,'water reflection texture footprint changed')
            prefix+=tw[a:b];private.add(slot)
        elif opcode==OPCODE_DCL_SAMPLER:
            require(executable_operands(tw,a,b)[0].immediate_indices==(14,),'water reflection sampler footprint changed');prefix+=tw[a:b]
        elif opcode==OPCODE_DCL_CONSTANT_BUFFER:
            indices=executable_operands(tw,a,b)[0].immediate_indices
            require(indices in ((12,61),(0,1)) and constants.get(indices[0],0)>=indices[1],'water reflection camera/viewport footprint changed')
    require(private==privateResources,'water reflection inputs incomplete')
    prefix+=instruction(OPCODE_DCL_TEMPS,[first+templateCount+fw[ft[0]+1]])+words[temps[1]:start]
    transformed=[]
    for a,b in tb:
        if tw[a]&2047==OPCODE_RET: require(b==len(tw),'water reflection template early return')
        else: transformed+=remap(tw[a:b])
    for a,b in fb:
        if fw[a]&2047==OPCODE_RET: require(b==len(fw),"FX flag early return")
        else: prefix+=remap(fw[a:b],first+templateCount,True)
    capture=body[dot][0];inject=body[lastMad][0]
    for a,b in body:
        if a==capture:
            prefix+=instruction(0x36,[0x001000f2,screenScratch],[0x00004002,0,0,0x3f800000 if sceneAvailable else 0,0])
            if sceneAvailable: prefix+=instruction(0x36,[0x00100032,screenScratch],[0x00101e46,position.register])
        if a==capture: prefix+=instruction(0x36,[0x00100072,normalScratch],normalWords)
        prefix+=words[a:b]
        if a==inject: prefix+=transformed
        # The native t9/t10 packet describes the opaque receiver behind water.
        # FX mode owns its own water ray; a miss falls back to the base/cube,
        # rather than reintroducing an unrelated opaque reflection.
        opcode=words[a]&2047
        if opcode in (0x45,0x48):
            operands=executable_operands(words,a,b)
            nativeReflection=any(op.operand_type==OPERAND_RESOURCE and op.immediate_indices in ((9,),(10,)) for op in operands)
            if nativeReflection:
                dest=operands[0];require(dest.operand_type==OPERAND_TEMP,'native water reflection destination changed')
                prefix+=instruction(0x37,words[dest.start:dest.end],[0x0010000a,fxScratch],[0x00004001,0],[0x00100e46,dest.immediate_indices[0]])
    prefix[1]=len(prefix);out=list(chunks);out[index]=DxbcChunk(chunks[index].tag,pack_words(prefix))
    return build_dxbc(version,out)
