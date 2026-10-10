#include "support/ReadableMemory.h"
#include "render/GpuTimingProfiler.h"
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main() {
 try {
  SYSTEM_INFO system{};GetSystemInfo(&system);const auto page=system.dwPageSize;
  auto* bytes=static_cast<char*>(VirtualAlloc(nullptr,page*3,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  require(bytes,"allocate boundary fixture");
  struct Release {void* p;~Release(){VirtualFree(p,0,MEM_RELEASE);}} release{bytes};
  std::memset(bytes,'a',page*3);DWORD old{};
  require(VirtualProtect(bytes+page,page,PAGE_READONLY,&old),"split committed regions");
  std::array<char,256> output{};
  bytes[page-1]=0;
  require(csah::support::readCString(reinterpret_cast<std::uintptr_t>(bytes+page-8),output.data(),output.size()) && std::strlen(output.data())==7,"string terminator before region boundary");
  std::array<char,16> memory{};
  require(csah::support::readMemory(reinterpret_cast<std::uintptr_t>(bytes+page-8),memory.data(),memory.size()),"copy across separately protected readable regions");
  require(VirtualProtect(bytes+page,page,PAGE_READWRITE,&old),"edit second region");
  bytes[page-1]='a';bytes[page+7]=0;
  require(VirtualProtect(bytes+page,page,PAGE_READONLY,&old),"split regions again");
  require(csah::support::readCString(reinterpret_cast<std::uintptr_t>(bytes+page-8),output.data(),output.size()) && std::strlen(output.data())==15,"string crossing readable regions");
  require(VirtualProtect(bytes+page,page,PAGE_NOACCESS,&old),"protect boundary");
  require(!csah::support::readMemory(reinterpret_cast<std::uintptr_t>(bytes+page-8),memory.data(),memory.size()),"guarded memory rejected");
  require(!csah::support::readCString(reinterpret_cast<std::uintptr_t>(bytes+page-8),output.data(),output.size()),"unterminated string at inaccessible page rejected");
  require(!csah::support::readCString(reinterpret_cast<std::uintptr_t>(bytes),output.data(),output.size()),"bounded unterminated name rejected");
  require(!csah::support::readMemory(0,memory.data(),memory.size()) && !csah::support::readMemory(UINTPTR_MAX-3,memory.data(),memory.size()),"null and overflow rejected");
  const char* name="CommonwealthWorldspaceReference";std::strcpy(bytes,name);
  const auto measure=[&](bool batched) {
   auto start=std::chrono::steady_clock::now();
   for(unsigned i=0;i<10000;i++) {
    if(batched) require(csah::support::readCString(reinterpret_cast<std::uintptr_t>(bytes),output.data(),output.size()),"batched read");
    else for(std::size_t j=0;j<output.size();j++) {MEMORY_BASIC_INFORMATION m{};require(VirtualQuery(bytes+j,&m,sizeof(m))==sizeof(m),"scalar query");output[j]=bytes[j];if(!output[j]) break;}
    require(std::strcmp(output.data(),name)==0,"same string result");
   }
   return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  };
  const auto scalar=measure(false),batched=measure(true);
  std::cout<<"10000 name reads: per-character="<<scalar<<" ms, per-region="<<batched<<" ms\n";
  using Profiler=csah::render::GpuTimingProfiler;
  for(auto driver:{D3D_DRIVER_TYPE_WARP,D3D_DRIVER_TYPE_HARDWARE}) {
   ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
   require(SUCCEEDED(D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"D3D device");
   Profiler::setOnDemandGroups(0);Profiler p;
   require(p.initialize(device.Get(),context.Get(),"test",{"work",nullptr,nullptr,nullptr},1),"initialize idle profiler");
   for(unsigned i=0;i<1000;i++) require(!p.begin(),"off does not collect startup timestamps");
   Profiler::setOnDemandGroups(1);require(!p.begin(),"unselected group does not collect");
   Profiler::setOnDemandGroups(4);require(Profiler::groupRequested(Profiler::Group::General),"general selected");
   {auto sample=p.begin();require(bool(sample),"live enable allocates and collects");require(!p.begin(),"no overlapping disjoint intervals");sample.mark();}
   Profiler::setOnDemandGroups(0);for(unsigned i=0;i<32;i++) require(!p.begin(),"live disable remains idle");
   Profiler::setOnDemandGroups(4);{auto sample=p.begin();require(bool(sample),"reenable resumes collection");sample.mark();}
   p.reset();Profiler::setOnDemandGroups(0);
  }
  std::cout<<"Memory bounds, identical reads, profiling default-off, live enable/disable and nested scopes passed\n";
  return 0;
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
