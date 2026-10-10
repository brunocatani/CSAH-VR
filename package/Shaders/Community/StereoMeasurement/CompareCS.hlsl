// Read-only comparison of native right-eye buffers with nearest left-eye samples.
// Counter order matches StereoMeasurement.h. This shader never writes scene data.
Texture2D<float4> Albedo : register(t0);
Texture2D<float2> Normal : register(t1);
Texture2D<float4> Material : register(t2);
Texture2D<float> Depth : register(t3);
Texture2D<float2> Motion : register(t4);
Texture2D<float> SurfaceClass : register(t5);
RWBuffer<uint> Counters : register(u0);
cbuffer Parameters : register(b0) {
    uint2 Origin;uint2 Extent;
    uint Stride;uint HasMotion;float NearDistance;float DepthTolerance;
    uint2 Phase;uint HasClasses;uint Reserved;
};
cbuffer NativeStereo : register(b8) {float4 Stereo;};
cbuffer NativeCamera : register(b12) {float4 Camera[61];};
groupshared uint Counts[78];
uint ClassAt(int2 p) {
    if(HasClasses==0)return 5;
    uint code=(uint)round(saturate(SurfaceClass.Load(int3(p,0)))*255);
    return code<5?code:5;
}
void Count(uint index) {InterlockedAdd(Counts[index],1);}
void Maximum(uint index,float value,float scale) {
    InterlockedMax(Counts[index],(uint)round(clamp(value*scale,0,4294967040.0)));
}
bool Finite3(float3 v){return all(isfinite(v));}
float3 ViewPosition(int2 pixel,float depth,uint eye) {
    float2 uv=(float2(pixel)+.5-Origin)/float2(Extent);
    float4 clip=float4((uv.x*2-1+(eye==0?.5:-.5))*2,1-uv.y*2,depth*1.01-.01,1);
    uint r=32+eye*4;float4 p=float4(dot(Camera[r],clip),dot(Camera[r+1],clip),dot(Camera[r+2],clip),dot(Camera[r+3],clip));
    return p.xyz/p.w;
}
float3 OtherView(float3 p,uint fromEye,uint toEye) {
    float3 world=float3(dot(Camera[20].xyz,p),dot(Camera[21].xyz,p),dot(Camera[22].xyz,p));
    world+=Camera[59+fromEye].xyz-Camera[59+toEye].xyz;
    return float3(dot(Camera[0].xyz,world),dot(Camera[1].xyz,world),dot(Camera[2].xyz,world));
}
float2 ProjectPixel(float3 p,uint eye) {
    uint r=4+eye*4;float4 v=float4(p,1);
    float w=dot(Camera[r+3],v);
    if(w<=0 || !isfinite(w)) return float2(-1e10,-1e10);
    float2 uv=float2(dot(Camera[r],v),dot(Camera[r+1],v))/w*float2(.5,-.5)+.5;
    return Origin+float2((uv.x+eye)*.5,uv.y)*Extent-.5;
}
bool Inside(int2 p,uint eye,int margin) {
    int2 lo=int2(Origin)+int2(eye*(Extent.x/2),0)+margin;
    int2 hi=int2(Origin)+int2((eye+1)*(Extent.x/2),Extent.y)-margin;
    return all(p>=lo)&&all(p<hi);
}
bool DepthEdge(int2 p,uint eye,float z) {
    const int2 offsets[4]={int2(-1,0),int2(1,0),int2(0,-1),int2(0,1)};
    [unroll] for(uint i=0;i<4;i++) {
        int2 q=p+offsets[i];float d=Depth.Load(int3(q,0));
        if(!isfinite(d)||d<=.01||d>=1) return true;
        float neighbor=ViewPosition(q,d,eye).z;
        if(!isfinite(neighbor)||abs(neighbor-z)>max(1,abs(z)*.005)) return true;
    }
    return false;
}
float3 DecodeNormal(float2 encoded) {
    float2 e=encoded*4-2;float l=dot(e,e);
    return normalize(float3(e*sqrt(max(1-l*.25,0)),l*.5-1));
}
float Max4(float4 v) {return max(max(v.x,v.y),max(v.z,v.w));}
void Evaluate(uint2 id) {
    int2 p=int2(Origin)+int2(Extent.x/2,0)+int2(id*Stride+Phase);
    if(!Inside(p,1,0)) return;
    Count(0);
    uint category=ClassAt(p),b=24+category*9;Count(b);
    if(!isfinite(Stereo.x)||Stereo.x<.5){Count(22);return;}
    float depth=Depth.Load(int3(p,0));
    if(!isfinite(depth)){Count(5);return;}
    if(depth<=0||depth>=1){Count(1);return;}
    if(depth<=.01){Count(2);return;}
    float3 right=ViewPosition(p,depth,1);
    if(!Finite3(right)){Count(5);return;}
    if(abs(right.z)<NearDistance){Count(3);return;}
    float2 projected=ProjectPixel(OtherView(right,1,0),0);
    if(!all(isfinite(projected))){Count(5);return;}
    if(any(projected<float2(Origin))||any(projected>=float2(Origin)+float2(Extent.x/2,Extent.y))){Count(4);return;}
    int2 q=int2(floor(projected+.5));
    if(!Inside(q,0,1)||!Inside(p,1,1)){Count(6);return;}
    float leftDepth=Depth.Load(int3(q,0));
    if(!isfinite(leftDepth)||leftDepth<=.01||leftDepth>=1){Count(7);return;}
    float3 left=ViewPosition(q,leftDepth,0),leftInRight=OtherView(left,0,1);
    if(!Finite3(left)||!Finite3(leftInRight)){Count(5);return;}
    if(DepthEdge(p,1,right.z)||DepthEdge(q,0,left.z)){Count(6);return;}
    if(abs(leftInRight.z-right.z)>max(.5,abs(right.z)*DepthTolerance)){Count(7);return;}
    float2 back=ProjectPixel(leftInRight,1);
    if(!all(isfinite(back))||length(back-float2(p))>1.5){Count(8);return;}
    Count(9);Count(b+1);
    bool sameClass=category<5&&ClassAt(q)==category;
    if(HasClasses!=0&&!sameClass)Count(b+8);
    float3 nr=DecodeNormal(Normal.Load(int3(p,0))),nl=DecodeNormal(Normal.Load(int3(q,0)));
    float4 ar=Albedo.Load(int3(p,0)),al=Albedo.Load(int3(q,0));
    float4 mr=Material.Load(int3(p,0)),ml=Material.Load(int3(q,0));
    if(!Finite3(nr)||!Finite3(nl)||!all(isfinite(ar))||!all(isfinite(al))||!all(isfinite(mr))||!all(isfinite(ml)))return;
    float ne=1-saturate(dot(nr,nl)),ae=Max4(abs(ar-al)),me=Max4(abs(mr-ml));
    bool normalMatch=ne<=.005,albedoMatch=ae<=2.0/255,materialMatch=me<=1.0/255;
    if(normalMatch){Count(10);Count(b+2);}if(albedoMatch){Count(11);Count(b+3);}if(materialMatch){Count(12);Count(b+4);}
    bool matches=normalMatch&&albedoMatch&&materialMatch;
    if(matches)Count(13);
    Maximum(18,ne,1000000);Maximum(19,ae,1000000);Maximum(20,me,1000000);
    if(HasMotion!=0){
        float2 r=Motion.Load(int3(p,0)),l=Motion.Load(int3(q,0));
        if(!all(isfinite(r))||!all(isfinite(l))){Count(17);return;}
        Count(14);Count(b+5);float error=max(abs(r.x-l.x),abs(r.y-l.y));Maximum(21,error,1000000);
        if(error<=.0001){Count(15);Count(b+6);if(matches){Count(16);if(sameClass)Count(b+7);}}
    }
}
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID,uint index:SV_GroupIndex) {
    for(uint clearIndex=index;clearIndex<78;clearIndex+=64)Counts[clearIndex]=0;
    GroupMemoryBarrierWithGroupSync();
    Evaluate(id.xy);
    GroupMemoryBarrierWithGroupSync();
    for(uint i=index;i<78;i+=64){if(i>=18&&i<=21)InterlockedMax(Counters[i],Counts[i]);else InterlockedAdd(Counters[i],Counts[i]);}
}
