// Regression for the native DFComposite environment * diffuse-light product.
// Use real generated shaders, with zero diffuse illumination and a nonzero cube.
void metalReflectionTest(GPU& gpu,const std::filesystem::path& root)
{
 using F4=std::array<float,4>;
 const auto compilePixel=[&](const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);require(bool(f),"metal shader file");std::vector<char> data((std::istreambuf_iterator<char>(f)),{});ComPtr<ID3D11PixelShader> shader;check(gpu.device->CreatePixelShader(data.data(),data.size(),nullptr,&shader),"metal pixel shader");return shader;};
 const std::string vs="struct O{float4 p:SV_Position;nointerpolation uint eye:EYEINDEX;float cull:SV_CullDistance;float clip:SV_ClipDistance;};O main(uint id:SV_VertexID){O o;float2 uv=float2((id<<1)&2,id&2);o.p=float4(uv*float2(2,-2)+float2(-1,1),0,1);o.eye=0;o.cull=1;o.clip=1;return o;}";
 ComPtr<ID3DBlob> code,error;check(D3DCompile(vs.data(),vs.size(),nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&code,&error),"metal VS compile");ComPtr<ID3D11VertexShader> vertex;check(gpu.device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vertex),"metal VS");
 const auto texture=[&](F4 value,bool cube=false,bool array=false){Texture t;D3D11_TEXTURE2D_DESC d{};d.Width=cube?1:2;d.Height=d.MipLevels=d.SampleDesc.Count=1;d.ArraySize=cube?6:1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.MiscFlags=cube?D3D11_RESOURCE_MISC_TEXTURECUBE:0;check(gpu.device->CreateTexture2D(&d,nullptr,&t.texture),"metal texture");std::array<F4,2> pixels{value,value};for(UINT i=0;i<d.ArraySize;i++)gpu.context->UpdateSubresource(t.texture.Get(),i,nullptr,pixels.data(),d.Width*16,0);D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=d.Format;sd.ViewDimension=array?D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:cube?D3D11_SRV_DIMENSION_TEXTURECUBE:D3D11_SRV_DIMENSION_TEXTURE2D;if(array){sd.TextureCubeArray.MipLevels=1;sd.TextureCubeArray.NumCubes=1;}else if(cube)sd.TextureCube.MipLevels=1;else sd.Texture2D.MipLevels=1;check(gpu.device->CreateShaderResourceView(t.texture.Get(),&sd,&t.view),"metal texture view");return t;};
 auto black=texture({0,0,0,0}),white=texture({1,1,1,1}),base=texture({.8f,.6f,.3f,1}),material=texture({.5f,1.f/255,.2f,1}),shading=texture({.9f,.5f,0,1}),normal=texture({.5f,.5f,.5f,.5f}),depth=texture({.5f,.5f,.5f,.5f});
 auto authored=texture({.625f,1,1,.5f}),style=texture({1,1,1,-1});auto native=texture({.4f,.6f,.8f,1},true,true),dynamic=texture({.8f,.3f,.2f,1},true),validity=texture({1,1,1,1},true);
 // At the tested pixel, the native incident direction is +Z. Our outgoing
 // dynamic environment must sample -Z, whose red differs from every other face.
 const F4 oppositeFace{.01f,.3f,.8f,1};
 for(UINT face=0;face<5;face++)gpu.context->UpdateSubresource(dynamic.texture.Get(),face,nullptr,oppositeFace.data(),16,0);
 std::array<ID3D11ShaderResourceView*,48> views{};for(auto& view:views)view=black.view.Get();views[0]=base.view.Get();views[1]=normal.view.Get();views[2]=material.view.Get();views[3]=shading.view.Get();views[7]=depth.view.Get();views[8]=native.view.Get();views[9]=white.view.Get();views[29]=base.view.Get();views[30]=dynamic.view.Get();views[31]=validity.view.Get();views[32]=dynamic.view.Get();views[33]=validity.view.Get();views[34]=dynamic.view.Get();views[35]=dynamic.view.Get();views[36]=shading.view.Get();views[37]=style.view.Get();views[45]=authored.view.Get();
 gpu.context->PSSetShaderResources(0,UINT(views.size()),views.data());
 D3D11_SAMPLER_DESC samplerDesc{};samplerDesc.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;samplerDesc.AddressU=samplerDesc.AddressV=samplerDesc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;samplerDesc.MaxLOD=D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState> sampler;check(gpu.device->CreateSamplerState(&samplerDesc,&sampler),"metal sampler");std::array<ID3D11SamplerState*,16> samplers{};samplers.fill(sampler.Get());gpu.context->PSSetSamplers(0,16,samplers.data());
 const auto buffer=[&](const void* data,UINT bytes){ComPtr<ID3D11Buffer> b;D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA initial{data,0,0};check(gpu.device->CreateBuffer(&d,&initial,&b),"metal constants");return b;};
 std::array<F4,82> scene{};for(UINT r:{0,20,32,36,40,44}){scene[r][0]=scene[r+1][1]=scene[r+2][2]=scene[r+3][3]=1;}scene[71]={0,0,1,0};scene[72][3]=scene[73][3]=1;
 std::array<F4,7> parameters{};parameters[0]={0,1,1,0};parameters[3]={1,1,1,1};parameters[4]={1,1,1,0};parameters[5]={1,1,.32f,.02f};parameters[6]={1,1,1,0};
 std::array<F4,3> nativeParams{};nativeParams[0]={.5f,1,0,0};nativeParams[1]={1,0,0,0};nativeParams[2][3]=1;F4 stereo{1,0,0,0};
 auto frame=buffer(scene.data(),sizeof(scene)),params=buffer(parameters.data(),sizeof(parameters)),pass=buffer(nativeParams.data(),sizeof(nativeParams)),eyes=buffer(&stereo,sizeof(stereo));
 for(auto pair:std::array<std::pair<UINT,ID3D11Buffer*>,5>{{{0,pass.Get()},{2,pass.Get()},{5,params.Get()},{8,eyes.Get()},{12,frame.Get()}}})gpu.context->PSSetConstantBuffers(pair.first,1,&pair.second);
 D3D11_TEXTURE2D_DESC td{};td.Width=2;td.Height=td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> target,staging;ComPtr<ID3D11RenderTargetView> rtv;check(gpu.device->CreateTexture2D(&td,nullptr,&target),"metal target");check(gpu.device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"metal RTV");td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(gpu.device->CreateTexture2D(&td,nullptr,&staging),"metal readback");
 auto* out=rtv.Get();gpu.context->OMSetRenderTargets(1,&out,nullptr);D3D11_VIEWPORT vp{0,0,2,1,0,1};gpu.context->RSSetViewports(1,&vp);gpu.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);gpu.context->VSSetShader(vertex.Get(),nullptr,0);
 for(const char* name:{"DFComposite_00_05f5cabf.dxbc","DFComposite_01_05fa2982.dxbc","DFComposite_09_3c3cc20b.dxbc","DFComposite_15_618ab40a.dxbc"}) {
  auto shader=compilePixel(root/"package/Shaders/Community/IBLMaterial"/name);gpu.context->PSSetShader(shader.Get(),nullptr,0);
  F4 precedingNative{};
  for(bool occlusion:{false,true})for(bool authoredMode:{false,true})for(bool enabled:{false,true})for(bool metal:{false,true})for(bool dynamicMode:{false,true}) {
   parameters[4][3]=occlusion?1.f:0.f;parameters[0][0]=dynamicMode?1.f:0.f;parameters[3][0]=enabled?1.f:0.f;gpu.context->UpdateSubresource(params.Get(),0,nullptr,parameters.data(),0,0);
   const F4 colour{.8f,.6f,.3f,1};std::array<F4,2> pixels{colour,colour};
   if(!authoredMode&&metal)for(auto& c:pixels)for(UINT i=0;i<3;i++)c[i]/=255;
   gpu.context->UpdateSubresource(base.texture.Get(),0,nullptr,pixels.data(),32,0);
   pixels.fill({.9f,.5f,0,!authoredMode&&metal?.5f:1.f});gpu.context->UpdateSubresource(shading.texture.Get(),0,nullptr,pixels.data(),32,0);
   pixels.fill(authoredMode?F4{.625f,metal?1.f:0.f,1,.5f}:F4{});gpu.context->UpdateSubresource(authored.texture.Get(),0,nullptr,pixels.data(),32,0);
   const float clear[4]{};gpu.context->ClearRenderTargetView(rtv.Get(),clear);gpu.context->Draw(3,0);gpu.context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};check(gpu.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"metal map");F4 value{};std::memcpy(value.data(),mapped.pData,16);gpu.context->Unmap(staging.Get(),0);
   if(!dynamicMode) precedingNative=value;
   if(dynamicMode&&enabled&&metal) require(value[0]>precedingNative[0]*1.5f,"metal lost dynamic cube colour in favour of vanilla fallback");
   require(std::isfinite(value[0])&&std::isfinite(value[1])&&std::isfinite(value[2]),"metal reflection nonfinite");
   if(enabled&&metal) {if(!(value[0]+value[1]+value[2]>.001f))std::cerr<<name<<" authored="<<authoredMode<<" dynamic="<<dynamicMode<<" metal dark\n";require(value[0]+value[1]+value[2]>.001f,"metal reflections disappeared with zero diffuse lighting");}
   else require(value[0]==0&&value[1]==0&&value[2]==0,"nonmetal or PBR-off native response changed");
  }
 }
 gpu.context->ClearState();gpu.validateMessages();std::cout<<"Authored/legacy metals retain native and correctly directed dynamic cubemaps with zero diffuse, occlusion on/off; dielectrics and PBR-off stay unchanged\n";
}
