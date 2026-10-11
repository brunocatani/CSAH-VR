#include "Features/bloom_glare/BloomGlareRuntime.h"
#include "Features/skylighting/SkylightingDepthRead.h"
#include "Features/filmic_tonemapping/FilmicTonemappingRuntime.h"
#include <DirectXPackedVector.h>
#include <Windows.h>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
// The test exercises Bloom/Glare directly; the output compositor gate has no draw here.
namespace csah::filmic_tonemapping {
Runtime& Runtime::get() noexcept { static Runtime runtime; return runtime; }
void Runtime::setOutputFeatureRequested(bool requested) noexcept { outputFeatureRequested_.store(requested); }
}
using Microsoft::WRL::ComPtr;
struct V {float x{},y{};};
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void check(HRESULT hr,const char* text){require(SUCCEEDED(hr),text);}
struct Texture {ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;ComPtr<ID3D11UnorderedAccessView> output;};
struct GPU {
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11InfoQueue> messages;
 explicit GPU(bool hardware) {
  const auto driver=hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP;
  auto hr=D3D11CreateDevice(nullptr,driver,nullptr,D3D11_CREATE_DEVICE_DEBUG,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
  if(hr==DXGI_ERROR_SDK_COMPONENT_MISSING)hr=D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
  check(hr,"device");(void)device.As(&messages);
 }
 Texture texture(unsigned n) {
  Texture t;D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=n;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32G32_FLOAT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
  check(device->CreateTexture2D(&d,nullptr,&t.texture),"texture");check(device->CreateShaderResourceView(t.texture.Get(),nullptr,&t.view),"view");check(device->CreateUnorderedAccessView(t.texture.Get(),nullptr,&t.output),"output");return t;
 }
 ComPtr<ID3D11ComputeShader> shader(const std::filesystem::path& file,const std::filesystem::path& includeRoot,bool row,bool inverse,bool fused=false,bool multiply=false,const char* entry=nullptr) {
  std::ifstream stream(file);require(bool(stream),"shader source");std::string source((std::istreambuf_iterator<char>(stream)),{});
  const D3D_SHADER_MACRO macros[]{{row?"ROW_PASS":"COL_PASS","1"},{inverse?"INVERSE":"FORWARD","1"},{fused?"MULTIPLY_PSF":"UNUSED","1"},{}};
  ComPtr<ID3DBlob> code,errors;auto hr=D3DCompile(source.data(),source.size(),(includeRoot/"Test.hlsl").string().c_str(),macros,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry?entry:(multiply?"CS_Multiply":"CS_Fft"),"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&errors);
  if(FAILED(hr)&&errors)std::cerr<<static_cast<char*>(errors->GetBufferPointer());check(hr,"compile");
  ComPtr<ID3D11ComputeShader> result;check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&result),"compute shader");return result;
 }
 void dispatch(ID3D11ComputeShader* shader,const Texture& input,const Texture& output,unsigned n,const Texture* psf=nullptr,bool multiply=false) {
  ID3D11ShaderResourceView* views[]{input.view.Get(),psf?psf->view.Get():nullptr};auto* target=output.output.Get();context->CSSetShaderResources(0,2,views);context->CSSetUnorderedAccessViews(0,1,&target,nullptr);context->CSSetShader(shader,nullptr,0);context->Dispatch(multiply?(n+7)/8:n,multiply?(n+7)/8:1,1);
  views[0]=views[1]=nullptr;target=nullptr;context->CSSetUnorderedAccessViews(0,1,&target,nullptr);context->CSSetShaderResources(0,2,views);
 }
 std::vector<V> read(const Texture& t,unsigned n) {
  D3D11_TEXTURE2D_DESC d{};t.texture->GetDesc(&d);d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&d,nullptr,&staging),"staging");context->CopyResource(staging.Get(),t.texture.Get());D3D11_MAPPED_SUBRESOURCE map{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"readback");std::vector<V> data(n*n);
  for(unsigned y=0;y<n;y++)std::memcpy(data.data()+y*n,static_cast<char*>(map.pData)+y*map.RowPitch,n*sizeof(V));context->Unmap(staging.Get(),0);return data;
 }
 template<class F> double time(F&& f) {
  ComPtr<ID3D11Query> disjoint,start,end;D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT,0};check(device->CreateQuery(&d,&disjoint),"disjoint");d.Query=D3D11_QUERY_TIMESTAMP;check(device->CreateQuery(&d,&start),"timestamp");check(device->CreateQuery(&d,&end),"timestamp");
  context->Begin(disjoint.Get());context->End(start.Get());for(unsigned i=0;i<20;i++)f();context->End(end.Get());context->End(disjoint.Get());context->Flush();
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  const auto wait=[&](ID3D11Query* q,void* data,UINT size){HRESULT hr;while((hr=context->GetData(q,data,size,D3D11_ASYNC_GETDATA_DONOTFLUSH))==S_FALSE){require(std::chrono::steady_clock::now()<deadline,"query timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(hr,"query read");};
  D3D11_QUERY_DATA_TIMESTAMP_DISJOINT range{};UINT64 a{},b{};wait(disjoint.Get(),&range,sizeof(range));wait(start.Get(),&a,sizeof(a));wait(end.Get(),&b,sizeof(b));require(!range.Disjoint&&range.Frequency,"stable timing");return double(b-a)*1000/double(range.Frequency)/20;
 }
 void validateMessages(){if(!messages)return;for(UINT64 i=0;i<messages->GetNumStoredMessages();i++){SIZE_T size{};check(messages->GetMessage(i,nullptr,&size),"message size");std::vector<char> data(size);auto* m=reinterpret_cast<D3D11_MESSAGE*>(data.data());check(messages->GetMessage(i,m,&size),"message");if(m->Severity<=D3D11_MESSAGE_SEVERITY_WARNING){std::cerr<<m->pDescription<<'\n';throw std::runtime_error("D3D validation warning");}}messages->ClearStoredMessages();}
};

#include "PbrMetalReflectionTests.inl"
#include "SkylightingDepthTests.inl"

void giFilterTest(GPU& gpu,const std::filesystem::path& root,bool hardware) {
 auto dir=root/"package/Shaders/Community/ScreenSpaceGI";
 auto before=gpu.shader(root/"tests/fixtures/gi/ReferenceFilter.hlsl",dir,false,false,false,false,"main");
 auto after=gpu.shader(dir/"FilterCS.hlsl",dir,false,false,false,false,"main");
 using Float4=std::array<float,4>;constexpr unsigned n=128;
 const auto texture=[&] {Texture t;D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=n;td.ArraySize=2;td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
  check(gpu.device->CreateTexture2D(&td,nullptr,&t.texture),"GI texture");check(gpu.device->CreateShaderResourceView(t.texture.Get(),nullptr,&t.view),"GI view");check(gpu.device->CreateUnorderedAccessView(t.texture.Get(),nullptr,&t.output),"GI output");return t;};
 std::array<Texture,8> inputs;for(auto& t:inputs)t=texture();auto oldD=texture(),oldS=texture(),newD=texture(),newS=texture();
 std::vector<Float4> values(n*n);
 const auto fill=[&](unsigned slot,auto fn){for(unsigned eye=0;eye<2;eye++){for(unsigned y=0;y<n;y++)for(unsigned x=0;x<n;x++)values[y*n+x]=fn(x,y,eye);gpu.context->UpdateSubresource(inputs[slot].texture.Get(),eye,nullptr,values.data(),n*16,0);}};
 for(unsigned slot:{0,2})fill(slot,[](unsigned x,unsigned y,unsigned){return Float4{((x+.5f)/n*2-1)*100,(1-(y+.5f)/n*2)*100,100,1};});
 for(unsigned slot:{1,3})fill(slot,[](unsigned,unsigned,unsigned){return Float4{0,0,-1,1};});
 for(unsigned slot:{4,6,7})fill(slot,[](unsigned x,unsigned y,unsigned eye){return Float4{float(x%7)/7,float(y%9)/9,float(eye+1)/3,5};});
 std::array<Float4,77> camera{};camera[0][0]=camera[1][1]=camera[2][2]=camera[20][0]=camera[21][1]=camera[22][2]=1;
 for(unsigned row:{4,8}){camera[row][0]=1;camera[row+1][1]=1;camera[row+2][2]=1;camera[row+3][2]=1;}
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(camera);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA initial{camera.data(),0,0};ComPtr<ID3D11Buffer> cameraBuffer;
 check(gpu.device->CreateBuffer(&bd,&initial,&cameraBuffer),"GI camera");auto* cb=cameraBuffer.Get();gpu.context->CSSetConstantBuffers(10,1,&cb);gpu.context->CSSetConstantBuffers(12,1,&cb);
 struct Settings {unsigned width{n},height{n},frame{},history{1};float x{},y{},vw{n*2},vh{n};float radius{256},thickness{32},power{2},strength{1};unsigned gi{},spec{},temporal{},tiled{};float pbr[16]{};} settings;
 bd.ByteWidth=sizeof(settings);ComPtr<ID3D11Buffer> constants;check(gpu.device->CreateBuffer(&bd,nullptr,&constants),"GI settings");cb=constants.Get();gpu.context->CSSetConstantBuffers(13,1,&cb);
 const auto dispatch=[&](ID3D11ComputeShader* shader,Texture& d,Texture& s){std::array<ID3D11ShaderResourceView*,8> views{};for(unsigned i=0;i<8;i++)views[i]=inputs[i].view.Get();ID3D11UnorderedAccessView* outputs[]{d.output.Get(),s.output.Get()};gpu.context->CSSetShaderResources(0,8,views.data());gpu.context->CSSetUnorderedAccessViews(0,2,outputs,nullptr);gpu.context->CSSetShader(shader,nullptr,0);gpu.context->Dispatch(n/8,n/8,2);outputs[0]=outputs[1]=nullptr;views={};gpu.context->CSSetUnorderedAccessViews(0,2,outputs,nullptr);gpu.context->CSSetShaderResources(0,8,views.data());};
 const auto read=[&](Texture& t){D3D11_TEXTURE2D_DESC td{};t.texture->GetDesc(&td);td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;check(gpu.device->CreateTexture2D(&td,nullptr,&staging),"GI readback texture");gpu.context->CopyResource(staging.Get(),t.texture.Get());std::vector<Float4> pixels(n*n*2);for(unsigned eye=0;eye<2;eye++){D3D11_MAPPED_SUBRESOURCE m{};check(gpu.context->Map(staging.Get(),eye,D3D11_MAP_READ,0,&m),"GI readback");for(unsigned y=0;y<n;y++)std::memcpy(pixels.data()+(eye*n+y)*n,static_cast<char*>(m.pData)+y*m.RowPitch,n*16);gpu.context->Unmap(staging.Get(),eye);}return pixels;};
 for(unsigned gi=0;gi<2;gi++)for(unsigned spec=0;spec<2;spec++)for(unsigned temporal=0;temporal<2;temporal++){
  settings.gi=gi;settings.spec=spec;settings.temporal=temporal;gpu.context->UpdateSubresource(constants.Get(),0,nullptr,&settings,0,0);
  fill(5,[&](unsigned x,unsigned y,unsigned eye){return gi&&spec?Float4{float(x%5)/5,float(y%3)/3,float(eye)/2,1}:Float4{0,0,0,1};});
  dispatch(before.Get(),oldD,oldS);dispatch(after.Get(),newD,newS);
  const auto od=read(oldD),nd=read(newD),os=read(oldS),ns=read(newS);
  require(std::memcmp(od.data(),nd.data(),od.size()*sizeof(Float4))==0 &&
      std::memcmp(os.data(),ns.data(),os.size()*sizeof(Float4))==0,"GI filter changes radiance, AO or temporal counts");
  if(hardware && gi && temporal){std::array<double,5> a{},b{};for(unsigned round=0;round<5;round++){a[round]=gpu.time([&]{dispatch(before.Get(),oldD,oldS);});b[round]=gpu.time([&]{dispatch(after.Get(),newD,newS);});}std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());std::cout<<"GI filter specular="<<spec<<" GPU ms before="<<a[2]<<", after="<<b[2]<<'\n';}
 }
 gpu.context->ClearState();gpu.validateMessages();std::cout<<"GI filter all feature combinations: bit-identical AO, diffuse, specular and temporal counts\n";
}
void runtimeTest(GPU& gpu) {
 auto& runtime=csah::bloom_glare::Runtime::get();runtime.onDeviceCreated(gpu.device.Get(),gpu.context.Get());
 csah::bloom_glare::Settings settings;settings.bloom.enabled=false;settings.glare.enabled=true;settings.glare.fftResolution=128;
 runtime.applySettings(settings);const auto initialGenerations=runtime.snapshot().psfGenerations;
 constexpr unsigned width=128,height=64;
 std::array<std::array<float,4>,width*height> pixels{};
 for(unsigned y=20;y<40;y++)for(unsigned x=20;x<40;x++)pixels[y*width+x]={64,0,0,1};
 D3D11_TEXTURE2D_DESC td{};td.Width=width;td.Height=height;td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 D3D11_SUBRESOURCE_DATA data{pixels.data(),width*16,0};ComPtr<ID3D11Texture2D> scene;ComPtr<ID3D11ShaderResourceView> source;
 check(gpu.device->CreateTexture2D(&td,&data,&scene),"runtime scene");check(gpu.device->CreateShaderResourceView(scene.Get(),nullptr,&source),"runtime scene view");
 auto* srv=source.Get();gpu.context->PSSetShaderResources(1,1,&srv);
 for(unsigned frame=0;frame<2;frame++) {
  {auto scope=runtime.scopeDraw(gpu.context.Get(),true);require(scope.active(),"glare runtime scope");
   ComPtr<ID3D11ShaderResourceView> output;gpu.context->PSGetShaderResources(5,1,&output);require(bool(output),"runtime glare output");
   ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> texture;output->GetResource(&resource);check(resource.As(&texture),"runtime output texture");
   texture->GetDesc(&td);td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;
   check(gpu.device->CreateTexture2D(&td,nullptr,&staging),"runtime staging");gpu.context->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE map{};
   check(gpu.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"runtime readback");bool nonzero=false;
   for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
    const auto* value=reinterpret_cast<const std::uint16_t*>(static_cast<char*>(map.pData)+y*map.RowPitch)+4*x;
    for(unsigned c=0;c<3;c++) {const float f=DirectX::PackedVector::XMConvertHalfToFloat(value[c]);require(std::isfinite(f),"finite glare");if(x>=width/2||c!=0)require(f==0,"eyes and colour channels remain isolated");else nonzero|=f!=0;}
   }
   gpu.context->Unmap(staging.Get(),0);require(nonzero,"bright scene generates glare");
  }
  ComPtr<ID3D11ShaderResourceView> restored;gpu.context->PSGetShaderResources(5,1,&restored);require(!restored,"glare scope restores caller resources");
 }
 require(runtime.snapshot().psfGenerations==initialGenerations+1,"unchanged frame reuses physical aperture kernel");
 runtime.onDeviceCreated(nullptr,nullptr);gpu.context->ClearState();gpu.validateMessages();
 std::cout<<"Physical glare runtime: live output, cached aperture, eye/channel isolation and restoration passed\n";
}
void pbrIntegrationTest(GPU& gpu,const std::filesystem::path& root) {
 auto directory=root/"tests/fixtures/pbr";
 auto shader=gpu.shader(directory/"MaterialIntegrationCS.hlsl",directory,false,false,false,true,"main");
 auto input=gpu.texture(8),output=gpu.texture(8);gpu.dispatch(shader.Get(),input,output,8,nullptr,true);
 auto values=gpu.read(output,8);const float expected[]{.8f,.8f,.2f,.16f,.16f,.6f,.1f,.4f};
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++) {
  require(std::abs(values[y*8+x].x-.25f)<1e-6f,"Smith normal-incidence visibility depends on roughness");
  require(std::abs(values[y*8+x].y-expected[y])<1e-5f,"GI material response lost metalness, settings or authored roughness");
 }
 gpu.context->ClearState();gpu.validateMessages();
}
void run(const std::filesystem::path& root,bool hardware) {
 GPU gpu(hardware);skylightingDepthTest(gpu,root);pbrIntegrationTest(gpu,root);metalReflectionTest(gpu,root);giFilterTest(gpu,root,hardware);runtimeTest(gpu);auto source=root/"package/Shaders/Community/BloomGlare";auto reference=root/"tests/fixtures/glare";
 auto oldRow=gpu.shader(reference/"ReferenceFft.hlsl",source,true,false),oldCol=gpu.shader(reference/"ReferenceFft.hlsl",source,false,false),oldInvRow=gpu.shader(reference/"ReferenceFft.hlsl",source,true,true),oldInvCol=gpu.shader(reference/"ReferenceFft.hlsl",source,false,true),multiply=gpu.shader(reference/"ReferenceMultiply.hlsl",source,false,false,false,true);
 auto row=gpu.shader(source/"GlareFftCS.hlsl",source,true,false),col=gpu.shader(source/"GlareFftCS.hlsl",source,false,false),convolve=gpu.shader(source/"GlareFftCS.hlsl",source,true,true,true),invCol=gpu.shader(source/"GlareFftCS.hlsl",source,false,true);
 for(unsigned n:{128,256,512}) {
  auto input=gpu.texture(n),psf=gpu.texture(n),a=gpu.texture(n),b=gpu.texture(n),c=gpu.texture(n),d=gpu.texture(n);
  std::array<float,28> params{};params[11]=float(n);D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(params);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA initial{params.data(),0,0};ComPtr<ID3D11Buffer> cb;check(gpu.device->CreateBuffer(&bd,&initial,&cb),"constants");auto* constants=cb.Get();gpu.context->CSSetConstantBuffers(13,1,&constants);
  const auto baseline=[&]{gpu.dispatch(oldRow.Get(),input,a,n);gpu.dispatch(oldCol.Get(),a,b,n);gpu.dispatch(multiply.Get(),b,a,n,&psf,true);gpu.dispatch(oldInvRow.Get(),a,b,n);gpu.dispatch(oldInvCol.Get(),b,a,n);};
  const auto optimized=[&]{gpu.dispatch(row.Get(),input,c,n);gpu.dispatch(col.Get(),c,d,n);gpu.dispatch(convolve.Get(),d,c,n,&psf);gpu.dispatch(invCol.Get(),c,d,n);};
  double maxError=0;std::vector<V> pixels(n*n),kernel(n*n);
  for(unsigned pattern=0;pattern<5;pattern++) {
   for(unsigned i=0;i<n*n;i++) {
    pixels[i]={float((i*1664525u+1013904223u)%1000)/100, float((i*22695477u)%1000)/100};
    if(pattern==1)pixels[i]={i==n*n/2?60000.0f:0.0f,0};
    if(pattern==2)pixels[i]={32,0};if(pattern==3)pixels[i]={0,0};
    kernel[i]={0.05f+float((i*69069u)%1000)/1000,float((i*16807u)%1000)/10000};
   }
   kernel[0]={pattern==4?0.0f:2.0f,0};
   gpu.context->UpdateSubresource(input.texture.Get(),0,nullptr,pixels.data(),n*sizeof(V),0);gpu.context->UpdateSubresource(psf.texture.Get(),0,nullptr,kernel.data(),n*sizeof(V),0);
   baseline();optimized();auto x=gpu.read(a,n),y=gpu.read(d,n);
   for(unsigned i=0;i<x.size();i++)for(unsigned component=0;component<2;component++){
    const float left=component?x[i].y:x[i].x,right=component?y[i].y:y[i].x;
    const double error=std::abs(double(left)-right)/(std::max)(1.0,std::abs(double(left)));maxError=(std::max)(maxError,error);require(std::isfinite(right)&&error<=1e-5,"FFT convolution changed output");
   }
  }
  std::cout<<(hardware?"Hardware":"WARP")<<" FFT "<<n<<": five patterns, maximum scaled error="<<maxError<<'\n';
  if(hardware) {
   std::array<double,5> oldTimes{},newTimes{};for(unsigned round=0;round<5;round++){if(round%2){newTimes[round]=gpu.time(optimized);oldTimes[round]=gpu.time(baseline);}else{oldTimes[round]=gpu.time(baseline);newTimes[round]=gpu.time(optimized);}}
   std::sort(oldTimes.begin(),oldTimes.end());std::sort(newTimes.begin(),newTimes.end());std::cout<<"FFT convolution GPU ms per channel: before="<<oldTimes[2]<<", after="<<newTimes[2]<<", reduction="<<100*(1-newTimes[2]/oldTimes[2])<<"%\n";
  }
  gpu.context->ClearState();gpu.validateMessages();
 }
}
int main(int argc,char** argv){try{require(argc==2,"root path required");run(argv[1],false);run(argv[1],true);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
