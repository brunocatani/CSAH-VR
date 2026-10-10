"""Native local-light material composition; retains light culling, shape and shadows."""
from dxbc_transform import *
from water_parallax_transform import instruction, require
from generate_contact_shadow_shader import replace_instruction_operands

MOV,MUL,DP2,DP3,RSQ,LOOP,MAD,DIV=0x36,0x38,0x0f,0x10,0x44,0x30,0x32,0x0e

def temp(n, mask=15, swizzle=0xe4, destination=False):
    return [(0x00100002 | (mask<<4)) if destination else (0x00100006 | (swizzle<<4)),n]
def immediate(*values):
    return [0x00004002,*values] if len(values)==4 else [0x00004001,*values]
def mov(n,source,mask=15):return instruction(MOV,temp(n,mask,destination=True),source)
def reg(op):
    require(op.operand_type==OPERAND_TEMP and len(op.immediate_indices)==1,'expected native local-light temporary')
    return op.immediate_indices[0]

def augment(original, init, lighting):
    version,chunks,chunkIndex,words=shader_words(original)
    temps,start,body=shader_declarations_and_body(words);compute=words[0]>>16==5
    def ops(i):
        a,b=body[i];return executable_operands(words,a,b)
    def raw(op):return words[op.start:op.end]
    def code(i):return words[body[i][0]]&2047
    def mask(op):return (words[op.start]>>4)&15
    nativeTemps=words[temps[0]+1]
    # Pixel, immutable normal/view/light, light colour/control and material terms.
    pixel,normal,view,light,colour,control,material,outD,outS=range(nativeTemps,nativeTemps+9)
    # Material occupies three float4 registers; outputs and template scratch follow it.
    outD=material+3;outS=material+4;cookie=material+5;scratch=material+6
    additions={};declarations={};maximum=0
    def add(i,codeBytes):additions.setdefault(i,[]).extend(codeBytes)
    def instantiate(data,inputs,outputs):
        nonlocal maximum
        _,_,_,tw=shader_words(data);tt,ts,tb=shader_declarations_and_body(tw)
        maximum=max(maximum,tw[tt[0]+1]);result=[]
        for a,b in instructions(tw):
            if a>=ts:break
            op=tw[a]&2047
            if op in (OPCODE_DCL_RESOURCE,OPCODE_DCL_CONSTANT_BUFFER):
                arg=executable_operands(tw,a,b)[0];slot=arg.immediate_indices[0]
                # t78 is supplied exactly once by the combined attenuation transform.
                if op==OPCODE_DCL_RESOURCE and slot==78:continue
                key=(op,slot);declarations[key]=tw[a:b]
            require(op not in (OPCODE_DCL_SAMPLER,OPCODE_CUSTOMDATA),'unexpected local-material template declaration')
        for a,b in tb:
            ins=tw[a:b]
            if ins[0]&2047==OPCODE_RET:
                require(b==len(tw),'unexpected local-material early return');continue
            replacements={}
            for op in flatten_operands(executable_operands(ins,0,len(ins))):
                if op.operand_type in (OPERAND_TEMP,OPERAND_INPUT,OPERAND_OUTPUT):
                    require(len(op.immediate_indices)==1 and op.immediate_indices[0] is not None,'relative local-material temporary')
                    index=op.immediate_indices[0]
                    target=scratch+index if op.operand_type==OPERAND_TEMP else (inputs if op.operand_type==OPERAND_INPUT else outputs)[index]
                    replacements[op.start]=replace_operand_with_temp(ins,op,target)
            result+=replace_instruction_operands(ins,replacements)
        return result
    initCode=instantiate(init,{0:pixel},{0:material,1:material+1,2:material+2})
    # Decode anchors are proved by the complete native normal reconstruction chain.
    normalSite=None;normalReg=None;viewSite=None;viewReg=None;lightSite=None;lightOperand=None;lightCandidates=[]
    for i in range(len(body)-5):
        if [code(j) for j in range(i,i+6)]==[0x32,DP2,0x32,0x4b,MUL,MOV]:
            if 0x40800000 in words[body[i][0]:body[i][1]] and 0xc0000000 in words[body[i][0]:body[i][1]]:
                require(normalSite is None,'ambiguous normal decode');normalSite=i+5;normalReg=reg(ops(i+5)[0])
        if [code(j) for j in range(i,i+3)]==[DP3,RSQ,MUL]:
            a=ops(i)
            if len(a)==3 and raw(a[1])==raw(a[2]) and raw(a[1])[0]&0x80000000 and (raw(a[1])[1]&0x3fc0)==0x40:
                viewSite=i+2;viewReg=reg(ops(i+2)[0])
        if code(i)==RSQ and code(i+1)==MUL:
            o=ops(i+1)
            if len(o)==3 and mask(o[0]) in (7,14) and any(p.operand_type==OPERAND_TEMP and reg(o[0])==reg(p) and not raw(p)[0]&0x80000000 for p in o[1:]):
                lightCandidates.append((i+1,temp(reg(o[0]),swizzle=0xe4 if mask(o[0])==7 else 0xf9)))
    if lightCandidates:
        # The first non-negative in-place normalization is the receiver-to-light
        # vector. Later normalizations construct the half/reflection vector.
        lightSite,lightOperand=lightCandidates[0]
    if compute:
        loops=[i for i in range(len(body)) if code(i)==LOOP];require(len(loops)==1,'tiled native light loop changed')
        require(normalReg is not None and viewReg is not None and lightSite is not None,'tiled normal/view/light anchors missing')
        accum=[]
        for i in range(len(body)):
            o=ops(i)
            if code(i)==MAD and len(o)==4 and o[0].operand_type==OPERAND_TEMP and o[3].operand_type==OPERAND_TEMP and reg(o[0])==reg(o[3]) and mask(o[0])==7:
                # The two final native sums share the distance attenuation scalar.
                if i+1<len(body) and code(i+1)==MAD:accum.append(i)
        require(len(accum)==1,'tiled diffuse/specular accumulators changed')
        site=accum[0];d=reg(ops(site)[1]);s=reg(ops(site+1)[1])
        colourLoad=[];flagsLoad=[]
        for i,(a,b) in enumerate(body):
            o=ops(i)
            if any(p.operand_type==OPERAND_RESOURCE and p.immediate_indices==(6,) for p in o):
                if len(o)==4 and o[2].operand_type==OPERAND_IMMEDIATE32:
                    offset=raw(o[2])[-1]
                    if offset==36:colourLoad.append(i)
                    if offset==0:flagsLoad.append(i)
        require(len(colourLoad)==1 and len(flagsLoad)==1,'tiled light record layout changed')
        colourReg=reg(ops(colourLoad[0])[0]);flagsReg=reg(ops(flagsLoad[0])[0])
        add(loops[0],instruction(0x56,temp(pixel,3,destination=True),temp(1,swizzle=0x04))+mov(normal,temp(normalReg))+mov(view,temp(viewReg))+initCode)
        add(colourLoad[0]+1,mov(colour,temp(colourReg)))
        add(flagsLoad[0]+1,mov(control,temp(flagsReg,swizzle=0))+mov(light,immediate(0,0,0x3f800000,0)))
        add(lightSite+1,mov(light,lightOperand,7))
        evalCode=instantiate(lighting,{0:d,1:s,2:normal,3:view,4:light,5:colour,6:control,7:material,8:material+1,9:material+2},{0:d,1:s})
        add(site,evalCode)
    else:
        require(all(op.immediate_indices in ((0,),(1,)) for a,b in body for op in executable_operands(words,a,b) if op.operand_type==OPERAND_OUTPUT),'local light gained another render target')
        spec=any(code(i)==MUL and ops(i)[0].operand_type==OPERAND_OUTPUT and ops(i)[0].immediate_indices==(1,) for i in range(len(body)))
        diffuseWrites=[i for i in range(len(body)) if code(i)==DIV and ops(i)[0].operand_type==OPERAND_OUTPUT and ops(i)[0].immediate_indices==(0,)]
        require(len(diffuseWrites)==1,'local diffuse scale changed')
        finalReg=reg(ops(diffuseWrites[0])[1]);scales=[i for i in range(diffuseWrites[0]) if code(i)==MUL and ops(i)[0].operand_type==OPERAND_TEMP and reg(ops(i)[0])==finalReg and mask(ops(i)[0]) in (7,14)]
        require(scales,'local attenuation output missing');scaleSite=scales[-1];factor=raw(ops(scaleSite)[1])
        colours=[op for a,b in body for op in executable_operands(words,a,b) if op.operand_type==OPERAND_CONSTANT_BUFFER and op.immediate_indices==(2,3)]
        require(colours,'local light colour missing')
        positions=[i for i in range(len(body)) if code(i)==DIV and ops(i)[0].operand_type==OPERAND_TEMP and mask(ops(i)[0]) in (7,14)]
        require(positions,'native view position missing')
        viewSite=positions[0];viewReg=reg(ops(viewSite)[0])
        ambient=normalReg is None or lightSite is None
        if not ambient:
            add(normalSite+1,mov(normal,temp(normalReg)))
            add(viewSite+1,mov(view,[0x80100006|((0xe4 if mask(ops(viewSite)[0])==7 else 0xf9)<<4),0x41,viewReg]))
            add(lightSite+1,mov(light,lightOperand,7))
        else:add(0,mov(normal,immediate(0,0,0x3f800000,0))+mov(view,immediate(0,0,0x3f800000,0))+mov(light,immediate(0,0,0x3f800000,0)))
        colourOperand=raw(colours[0]);colourOperand[0]=(colourOperand[0]&~0xff0)|(0xe4<<4)
        add(0,mov(cookie,immediate(0x3f800000,0x3f800000,0x3f800000,0x3f800000)))
        cookieSamples=[i for i in range(len(body)) if any(p.operand_type==OPERAND_RESOURCE and p.immediate_indices==(7,) for p in ops(i))]
        require(len(cookieSamples)<=1,'native projected colour changed')
        if cookieSamples:
            sample=cookieSamples[0];destination=ops(sample)[0];require(mask(destination) in (7,11),'projected colour mask changed')
            add(sample+1,mov(cookie,temp(reg(destination),swizzle=0xe4 if mask(destination)==7 else 0xf4),7))
        add(scaleSite,instruction(MUL,temp(colour,7,destination=True),colourOperand,factor)+instruction(MUL,temp(colour,7,destination=True),temp(colour),temp(cookie)))
        end=len(body)-1;require(code(end)==OPCODE_RET,'local final return changed')
        add(end,mov(pixel,[0x00101e46,0])+mov(control,immediate((16 if spec else 0)|(8 if ambient else 0)))+initCode)
        add(end,instantiate(lighting,{0:outD,1:outS,2:normal,3:view,4:light,5:colour,6:control,7:material,8:material+1,9:material+2},{0:outD,1:outS}))
    prefix=words[:temps[0]]
    for a,b in instructions(words):
        if a>=start:break
        op=words[a]&2047
        if op in (OPCODE_DCL_RESOURCE,OPCODE_DCL_CONSTANT_BUFFER):
            slot=executable_operands(words,a,b)[0].immediate_indices[0]
            require((op,slot) not in declarations,'native shader owns a private local-material binding')
    for decl in declarations.values():prefix+=decl
    prefix+=instruction(OPCODE_DCL_TEMPS,[scratch+maximum])+words[temps[1]:start]
    for i,(a,b) in enumerate(body):
        prefix+=additions.get(i,[]);ins=words[a:b]
        if not compute:
            replacements={op.start:replace_operand_with_temp(ins,op,outD if op.immediate_indices==(0,) else outS) for op in executable_operands(ins,0,len(ins)) if op.operand_type==OPERAND_OUTPUT}
            ins=replace_instruction_operands(ins,replacements)
            if code(i)==OPCODE_RET:
                prefix+=instruction(MOV,[0x001020f2,0],temp(outD))+instruction(MOV,[0x001020f2,1],temp(outS))
        prefix+=ins
    prefix[1]=len(prefix);chunks[chunkIndex]=DxbcChunk(chunks[chunkIndex].tag,pack_words(prefix));return build_dxbc(version,chunks)
