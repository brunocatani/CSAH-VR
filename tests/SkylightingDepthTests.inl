// Real depth texture + real probe-update shader, including the native DSV/SRV hazard.
void skylightingDepthTest(GPU& gpu,const std::filesystem::path& root)
{
 using F4=std::array<float,4>;
 auto dir=root/"package/Shaders/Community/Skylighting";
 auto shader=gpu.shader(dir/"UpdateProbesCS.hlsl",dir,false,false,false,false,"CSMain");
 D3D11_TEXTURE2D_DESC dd{};dd.Width=dd.Height=4;dd.MipLevels=dd.ArraySize=dd.SampleDesc.Count=1;
 dd.Format=DXGI_FORMAT_R16_TYPELESS;dd.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
 ComPtr<ID3D11Texture2D> depth;check(gpu.device->CreateTexture2D(&dd,nullptr,&depth),"probe capture depth");
 D3D11_DEPTH_STENCIL_VIEW_DESC vd{};vd.Format=DXGI_FORMAT_D16_UNORM;vd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
 ComPtr<ID3D11DepthStencilView> dsv;check(gpu.device->CreateDepthStencilView(depth.Get(),&vd,&dsv),"probe capture DSV");
 D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R16_UNORM;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
 ComPtr<ID3D11ShaderResourceView> srv;check(gpu.device->CreateShaderResourceView(depth.Get(),&sd,&srv),"probe depth SRV");
 D3D11_TEXTURE3D_DESC td{};td.Width=td.Height=td.Depth=td.MipLevels=1;td.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
 td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
 ComPtr<ID3D11Texture3D> probes,counts,staging,stagingCount;ComPtr<ID3D11UnorderedAccessView> probeUav,countUav;
 check(gpu.device->CreateTexture3D(&td,nullptr,&probes),"probe output");check(gpu.device->CreateUnorderedAccessView(probes.Get(),nullptr,&probeUav),"probe UAV");
 td.Format=DXGI_FORMAT_R32_UINT;check(gpu.device->CreateTexture3D(&td,nullptr,&counts),"probe counts");check(gpu.device->CreateUnorderedAccessView(counts.Get(),nullptr,&countUav),"count UAV");
 td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 check(gpu.device->CreateTexture3D(&td,nullptr,&stagingCount),"count readback");td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
 check(gpu.device->CreateTexture3D(&td,nullptr,&staging),"probe readback");
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=32*16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> constants;
 check(gpu.device->CreateBuffer(&bd,nullptr,&constants),"probe constants");
 bd.ByteWidth=128;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
 ComPtr<ID3D11Buffer> diagnostics;check(gpu.device->CreateBuffer(&bd,nullptr,&diagnostics),"probe diagnostic buffer");
 D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R32_TYPELESS;ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=32;ud.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
 ComPtr<ID3D11UnorderedAccessView> diagnosticUav;check(gpu.device->CreateUnorderedAccessView(diagnostics.Get(),&ud,&diagnosticUav),"probe diagnostic UAV");
 D3D11_SAMPLER_DESC samplerDesc{};samplerDesc.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
 samplerDesc.AddressU=samplerDesc.AddressV=samplerDesc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;samplerDesc.ComparisonFunc=csah::skylighting::kOcclusionComparison;samplerDesc.MaxLOD=D3D11_FLOAT32_MAX;
 ComPtr<ID3D11SamplerState> sampler;check(gpu.device->CreateSamplerState(&samplerDesc,&sampler),"probe comparison sampler");
 auto psOutput=gpu.texture(4);auto* psUav=psOutput.output.Get();
 D3D11_TEXTURE2D_DESC colourDesc=dd;colourDesc.Format=DXGI_FORMAT_R32G32_FLOAT;colourDesc.BindFlags=D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D> colour;ComPtr<ID3D11RenderTargetView> rtv;
 check(gpu.device->CreateTexture2D(&colourDesc,nullptr,&colour),"retained colour output");check(gpu.device->CreateRenderTargetView(colour.Get(),nullptr,&rtv),"retained colour RTV");auto* colourOutput=rtv.Get();
 // columns: captured depth, receiver depth, expected visible, expected sampled
 for(auto scenario:std::array<F4,5>{{{1,.5f,1,1},{.75f,.5f,1,1},{.25f,.5f,0,1},{1,-.1f,1,0},{1,1.1f,1,0}}}) {
  gpu.context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,scenario[0],0);
  gpu.context->OMSetRenderTargetsAndUnorderedAccessViews(1,&colourOutput,dsv.Get(),3,1,&psUav,nullptr);
  std::array<F4,32> data{};data[3]={0,0,scenario[1],1};data[4]={0,0,1,0};data[5]=data[6]={1,1,1,0};
  UINT one=1;float packedOne;std::memcpy(&packedOne,&one,4);data[8]={packedOne,packedOne,packedOne,0};data[17]={0,0,packedOne,0};data[18]={.1f,.1f,1,0};
  gpu.context->UpdateSubresource(constants.Get(),0,nullptr,data.data(),0,0);
  const F4 neutral{3.5449077018f,0,0,0};const UINT zero=0;
  gpu.context->UpdateSubresource(probes.Get(),0,nullptr,neutral.data(),16,16);gpu.context->UpdateSubresource(counts.Get(),0,nullptr,&zero,4,4);
  {
   csah::skylighting::ScopedDepthRead read(gpu.context.Get(),depth.Get());
   gpu.context->CSSetShaderResources(0,1,srv.GetAddressOf());ComPtr<ID3D11ShaderResourceView> actual;gpu.context->CSGetShaderResources(0,1,&actual);
   require(actual.Get()==srv.Get(),"writable capture DSV nullified Skylighting depth SRV");
   std::array<ID3D11UnorderedAccessView*,3> outputs{probeUav.Get(),countUav.Get(),diagnosticUav.Get()};gpu.context->CSSetUnorderedAccessViews(0,3,outputs.data(),nullptr);
   auto* cb=constants.Get();gpu.context->CSSetConstantBuffers(13,1,&cb);gpu.context->CSSetSamplers(0,1,sampler.GetAddressOf());gpu.context->CSSetShader(shader.Get(),nullptr,0);gpu.context->Dispatch(1,1,1);
   outputs.fill(nullptr);gpu.context->CSSetUnorderedAccessViews(0,3,outputs.data(),nullptr);ID3D11ShaderResourceView* empty=nullptr;gpu.context->CSSetShaderResources(0,1,&empty);
  }
  ComPtr<ID3D11DepthStencilView> restored;ComPtr<ID3D11UnorderedAccessView> restoredUav;ComPtr<ID3D11RenderTargetView> restoredColour;
  gpu.context->OMGetRenderTargetsAndUnorderedAccessViews(1,restoredColour.GetAddressOf(),&restored,3,1,&restoredUav);
  require(restored.Get()==dsv.Get() && restoredUav.Get()==psUav && restoredColour.Get()==rtv.Get(),"probe update did not restore OM depth/UAV state");
  gpu.context->CopyResource(staging.Get(),probes.Get());gpu.context->CopyResource(stagingCount.Get(),counts.Get());
  D3D11_MAPPED_SUBRESOURCE map{};check(gpu.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"probe map");float dc;std::memcpy(&dc,map.pData,4);gpu.context->Unmap(staging.Get(),0);
  check(gpu.context->Map(stagingCount.Get(),0,D3D11_MAP_READ,0,&map),"count map");UINT count;std::memcpy(&count,map.pData,4);gpu.context->Unmap(stagingCount.Get(),0);
  require(count==UINT(scenario[3]),"out-of-frustum probe accumulated a false occlusion");
  require(scenario[2]>0?std::abs(dc-neutral[0])<1e-5f:dc<neutral[0]*.95f,"probe clear/front/behind depth visibility is inverted");
 }
 gpu.context->ClearState();gpu.validateMessages();
 std::cout<<"Skylighting depth producer: clear sky, front/behind occluder, frustum rejection and DSV/UAV restoration passed\n";
}
