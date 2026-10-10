#include "diagnostics/stereo_measurement/StereoMeasurement.h"
#include "render/ComputeStateScope.h"
#include "support/Logger.h"
#include "StereoMeasurementCS.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace csah::diagnostics::stereo_measurement {
namespace {
template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
struct ProbeState {
    ID3D11DeviceContext* context;Ptr<ID3D11DeviceContext1> extended;
    render::ScopedComputeState compute;
    std::array<Ptr<ID3D11Buffer>,3> buffers;
    static constexpr UINT slots[]{0,8,12};
    std::array<UINT,3> first{},count{};
    Ptr<ID3D11Predicate> predicate;BOOL predicateValue{};
    ProbeState(ID3D11DeviceContext* c):context(c),compute(c,{0,5,0,1,0,0,0,0}) {
        (void)c->QueryInterface(IID_PPV_ARGS(&extended));
        for(UINT i=0;i<3;i++) {const UINT slot=slots[i];
            if(extended)extended->CSGetConstantBuffers1(slot,1,&buffers[i],&first[i],&count[i]);
            else c->CSGetConstantBuffers(slot,1,&buffers[i]);
        }
        c->GetPredication(&predicate,&predicateValue);c->SetPredication(nullptr,FALSE);
    }
    ~ProbeState(){for(UINT i=0;i<3;i++){auto* b=buffers[i].Get();const UINT slot=slots[i];
        if(extended)extended->CSSetConstantBuffers1(slot,1,&b,&first[i],&count[i]);
        else context->CSSetConstantBuffers(slot,1,&b);}
        context->SetPredication(predicate.Get(),predicateValue);
    }
};
bool describe(ID3D11ShaderResourceView* view,D3D11_TEXTURE2D_DESC& td,DXGI_FORMAT& format,Ptr<ID3D11Resource>& resource) noexcept {
    if(!view)return false;D3D11_SHADER_RESOURCE_VIEW_DESC vd{};view->GetDesc(&vd);
    if(vd.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||vd.Texture2D.MostDetailedMip!=0)return false;
    format=vd.Format;view->GetResource(&resource);Ptr<ID3D11Texture2D> t;
    if(FAILED(resource.As(&t)))return false;t->GetDesc(&td);
    return td.ArraySize==1&&td.SampleDesc.Count==1&&td.Width>1&&td.Height>0;
}
bool validViewport(const D3D11_VIEWPORT& v,UINT w,UINT h) noexcept {
    return std::isfinite(v.TopLeftX)&&std::isfinite(v.TopLeftY)&&std::isfinite(v.Width)&&std::isfinite(v.Height)&&
        v.TopLeftX>=0&&v.TopLeftY>=0&&v.Width>=8&&v.Height>=8&&v.TopLeftX+v.Width<=float(w)&&v.TopLeftY+v.Height<=float(h)&&
        std::floor(v.TopLeftX)==v.TopLeftX&&std::floor(v.TopLeftY)==v.TopLeftY&&
        std::floor(v.Width)==v.Width&&std::floor(v.Height)==v.Height&&(UINT(v.Width)%2)==0;
}
}
Runtime& Runtime::get() noexcept {static Runtime instance;return instance;}
void Runtime::setEnabled(bool value) noexcept {
    if(enabled_.exchange(value,std::memory_order_acq_rel)!=value)resetWorld();
}
void Runtime::synchronize() noexcept {
    const auto current=epoch_.load(std::memory_order_acquire);if(current==observedEpoch_)return;
    span_.finish();observedEpoch_=current;materialNormal_.Reset();motionView_.Reset();motionResource_.Reset();motionThisFrame_=false;
    snapshot_={};totals_={};windowFrames_=windowDraws_=timedFrames_=frame_=draws_=retryAfter_=motionRetryAfter_=0;
    sampleFrame_=~0ull;width_=height_=0;firstReject_=firstSample_=noSampleWarning_=false;
    for(auto& slot:slots_)slot.pending=false;
}
void Runtime::onDeviceCreated(ID3D11Device* d,ID3D11DeviceContext* c) noexcept {
    span_.finish();spanTiming_.reset();probeTiming_.reset();slots_={};shader_.Reset();parameters_.Reset();
    motionView_.Reset();motionResource_.Reset();materialNormal_.Reset();device_=d;context_=c;context1_.Reset();
    resetWorld();synchronize();if(!d||!c)return;(void)c->QueryInterface(IID_PPV_ARGS(&context1_));
    (void)spanTiming_.initialize(d,c,"Stereo measurement",{"before first material","material-to-ambient span","comparison","stats copy"},4,128,1,render::GpuTimingProfiler::Group::StereoMeasurements);
    (void)probeTiming_.initialize(d,c,"Stereo comparison without material timing",{"comparison + stats copy",nullptr,nullptr,nullptr},1,128,1,render::GpuTimingProfiler::Group::StereoMeasurements);
}
void Runtime::reject(const char* why) noexcept {
    if(!firstReject_){firstReject_=true;logging::warn("Stereo measurement rejected inputs: {}. Native rendering is unchanged.",why);}
}
bool Runtime::ensureResources() noexcept {
    if(shader_&&parameters_&&slots_.back().event)return true;
    if(frame_<retryAfter_||!device_)return false;retryAfter_=frame_+120;
    if(!shader_&&FAILED(device_->CreateComputeShader(csah_stereo_measurement_cs,sizeof(csah_stereo_measurement_cs),nullptr,&shader_))){reject("comparison shader creation failed");return false;}
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=48;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(!parameters_&&FAILED(device_->CreateBuffer(&bd,nullptr,&parameters_))){reject("parameters allocation failed");return false;}
    std::array<Slot,4> next;
    for(auto& s:next){
        bd={};bd.ByteWidth=kCounterCount*sizeof(UINT);bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        if(FAILED(device_->CreateBuffer(&bd,nullptr,&s.gpu))){reject("statistics allocation failed");return false;}
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R32_UINT;ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=kCounterCount;
        if(FAILED(device_->CreateUnorderedAccessView(s.gpu.Get(),&ud,&s.output))){reject("statistics UAV failed");return false;}
        bd.BindFlags=0;bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};
        if(FAILED(device_->CreateBuffer(&bd,nullptr,&s.staging))||FAILED(device_->CreateQuery(&q,&s.event))){reject("readback ring allocation failed");return false;}
    }
    slots_=std::move(next);return true;
}
void Runtime::beginFrame() noexcept {
    synchronize();span_.finish();materialNormal_.Reset();motionThisFrame_=false;draws_=0;
    if(!enabled_.load(std::memory_order_acquire)||!context_)return;
    ++frame_;if(!ensureResources())return;poll();spanTiming_.poll();probeTiming_.poll();
    span_=spanTiming_.begin();
    if(frame_>=120&&snapshot_.submitted==0&&!noSampleWarning_){noSampleWarning_=true;
        logging::warn("Stereo measurement has no qualified ambient samples. Linear Lighting or Skylighting must expose a tracked DFLight ambient draw; no culling is active.");}
}
void Runtime::observeDraw(ID3D11DeviceContext* c,bool material,bool ambient) noexcept {
    if(!enabled_.load(std::memory_order_acquire)||c!=context_.Get()||(!material&&!ambient))return;
    synchronize();if(sampleFrame_==frame_)return;
    if(material)materialDraw(c);if(ambient)sample(c);
}
void Runtime::materialDraw(ID3D11DeviceContext* c) noexcept {
    ++draws_;if(materialNormal_)return;
    std::array<ID3D11RenderTargetView*,8> raw{};c->OMGetRenderTargets(8,raw.data(),nullptr);
    std::array<Ptr<ID3D11RenderTargetView>,8> views;for(UINT i=0;i<8;i++)views[i].Attach(raw[i]);
    Ptr<ID3D11Resource> normal,motion;D3D11_TEXTURE2D_DESC normalDescription{};
    for(auto& view:views){if(!view)continue;D3D11_RENDER_TARGET_VIEW_DESC vd{};view->GetDesc(&vd);
        if(vd.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D||vd.Texture2D.MipSlice!=0)continue;
        if(vd.Format==DXGI_FORMAT_R16G16_UNORM)view->GetResource(normal.ReleaseAndGetAddressOf());
        if(vd.Format==DXGI_FORMAT_R16G16_FLOAT)view->GetResource(motion.ReleaseAndGetAddressOf());
    }
    Ptr<ID3D11Texture2D> texture;if(!normal||FAILED(normal.As(&texture)))return;
    texture->GetDesc(&normalDescription);if(normalDescription.ArraySize!=1||normalDescription.SampleDesc.Count!=1)return;
    UINT count=1;D3D11_VIEWPORT vp{};c->RSGetViewports(&count,&vp);
    if(count!=1||!validViewport(vp,normalDescription.Width,normalDescription.Height))return;
    materialNormal_=normal;
    if(motion){Ptr<ID3D11Texture2D> t;if(SUCCEEDED(motion.As(&t))){D3D11_TEXTURE2D_DESC td{};t->GetDesc(&td);
        if(td.Width==normalDescription.Width&&td.Height==normalDescription.Height&&td.ArraySize==1&&td.SampleDesc.Count==1&&(td.BindFlags&D3D11_BIND_SHADER_RESOURCE)){
            if(motion.Get()!=motionResource_.Get()){motionView_.Reset();motionResource_=motion;motionRetryAfter_=0;}
            if(!motionView_ && frame_>=motionRetryAfter_){
                motionRetryAfter_=frame_+120;
                D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R16G16_FLOAT;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
                if(FAILED(device_->CreateShaderResourceView(motion.Get(),&sd,&motionView_)))reject("native motion SRV unavailable; motion agreement will be unmeasured");}
            motionThisFrame_=motionView_!=nullptr;
        }
    }}
    span_.mark();
}
void Runtime::sample(ID3D11DeviceContext* c) noexcept {
    std::array<Ptr<ID3D11ShaderResourceView>,4> views;std::array<ID3D11ShaderResourceView*,4> raw{};c->PSGetShaderResources(0,4,raw.data());
    std::array<Ptr<ID3D11Resource>,4> resources;std::array<D3D11_TEXTURE2D_DESC,4> td{};std::array<DXGI_FORMAT,4> formats{};
    for(UINT i=0;i<4;i++)views[i].Attach(raw[i]);
    for(UINT i=0;i<4;i++){if(!describe(views[i].Get(),td[i],formats[i],resources[i])){reject("DFLight t0-t3 must be single-sample 2D buffers");return;}}
    for(UINT i=0;i<3;i++)if(td[i].Width!=td[3].Width||td[i].Height!=td[3].Height){reject("G-buffer dimensions disagree");return;}
    if(formats[1]!=DXGI_FORMAT_R16G16_UNORM||formats[2]!=DXGI_FORMAT_R8G8B8A8_UNORM||
        (formats[0]!=DXGI_FORMAT_R8G8B8A8_UNORM&&formats[0]!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)){reject("unrecognized native G-buffer formats");return;}
    UINT count=1;D3D11_VIEWPORT vp{};c->RSGetViewports(&count,&vp);
    if(count!=1||!validViewport(vp,td[3].Width,td[3].Height)){reject("invalid packed stereo viewport");return;}
    Ptr<ID3D11Buffer> camera;UINT first{},constants{};
    if(context1_)context1_->PSGetConstantBuffers1(12,1,&camera,&first,&constants);else c->PSGetConstantBuffers(12,1,&camera);
    if(!camera){reject("missing native camera b12");return;}D3D11_BUFFER_DESC bd{};camera->GetDesc(&bd);
    if(bd.ByteWidth/16<first||bd.ByteWidth/16-first<61||(context1_&&constants<61)){reject("native camera window is too small");return;}
    Ptr<ID3D11Buffer> stereo;UINT stereoFirst{},stereoCount{};
    if(context1_)context1_->PSGetConstantBuffers1(8,1,&stereo,&stereoFirst,&stereoCount);else c->PSGetConstantBuffers(8,1,&stereo);
    if(!stereo){reject("missing native stereo b8");return;}stereo->GetDesc(&bd);
    if(bd.ByteWidth/16<=stereoFirst||(context1_&&stereoCount<1)){reject("native stereo window is too small");return;}
    if(width_&&(width_!=td[3].Width||height_!=td[3].Height||viewport_.TopLeftX!=vp.TopLeftX||viewport_.TopLeftY!=vp.TopLeftY||viewport_.Width!=vp.Width||viewport_.Height!=vp.Height)){
        resetWorld();synchronize();
    }
    width_=td[3].Width;height_=td[3].Height;viewport_=vp;
    if(!ensureResources())return;
    auto found=std::find_if(slots_.begin(),slots_.end(),[](const Slot& slot){return !slot.pending;});
    if(found==slots_.end()){++snapshot_.skipped;span_.finish();sampleFrame_=frame_;return;}
    auto& slot=*found;const bool matched=materialNormal_.Get()==resources[1].Get();
    const bool timed=matched&&bool(span_);if(!timed)span_.finish();
    auto late=timed?render::GpuTimingProfiler::Scope{}:probeTiming_.begin();
    if(timed)span_.mark();
    const bool motion=matched&&motionThisFrame_&&motionView_;
    struct Parameters{UINT x,y,w,h,stride,motion;float nearDistance,depthTolerance;UINT phaseX,phaseY,pad0,pad1;};
    static_assert(sizeof(Parameters)==48);
    const Parameters parameters{UINT(vp.TopLeftX),UINT(vp.TopLeftY),UINT(vp.Width),UINT(vp.Height),4,motion?1u:0u,200,.001f,UINT(snapshot_.submitted&3u),UINT((snapshot_.submitted>>2u)&3u),0,0};
    ProbeState restore(c);if(!restore.compute.captured()){reject("compute state could not be retained");span_.finish();return;}
    c->UpdateSubresource(parameters_.Get(),0,nullptr,&parameters,0,0);auto* cb=parameters_.Get();c->CSSetConstantBuffers(0,1,&cb);
    cb=camera.Get();if(context1_)context1_->CSSetConstantBuffers1(12,1,&cb,&first,&constants);else c->CSSetConstantBuffers(12,1,&cb);
    cb=stereo.Get();if(context1_)context1_->CSSetConstantBuffers1(8,1,&cb,&stereoFirst,&stereoCount);else c->CSSetConstantBuffers(8,1,&cb);
    const std::array<ID3D11ShaderResourceView*,5> inputs{views[0].Get(),views[1].Get(),views[2].Get(),views[3].Get(),motion?motionView_.Get():nullptr};
    constexpr UINT zero[4]{};c->ClearUnorderedAccessViewUint(slot.output.Get(),zero);
    auto* out=slot.output.Get();c->CSSetShaderResources(0,5,inputs.data());c->CSSetUnorderedAccessViews(0,1,&out,nullptr);c->CSSetShader(shader_.Get(),nullptr,0);
    c->Dispatch(((parameters.w/2+3)/4+7)/8,((parameters.h+3)/4+7)/8,1);
    if(timed)span_.mark();out=nullptr;c->CSSetUnorderedAccessViews(0,1,&out,nullptr);
    c->CopyResource(slot.staging.Get(),slot.gpu.Get());c->End(slot.event.Get());
    span_.finish();late.finish();slot.pending=true;slot.epoch=observedEpoch_;slot.motion=motion;slot.timed=timed;slot.draws=draws_;
    sampleFrame_=frame_;++snapshot_.submitted;
    if(!firstSample_){firstSample_=true;logging::info("Stereo measurement active: allocation={}x{}, viewport=({},{},{}x{}), spatial stride=4 with 16 rotating phases, near exclusion=200 units, depth tolerance=0.1%, normal cosine>=0.995, albedo<=2/255, material<=1/255, copied motion delta<=0.0001 native UV. Both eyes render normally; no scene writes or culling. Material/PBR eligibility and net savings are NOT established by matches.",width_,height_,vp.TopLeftX,vp.TopLeftY,vp.Width,vp.Height);}
}
void Runtime::poll() noexcept {
    for(auto& slot:slots_){if(!slot.pending)continue;
        const auto ready=context_->GetData(slot.event.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(ready==S_FALSE)continue;if(FAILED(ready)){slot.pending=false;reject("readback completion failed");continue;}
        if(slot.epoch!=observedEpoch_){slot.pending=false;continue;}
        D3D11_MAPPED_SUBRESOURCE mapped{};const auto hr=context_->Map(slot.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
        if(hr==DXGI_ERROR_WAS_STILL_DRAWING)continue;if(FAILED(hr)){slot.pending=false;reject("nonblocking statistics readback failed");continue;}
        std::memcpy(snapshot_.last.data(),mapped.pData,sizeof(snapshot_.last));context_->Unmap(slot.staging.Get(),0);slot.pending=false;
        ++snapshot_.completed;++windowFrames_;windowDraws_+=slot.draws;if(slot.timed)++timedFrames_;
        for(UINT i=0;i<kCounterCount;i++){if(i>=18&&i<=21)totals_[i]=(std::max)(totals_[i],std::uint64_t(snapshot_.last[i]));else totals_[i]+=snapshot_.last[i];}
    }
    if(windowFrames_<128)return;
    const auto percent=[&](UINT a,UINT b){return totals_[b]?100.0*double(totals_[a])/double(totals_[b]):0.0;};
    logging::info("Stereo measurement [{} frames]: geometry matches={:.2f}% of {} sampled right-eye pixels; within geometry, normal/albedo/material agreement={:.2f}/{:.2f}/{:.2f}%, all three={:.2f}%; copied motion agreement={:.2f}% of {} valid motion pairs; all checked including motion={:.2f}% of total. Reject sky/firstPerson/near/outside/invalid/edge/depth/roundtrip={}/{}/{}/{}/{}/{}/{}/{}. Max errors normal/albedo/material/nativeMotionUV={:.6f}/{:.6f}/{:.6f}/{:.6f}; invalid motion={}, non-stereo samples={}; material timing frames={}/{}, tracked material draws/frame={:.1f}, skipped busy samples={}. Candidate matches are not safe-to-cull coverage or predicted FPS.",
        windowFrames_,percent(GeometryMatch,Samples),totals_[Samples],percent(NormalMatch,GeometryMatch),percent(AlbedoMatch,GeometryMatch),percent(MaterialMatch,GeometryMatch),percent(AllMaterialMatch,GeometryMatch),percent(MotionMatch,MotionAvailable),totals_[MotionAvailable],percent(AllWithMotion,Samples),
        totals_[Sky],totals_[FirstPerson],totals_[Near],totals_[Outside],totals_[Invalid],totals_[Edge],totals_[DepthMismatch],totals_[RoundTripMismatch],
        totals_[MaxNormalError]/1e6,totals_[MaxAlbedoError]/1e6,totals_[MaxMaterialError]/1e6,totals_[MaxMotionError]/1e6,totals_[InvalidMotion],totals_[NonStereo],timedFrames_,windowFrames_,double(windowDraws_)/double(windowFrames_),snapshot_.skipped);
    totals_={};windowFrames_=windowDraws_=timedFrames_=0;
}
}
