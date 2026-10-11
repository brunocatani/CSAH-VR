#include "../../../package/Shaders/Community/PBR/PbrCommon.hlsli"
#include "../../../package/Shaders/Community/PBR/PbrIndirectMaterial.hlsli"
RWTexture2D<float2> Output : register(u0);
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID) {
 float4 feature0=float4(1,1,1,0),feature1=1,params0=float4(1,1,.4,.03),params1=float4(1,1,1,0);
 float4 material=float4(.5,.5,0,.5),authored=0;
 float3 base=float3(.8,.6,.3)/255;
 if(id.y==1 || id.y==5) {authored=float4(.65,1,1,.5);base=float3(.8,.6,.3);}
 if(id.y==2 || id.y==6) material.w=1;
 if(id.y==3) {feature0.y=0;material.w=1;}
 if(id.y==4) feature0.x=0;
 if(id.y==5) params0.x=2;
 if(id.y==6) {material.y=0;params0.w=.1;}
 if(id.y==7) params1.y=.5;
 PbrIndirectMaterial m=DecodePbrIndirectMaterial(material,authored,base,0,feature0,feature1,params0,params1);
 Output[id.xy]=float2(PbrVisibilitySmithJointApprox((id.x+1)/8.0,1,1),id.y==5?m.roughness:m.f0.x);
}
