// SPDX-License-Identifier: GPL-3.0-only
// Adapted capture regression fixture from FO4CloudShadows.
// GPU contract tests use generated textures/constants, never game assets.
#include "CloudGeometryCapture.h"
#include "CloudShadowRuntime.h"
#include "CloudFieldUpdate.h"




#include <d3d11_1.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace Motion = csah::cloud_shadows::geometry;
using Float4 = std::array<float, 4>;

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void Check(HRESULT result, const char* message)
    {
        Require(SUCCEEDED(result), message);
    }

    struct GPU
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<ID3D11InfoQueue> messages;

        explicit GPU(bool hardware = false)
        {
            D3D_FEATURE_LEVEL level{};
            HRESULT result = D3D11CreateDevice(nullptr,
                hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP,
                nullptr, hardware ? 0u : D3D11_CREATE_DEVICE_DEBUG, nullptr, 0,
                D3D11_SDK_VERSION, &device, &level, &context);
            if (result == DXGI_ERROR_SDK_COMPONENT_MISSING) {
                result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP,
                    nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                    &device, &level, &context);
                std::cout << "D3D debug layer unavailable; GPU assertions still run\n";
            }
            Check(result, "create renderer test device");
            (void)device.As(&messages);
            if (hardware) {
                ComPtr<IDXGIDevice> dxgi;
                ComPtr<IDXGIAdapter> adapter;
                Check(device.As(&dxgi), "query benchmark adapter");
                Check(dxgi->GetAdapter(&adapter), "get benchmark adapter");
                DXGI_ADAPTER_DESC desc{};
                Check(adapter->GetDesc(&desc), "benchmark adapter description");
                std::wcout << L"Hardware renderer adapter: " << desc.Description << L'\n';
            }
        }

        template<class Work>
        double Measure(Work work, UINT repetitions)
        {
            ComPtr<ID3D11Query> disjoint, start, end;
            D3D11_QUERY_DESC desc{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
            Check(device->CreateQuery(&desc, &disjoint), "create disjoint query");
            desc.Query = D3D11_QUERY_TIMESTAMP;
            Check(device->CreateQuery(&desc, &start), "create start timestamp");
            Check(device->CreateQuery(&desc, &end), "create end timestamp");
            work();
            context->Begin(disjoint.Get());
            context->End(start.Get());
            for (UINT i = 0; i < repetitions; ++i)
                work();
            context->End(end.Get());
            context->End(disjoint.Get());
            context->Flush(); // Offline benchmark only; never linked into the DLL.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            const auto read = [&](ID3D11Query* query, void* data, UINT size) {
                HRESULT result;
                while ((result = context->GetData(query, data, size,
                            D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE) {
                    Require(std::chrono::steady_clock::now() < deadline, "GPU query timed out");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                Check(result, "read GPU timestamp");
            };
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT frequency{};
            UINT64 first{}, last{};
            read(disjoint.Get(), &frequency, sizeof(frequency));
            read(start.Get(), &first, sizeof(first));
            read(end.Get(), &last, sizeof(last));
            Require(!frequency.Disjoint && frequency.Frequency != 0, "stable GPU timestamps");
            return static_cast<double>(last - first) * 1000.0 /
                static_cast<double>(frequency.Frequency) / repetitions;
        }

        void CheckMessages()
        {
            if (!messages)
                return;
            for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
                SIZE_T size{};
                Check(messages->GetMessage(i, nullptr, &size), "query debug message");
                std::vector<std::byte> bytes(size);
                auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
                Check(messages->GetMessage(i, message, &size), "read debug message");
                if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) {
                    std::cerr << message->pDescription << '\n';
                    throw std::runtime_error("D3D debug-layer warning/error");
                }
            }
            messages->ClearStoredMessages();
        }

        template<class Work>
        void Benchmark(const std::string& name, Work work, UINT repetitions)
        {
            // Two warmup rounds, then nine measured rounds.
            std::array<double,9> values{};
            for (UINT round=0; round<11; ++round) {
                const double value = Measure(work,repetitions);
                if (round>=2) values[round-2]=value;
            }
            std::sort(values.begin(),values.end());
            std::cout << name << " GPU-span-ms median=" << values[4]
                << " min=" << values.front() << " max=" << values.back() << " batches=9\n";
        }

        ComPtr<ID3D11Buffer> Constants(const void* data, UINT size)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = size;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            D3D11_SUBRESOURCE_DATA initial{};
            initial.pSysMem = data;
            ComPtr<ID3D11Buffer> buffer;
            Check(device->CreateBuffer(&desc, &initial, &buffer), "create constants");
            return buffer;
        }

        ComPtr<ID3D11SamplerState> Sampler()
        {
            D3D11_SAMPLER_DESC desc{};
            desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            desc.MaxLOD = D3D11_FLOAT32_MAX;
            desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            ComPtr<ID3D11SamplerState> sampler;
            Check(device->CreateSamplerState(&desc, &sampler), "create sampler");
            return sampler;
        }

        std::vector<float> Read(ID3D11Resource* resource)
        {
            ComPtr<ID3D11Texture2D> source;
            Check(resource->QueryInterface(IID_PPV_ARGS(&source)), "read texture type");
            D3D11_TEXTURE2D_DESC desc{};
            source->GetDesc(&desc);
            const auto format = desc.Format;
            Require(format == DXGI_FORMAT_R16_FLOAT || format == DXGI_FORMAT_R32_FLOAT ||
                format == DXGI_FORMAT_R32G32B32A32_FLOAT || format == DXGI_FORMAT_R8G8B8A8_UNORM,
                "readback supports float and RGBA8 preview formats");
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = desc.MiscFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;
            Check(device->CreateTexture2D(&desc, nullptr, &staging), "create readback");
            context->CopyResource(staging.Get(), source.Get());
            std::vector<float> values;
            for (UINT face = 0; face < desc.ArraySize; ++face) {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                const UINT subresource = D3D11CalcSubresource(0, face, desc.MipLevels);
                Check(context->Map(staging.Get(), subresource, D3D11_MAP_READ, 0,
                    &mapped), "map test readback");
                for (UINT y = 0; y < desc.Height; ++y) {
                    const auto* row = static_cast<const std::byte*>(mapped.pData) +
                        y * mapped.RowPitch;
                    const UINT components = format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
                        format == DXGI_FORMAT_R8G8B8A8_UNORM ? 4u : 1u;
                    for (UINT x = 0; x < desc.Width * components; ++x) {
                        values.push_back(format == DXGI_FORMAT_R16_FLOAT
                            ? DirectX::PackedVector::XMConvertHalfToFloat(
                                reinterpret_cast<const uint16_t*>(row)[x])
                            : format == DXGI_FORMAT_R8G8B8A8_UNORM
                                ? static_cast<float>(reinterpret_cast<const uint8_t*>(row)[x]) / 255.0f
                                : reinterpret_cast<const float*>(row)[x]);
                    }
                }
                context->Unmap(staging.Get(), subresource);
            }
            return values;
        }
    };

    struct MotionFixture
    {
        GPU& gpu;
        Motion::SkyConstantLayout layout;
        std::array<Float4, 32> vertex{};
        std::array<Float4, 16> pixel{};
        ComPtr<ID3D11Buffer> vs;
        ComPtr<ID3D11Buffer> ps;
        ComPtr<ID3D11ShaderResourceView> texture;
        ComPtr<ID3D11SamplerState> sampler;
        uint64_t serial{};
        uint32_t faceSize = 8;

        MotionFixture(GPU& device, Motion::SkyConstantLayout constantLayout) :
            gpu(device), layout(constantLayout)
        {
            Motion::ReleaseDeviceResources();
            vs = gpu.Constants(vertex.data(), sizeof(vertex));
            ps = gpu.Constants(pixel.data(), sizeof(pixel));
            SetLive(0.0f, 0.5f, 0.0f);
            std::array<Float4, 16> texels{};
            for (size_t i = 0; i < texels.size(); ++i)
                texels[i] = { 1.0f, 1.0f, 1.0f, i % 4 < 2 ? 0.25f : 0.75f };
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = desc.Height = 4;
            desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{};
            data.pSysMem = texels.data();
            data.SysMemPitch = 4 * sizeof(Float4);
            ComPtr<ID3D11Texture2D> source;
            Check(gpu.device->CreateTexture2D(&desc, &data, &source), "cloud source");
            Check(gpu.device->CreateShaderResourceView(source.Get(), nullptr, &texture),
                "cloud source view");
            sampler = gpu.Sampler();
            ID3D11ShaderResourceView* sources[]{ texture.Get(), texture.Get() };
            ID3D11SamplerState* samplers[]{ sampler.Get(), sampler.Get() };
            gpu.context->PSSetShaderResources(0, 2, sources);
            gpu.context->PSSetSamplers(0, 2, samplers);
        }

        ~MotionFixture()
        {
            gpu.context->ClearState();
            Motion::ReleaseDeviceResources();
        }

        void SetLive(float offset, float blend, float parameter)
        {
            const bool vr = layout == Motion::SkyConstantLayout::kVr;
            vertex[vr ? 14 : 10] = { offset, 0, 0, 0 };
            vertex[vr ? 11 : 7][3] = blend;
            pixel[0][0] = parameter;
            gpu.context->UpdateSubresource(vs.Get(), 0, nullptr, vertex.data(), 0, 0);
            gpu.context->UpdateSubresource(ps.Get(), 0, nullptr, pixel.data(), 0, 0);
            ID3D11Buffer* rawVS = vs.Get();
            ID3D11Buffer* rawPS = ps.Get();
            gpu.context->VSSetConstantBuffers(2, 1, &rawVS);
            gpu.context->PSSetConstantBuffers(2, 1, &rawPS);
        }

    };

    void RunGeometryCaptureTests(GPU& gpu, Motion::SkyConstantLayout layout,
        float initialBlend = 0.5f, bool benchmark = false)
    {
        namespace Geometry = csah::cloud_shadows::geometry;
        MotionFixture fixture(gpu, layout);
        const bool vr = layout == Motion::SkyConstantLayout::kVr;
        const UINT world = vr ? 8u : 4u;
        // World is already camera-relative. Rotate the dome 90 degrees about
        // Z and offset it from the camera; its visible directions include both.
        fixture.vertex[world] = {0, -1, 0, 0.2f};
        fixture.vertex[world + 1] = {1, 0, 0, -0.15f};
        fixture.vertex[world + 2] = {0, 0, 1, 0.3f};
        // A layer can be transparent during loading, then become visible
        // without changing geometry. Its raw mesh alpha must survive bootstrap.
        fixture.SetLive(0.25f, initialBlend, 0);
        constexpr char shader[] = R"(
struct Input { float3 p : POSITION0; float2 uv : TEXCOORD0; float4 c : COLOR0; };
struct Output { float4 p : SV_Position; float2 uv : TEXCOORD0; float4 c : COLOR0; };
Output VS(Input i) { Output o; o.p=float4(i.p,1); o.uv=i.uv; o.c=i.c; return o; }
float4 PS(Output i) : SV_Target { return float4(i.uv,i.c.w,1); }
)";
        const auto compile = [&](const char* entry, const char* profile) {
            ComPtr<ID3DBlob> code, errors;
            Check(D3DCompile(shader, sizeof(shader)-1, nullptr, nullptr, nullptr,
                entry, profile, D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &code, &errors),
                "compile geometry test shader");
            return code;
        };
        const auto vertexCode = compile("VS", "vs_5_0");
        const auto pixelCode = compile("PS", "ps_5_0");
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader> ps;
        Check(gpu.device->CreateVertexShader(vertexCode->GetBufferPointer(),
            vertexCode->GetBufferSize(), nullptr, &vs), "test geometry VS");
        Check(gpu.device->CreatePixelShader(pixelCode->GetBufferPointer(),
            pixelCode->GetBufferSize(), nullptr, &ps), "test geometry PS");
        const D3D11_INPUT_ELEMENT_DESC elements[]{
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,20,D3D11_INPUT_PER_VERTEX_DATA,0}
        };
        ComPtr<ID3D11InputLayout> inputLayout;
        Check(gpu.device->CreateInputLayout(elements, 3,
            vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), &inputLayout),
            "test geometry input layout");
        struct Vertex { std::array<float,3> p; std::array<float,2> uv; Float4 color; };
        std::array<Vertex, 36> vertices{};
        // Independent cube mesh: X faces span Y/Z, Y faces X/Z, Z faces X/Y.
        constexpr std::array<std::array<float,2>,6> corners{{
            {-1,-1}, {-1,1}, {1,-1}, {1,-1}, {-1,1}, {1,1}
        }};
        for (UINT face = 0; face < 6; ++face) {
            const UINT axis = face / 2;
            for (UINT index = 0; index < 6; ++index) {
                auto& vertex = vertices[face * 6 + index];
                vertex.p[axis] = face % 2 ? -1.0f : 1.0f;
                vertex.p[(axis + 1) % 3] = corners[index][0];
                vertex.p[(axis + 2) % 3] = corners[index][1];
                vertex.uv = {face % 2 ? 0.625f : 0.125f, 0.5f};
                vertex.color = {1,1,1,1};
            }
        }
        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.ByteWidth = sizeof(vertices);
        bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = vertices.data();
        ComPtr<ID3D11Buffer> vb;
        Check(gpu.device->CreateBuffer(&bufferDesc, &initial, &vb), "test dome geometry");
        ID3D11Buffer* rawVB = vb.Get();
        const UINT stride = sizeof(Vertex), offset = 0;
        gpu.context->IASetVertexBuffers(0,1,&rawVB,&stride,&offset);
        gpu.context->IASetInputLayout(inputLayout.Get());
        gpu.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        gpu.context->VSSetShader(vs.Get(), nullptr, 0);
        gpu.context->PSSetShader(ps.Get(), nullptr, 0);
        ComPtr<ID3D11DeviceContext1> context1;
        Check(gpu.context.As(&context1), "geometry D3D11.1 context");
        ID3D11Buffer* sentinel = fixture.vs.Get();
        const UINT first = 16, count = 16;
        context1->VSSetConstantBuffers1(0,1,&sentinel,&first,&count);
        D3D11_TEXTURE2D_DESC targetDesc{};
        targetDesc.Width = targetDesc.Height = 4;
        targetDesc.ArraySize = targetDesc.MipLevels = targetDesc.SampleDesc.Count = 1;
        targetDesc.Format = DXGI_FORMAT_R32_FLOAT;
        targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> primary;
        ComPtr<ID3D11RenderTargetView> primaryView;
        Check(gpu.device->CreateTexture2D(&targetDesc, nullptr, &primary), "primary sentinel");
        Check(gpu.device->CreateRenderTargetView(primary.Get(), nullptr, &primaryView),
            "primary sentinel RTV");
        const float clear[]{0.625f,0,0,0};
        gpu.context->ClearRenderTargetView(primaryView.Get(), clear);
        auto* rawTarget = primaryView.Get();
        gpu.context->OMSetRenderTargets(1,&rawTarget,nullptr);
        const D3D11_VIEWPORT viewport{0,0,4,4,0,1};
        gpu.context->RSSetViewports(1,&viewport);
        struct Draw { ID3D11DeviceContext* context; UINT calls{}; bool vr; bool trimStereo{}; UINT submittedInstances{}; } draw{gpu.context.Get(),0,vr};
        ComPtr<ID3D11Buffer> restoredCB;
        UINT restoredFirst{}, restoredCount{};
        // Real FO4 cloud meshes contain overlapping surfaces within a single
        // draw. Both .25-alpha surfaces must contribute (.25 + .25*.75),
        // including in VR where native geometry is submitted once per eye.
        std::array<Vertex, 72> overlapping{};
        for (UINT i = 0; i < overlapping.size(); ++i) {
            UINT sourceIndex = i % static_cast<UINT>(vertices.size());
            // Make all six faces point inward, as the captured native cloud
            // shell does. The earlier no-cull fixture concealed a handedness
            // mismatch between native Ni-world and the cube-face projection.
            if ((sourceIndex / 6) % 2 != 0 && sourceIndex % 3 != 0)
                sourceIndex += sourceIndex % 3 == 1 ? 1u : UINT(-1);
            overlapping[i] = vertices[sourceIndex];
            for (auto& coordinate : overlapping[i].p)
                coordinate *= i < vertices.size() ? 1.0f : 1.2f;
            overlapping[i].uv = {0.125f, 0.5f};
        }
        bufferDesc.ByteWidth = sizeof(overlapping);
        initial.pSysMem = overlapping.data();
        ComPtr<ID3D11Buffer> overlapVB;
        Check(gpu.device->CreateBuffer(&bufferDesc, &initial, &overlapVB),
            "overlapping cloud surfaces");
        rawVB = overlapVB.Get();
        gpu.context->IASetVertexBuffers(0,1,&rawVB,&stride,&offset);
        D3D11_RASTERIZER_DESC noCull{};
        noCull.FillMode = D3D11_FILL_SOLID;
        noCull.CullMode = D3D11_CULL_NONE;
        noCull.DepthClipEnable = TRUE;
        ComPtr<ID3D11RasterizerState> noCullState;
        Check(gpu.device->CreateRasterizerState(&noCull,&noCullState), "overlap rasterizer");
        gpu.context->RSSetState(noCullState.Get());
        const UINT opacityFaceSize = benchmark ? 256u : 8u;
        targetDesc.Width = targetDesc.Height = opacityFaceSize;
        targetDesc.ArraySize = 6;
        targetDesc.Format = DXGI_FORMAT_R16_FLOAT;
        targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        targetDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        ComPtr<ID3D11Texture2D> opacity;
        Check(gpu.device->CreateTexture2D(&targetDesc,nullptr,&opacity), "overlap cube");
        std::array<ComPtr<ID3D11RenderTargetView>,6> opacityViews;
        std::array<ID3D11RenderTargetView*,6> opacityTargets{};
        const float zero[4]{};
        for (UINT face = 0; face < 6; ++face) {
            D3D11_RENDER_TARGET_VIEW_DESC view{};
            view.Format = DXGI_FORMAT_R16_FLOAT;
            view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.FirstArraySlice = face;
            view.Texture2DArray.ArraySize = 1;
            Check(gpu.device->CreateRenderTargetView(opacity.Get(),&view,
                &opacityViews[face]), "overlap face");
            opacityTargets[face] = opacityViews[face].Get();
        }
        const auto submitOverlap = [](void* user) {
            auto& request = *static_cast<Draw*>(user);
            ++request.calls;
            if (request.vr) {
                const auto instances=request.trimStereo?Geometry::opacityReplayInstanceCount(2,0):2;
                request.submittedInstances+=instances;
                request.context->DrawInstanced(72,instances,0,0);
            }
            else request.context->Draw(72,0);
        };
        // Include a transparent first frame and a later changed World matrix:
        // direct opacity must use this frame's geometry, UV, and blend.
        for (UINT frame = 0; frame < 3; ++frame) {
            fixture.vertex[world] = {1,0,0,frame == 2 ? 0.1f : 0.0f};
            fixture.vertex[world+1] = {0,1,0,0};
            fixture.vertex[world+2] = {0,0,1,0};
            fixture.SetLive(frame == 2 ? 0.5f : 0.0f, frame ? 1.0f : 0.0f, 0);
            for (auto* target : opacityTargets)
                gpu.context->ClearRenderTargetView(target,zero);
            Require(Geometry::AccumulateOpacity(gpu.context.Get(),layout,
                Motion::CloudTechnique::kClouds,opacityFaceSize,opacityTargets,submitOverlap,&draw),
                "capture overlapping live opacity");
            const float expected = frame == 0 ? 0.0f : frame == 1 ? 0.4375f : 0.9375f;
            const auto values = gpu.Read(opacity.Get());
            const size_t faceTexels = static_cast<size_t>(opacityFaceSize) * opacityFaceSize;
            for (size_t i = 0; i < values.size(); ++i) {
                const bool belowHorizonFace = i / faceTexels == 5;
                Require(std::abs(values[i] - (belowHorizonFace ? 0.0f : expected)) < 0.001f,
                    "all overlapping surfaces contribute exactly once, including VR stereo; "
                    "the never-sampled -Z face is not replayed");
            }
            if (vr) {
                for (auto* target : opacityTargets) gpu.context->ClearRenderTargetView(target,zero);
                const auto before=draw.submittedInstances;draw.trimStereo=true;
                Require(Geometry::AccumulateOpacity(gpu.context.Get(),layout,
                    Motion::CloudTechnique::kClouds,opacityFaceSize,opacityTargets,submitOverlap,&draw),
                    "single-instance cloud opacity replay");
                Require(gpu.Read(opacity.Get())==values,"dropping clipped eye instances changes no capture pixel");
                Require(draw.submittedInstances-before==Geometry::kCapturedCubeFaceCount,
                    "one instance per face instead of two");
                draw.trimStereo=false;
            }
            const UINT callsBefore = draw.calls;
            Require(Geometry::AccumulateOpacity(gpu.context.Get(),layout,
                Motion::CloudTechnique::kClouds,opacityFaceSize,opacityTargets,submitOverlap,&draw),
                "repeat capture for draw accounting");
            Require(draw.calls - callsBefore == Geometry::kCapturedCubeFaceCount,
                "one replay per captured face");
            for (float value : gpu.Read(primary.Get()))
                Require(value == 0.625f,"opacity capture leaves the visible target untouched");
        }
        // Different weather textures and a partial fade must preserve native
        // alpha math before source-over accumulation of the two surfaces.
        {
            D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;
            td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            const Float4 alpha{1,1,1,0.75f};D3D11_SUBRESOURCE_DATA data{alpha.data(),sizeof(Float4),0};
            ComPtr<ID3D11Texture2D> second;ComPtr<ID3D11ShaderResourceView> secondView;
            Check(gpu.device->CreateTexture2D(&td,&data,&second),"second cloud weather texture");
            Check(gpu.device->CreateShaderResourceView(second.Get(),nullptr,&secondView),"second cloud texture view");
            auto* view=secondView.Get();gpu.context->PSSetShaderResources(1,1,&view);
            for (auto technique : {Motion::CloudTechnique::kCloudsLerp,Motion::CloudTechnique::kCloudsFade}) {
                const bool lerp=technique==Motion::CloudTechnique::kCloudsLerp;
                fixture.SetLive(0,1,lerp?0.3f:0.7f);
                for(auto* target:opacityTargets) gpu.context->ClearRenderTargetView(target,zero);
                Require(Geometry::AccumulateOpacity(gpu.context.Get(),layout,technique,
                    opacityFaceSize,opacityTargets,submitOverlap,&draw),"weather layer replay");
                const float one=lerp?0.4f:0.125f,expected=1-(1-one)*(1-one);
                const auto values=gpu.Read(opacity.Get());
                for(size_t i=0;i<values.size();++i) Require(std::abs(values[i]-(i/(opacityFaceSize*opacityFaceSize)==5?0:expected))<0.001f,
                    "lerp/fade alpha is accumulated once per primitive, not once per eye");
            }
            view=fixture.texture.Get();gpu.context->PSSetShaderResources(1,1,&view);
            fixture.SetLive(0.5f,1,0);
        }
        // Native stock Sky uses back-face culling and CCW screen fronts.
        // Its view projection reverses winding relative to the cube bases.
        for (UINT winding = 0; winding < 2; ++winding) {
            D3D11_RASTERIZER_DESC nativeCull = noCull;
            nativeCull.CullMode = D3D11_CULL_BACK;
            nativeCull.FrontCounterClockwise = winding != 0;
            ComPtr<ID3D11RasterizerState> nativeCullState;
            Check(gpu.device->CreateRasterizerState(&nativeCull,&nativeCullState),
                "native sky culling convention");
            gpu.context->RSSetState(nativeCullState.Get());
            for (auto* target : opacityTargets)
                gpu.context->ClearRenderTargetView(target,zero);
            Require(Geometry::AccumulateOpacity(gpu.context.Get(),layout,
                Motion::CloudTechnique::kClouds,opacityFaceSize,opacityTargets,submitOverlap,&draw),
                "capture native front-facing overlapping cloud surfaces");
            const auto values = gpu.Read(opacity.Get());
            const size_t faceTexels = static_cast<size_t>(opacityFaceSize) * opacityFaceSize;
            for (size_t i = 0; i < values.size(); ++i) {
                const float expected = i / faceTexels == 5 ? 0.0f : (winding ? 0.9375f : 0.0f);
                Require(std::abs(values[i] - expected) < 0.001f,
                    "native CCW cloud fronts survive cube projection; reversed fronts are culled");
            }
            ComPtr<ID3D11RasterizerState> restoredRasterizer;
            gpu.context->RSGetState(&restoredRasterizer);
            Require(restoredRasterizer.Get() == nativeCullState.Get(),
                "opacity capture restores the native rasterizer");
        }
        gpu.context->RSSetState(noCullState.Get());
        restoredCB.Reset();
        context1->VSGetConstantBuffers1(0,1,&restoredCB,&restoredFirst,&restoredCount);
        Require(restoredCB.Get() == sentinel && restoredFirst == first && restoredCount == count,
            "opacity capture restores the exact native constant buffer window");
        if (benchmark) {
            gpu.Benchmark("Capture layers=9 cube=256x256x5 surfaces=2", [&] {
                for (auto* target : opacityTargets)
                    gpu.context->ClearRenderTargetView(target,zero);
                for (UINT layer = 0; layer < 9; ++layer)
                    Require(Geometry::AccumulateOpacity(gpu.context.Get(),layout,
                        Motion::CloudTechnique::kClouds,opacityFaceSize,opacityTargets,
                        submitOverlap,&draw), "benchmark live opacity");
            },30);
        }
        if (!benchmark) {
            // Repeated native draws must produce the same pixels when private
            // captures are inserted between them. Merely checking that capture
            // leaves RT0 untouched cannot detect state leaked into the next draw.
            constexpr char visibleShader[] = R"(
cbuffer NativeVS : register(b0) { float4 Marker; };
Texture2D<float4> CloudTexture : register(t0);
SamplerState CloudSampler : register(s0);
struct Input { float3 p : POSITION0; float2 uv : TEXCOORD0; float4 c : COLOR0; };
struct Output { float4 p : SV_Position; float2 uv : TEXCOORD0; };
Output VS(Input i) {
    Output o; o.p=float4(i.p.xy*0.7+Marker.xy,0.5,1); o.uv=i.uv; return o;
}
float4 PS(Output i) : SV_Target {
    return float4(CloudTexture.Sample(CloudSampler,i.uv).a*0.5+i.uv.x*0.25,0,0,0.6);
}
)";
            const auto compileVisible = [&](const char* entry, const char* profile) {
                ComPtr<ID3DBlob> code, errors;
                Check(D3DCompile(visibleShader, sizeof(visibleShader)-1, nullptr, nullptr,
                    nullptr, entry, profile, D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &code, &errors),
                    "compile visible sky roundtrip shader");
                return code;
            };
            auto visibleVSCode = compileVisible("VS", "vs_5_0");
            auto visiblePSCode = compileVisible("PS", "ps_5_0");
            ComPtr<ID3D11VertexShader> visibleVS;
            ComPtr<ID3D11PixelShader> visiblePS;
            Check(gpu.device->CreateVertexShader(visibleVSCode->GetBufferPointer(),
                visibleVSCode->GetBufferSize(), nullptr, &visibleVS), "visible sky roundtrip VS");
            Check(gpu.device->CreatePixelShader(visiblePSCode->GetBufferPointer(),
                visiblePSCode->GetBufferSize(), nullptr, &visiblePS), "visible sky roundtrip PS");
            std::array<Float4,32> marker{};
            marker[16] = {0.15f,-0.1f,0,0};
            auto markerCB = gpu.Constants(marker.data(), sizeof(marker));
            ID3D11Buffer* markerBinding = markerCB.Get();
            context1->VSSetConstantBuffers1(0,1,&markerBinding,&first,&count);
            gpu.context->VSSetShader(visibleVS.Get(),nullptr,0);
            gpu.context->PSSetShader(visiblePS.Get(),nullptr,0);
            gpu.context->OMSetRenderTargets(1,&rawTarget,nullptr);
            gpu.context->RSSetViewports(1,&viewport);
            D3D11_RASTERIZER_DESC visibleRasterizer = noCull;
            visibleRasterizer.ScissorEnable = TRUE;
            ComPtr<ID3D11RasterizerState> visibleRasterizerState;
            Check(gpu.device->CreateRasterizerState(&visibleRasterizer,&visibleRasterizerState),
                "visible sky scissor rasterizer");
            gpu.context->RSSetState(visibleRasterizerState.Get());
            const D3D11_RECT scissor{1,0,4,3};
            gpu.context->RSSetScissorRects(1,&scissor);
            D3D11_BLEND_DESC visibleBlend{};
            auto& blendTarget = visibleBlend.RenderTarget[0];
            blendTarget.BlendEnable = TRUE;
            blendTarget.SrcBlend = D3D11_BLEND_BLEND_FACTOR;
            blendTarget.DestBlend = D3D11_BLEND_INV_BLEND_FACTOR;
            blendTarget.BlendOp = D3D11_BLEND_OP_ADD;
            blendTarget.SrcBlendAlpha = D3D11_BLEND_ONE;
            blendTarget.DestBlendAlpha = D3D11_BLEND_ZERO;
            blendTarget.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blendTarget.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED;
            ComPtr<ID3D11BlendState> visibleBlendState;
            Check(gpu.device->CreateBlendState(&visibleBlend,&visibleBlendState),
                "visible sky non-default alpha blend");
            const float visibleFactor[]{0.35f,0.45f,0.55f,0.65f};
            gpu.context->OMSetBlendState(visibleBlendState.Get(),visibleFactor,UINT_MAX);
            D3D11_DEPTH_STENCIL_DESC visibleDepth{};
            visibleDepth.DepthEnable = FALSE;
            visibleDepth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            visibleDepth.DepthFunc = D3D11_COMPARISON_ALWAYS;
            ComPtr<ID3D11DepthStencilState> visibleDepthState;
            Check(gpu.device->CreateDepthStencilState(&visibleDepth,&visibleDepthState),
                "visible sky depth state");
            gpu.context->OMSetDepthStencilState(visibleDepthState.Get(),7);
            std::vector<float> reference;
            for (UINT frame=0; frame<27; ++frame) {
                gpu.context->ClearRenderTargetView(primaryView.Get(),clear);
                const auto technique = static_cast<Motion::CloudTechnique>(5u+(frame/3u)%3u);
                if (frame%3 != 0) {
                    for (auto* target : opacityTargets)
                        gpu.context->ClearRenderTargetView(target,zero);
                    Require(Geometry::AccumulateOpacity(gpu.context.Get(),layout,technique,
                        opacityFaceSize,opacityTargets,submitOverlap,&draw), "roundtrip cube capture");
                }
                // Deliberately do not rebind native state here: Fallout can
                // elide those setters when its CPU-side cache has not changed.
                submitOverlap(&draw);
                const auto pixels = gpu.Read(primary.Get());
                if (frame == 0) {
                    reference = pixels;
                    Require(std::any_of(pixels.begin(),pixels.end(),[&](float v) { return v != clear[0]; }),
                        "visible sky roundtrip baseline contains actual geometry");
                    Require(std::any_of(pixels.begin(),pixels.end(),[&](float v) { return v == clear[0]; }),
                        "visible sky roundtrip baseline contains preserved background");
                } else {
                    Require(pixels == reference,
                        "visible sky is pixel-identical across OFF and cubemap capture frames");
                }
            }
            std::cout << "Visible sky roundtrip passed: 27 alternating OFF/cube frames, "
                << (vr ? "VR" : "flat") << ", cloud techniques 5/6/7\n";
        }
        // Exercise the actual CSAH publication and draw routing, without
        // reflection targets or a negative-Z draw ever being submitted.
        {
            using namespace csah;
            auto& runtime = cloud_shadows::Runtime::get();
            runtime.onDeviceCreated(gpu.device.Get(),gpu.context.Get());
            runtime.applySettings({true,0.55f});
            const linear_lighting::ReplacementShaderBinding binding{
                linear_lighting::ReplacementShaderFamily::sky,5};
            std::array<Float4,61> camera{};camera[59]={10,20,30,0};camera[60]={14,20,30,0};
            auto cameraBuffer=gpu.Constants(camera.data(),sizeof(camera));
            ID3D11ShaderResourceView* cloud{};ID3D11SamplerState* sampler{};float strength{};
            const auto ready=[&] { return runtime.prepareLighting(gpu.context.Get(),cloud,sampler,strength,cameraBuffer.Get()); };
            runtime.beginFrame();runtime.observeMainView(gpu.context.Get());
            Require(!ready(),"no cloud field before capture");
            runtime.capture(gpu.context.Get(),binding,submitOverlap,&draw);
            Require(runtime.snapshot().capturedDraws>0,"native main target captures without reflection cube");
            Require(!ready(),"incomplete frame cannot publish");
            runtime.beginFrame();Require(ready(),"five faces publish the completed cloud frame");
            Require(strength==0.55f && runtime.fieldView(),"published field includes settings and world anchor");
            runtime.beginFrame();Require(!ready(),"cloudless frame withdraws old cloud field");
            runtime.observeMainView(gpu.context.Get());runtime.capture(gpu.context.Get(),binding,submitOverlap,&draw);
            runtime.resetWorld();runtime.beginFrame();Require(!ready(),"world reset cannot publish old capture");
            runtime.observeMainView(gpu.context.Get());runtime.capture(gpu.context.Get(),binding,submitOverlap,&draw);
            gpu.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            runtime.capture(gpu.context.Get(),binding,submitOverlap,&draw);
            runtime.beginFrame();Require(!ready(),"one malformed layer withdraws the entire cube");
            gpu.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            runtime.onDeviceCreated(nullptr,nullptr);
        }
        const UINT callsBeforeReject = draw.calls;
        gpu.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        auto reason = Geometry::RejectReason::kNone;
        Require(!Geometry::AccumulateOpacity(gpu.context.Get(), layout,
            Motion::CloudTechnique::kClouds, opacityFaceSize, opacityTargets,
            submitOverlap, &draw, &reason) && reason == Geometry::RejectReason::kTopology,
            "unsupported geometry remains fail neutral and reports why");
        Require(draw.calls == callsBeforeReject, "rejected capture never submits geometry");
        Geometry::ReleaseDeviceResources();
        gpu.context->ClearState();
        gpu.CheckMessages();
    }

}
void RunFieldTests(GPU& gpu, const std::filesystem::path& root)
{
    auto compile=[&](const std::string& source,const std::filesystem::path& name) {
        ComPtr<ID3DBlob> code,errors;
        auto hr=D3DCompile(source.data(),source.size(),name.string().c_str(),nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_0",D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&code,&errors);
        if(FAILED(hr) && errors) std::cerr<<static_cast<char*>(errors->GetBufferPointer());
        Check(hr,"field test shader compile");ComPtr<ID3D11ComputeShader> shader;
        Check(gpu.device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader),"field test shader");return shader;
    };
    auto update=compile(csah::cloud_shadows::kCloudFieldUpdate,"CloudFieldUpdate");
    std::array<Float4,61> camera{};camera[20][0]=camera[21][1]=camera[22][2]=1;
    camera[59]={-2,0,0,0};camera[60]={2,0,0,0};
    auto cameraCB=gpu.Constants(camera.data(),sizeof(camera));
    Float4 parameters{1,0,10000,4};auto params=gpu.Constants(parameters.data(),sizeof(parameters));
    ComPtr<ID3D11Buffer> field,readback;
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=48;bd.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    Check(gpu.device->CreateBuffer(&bd,nullptr,&field),"field buffer");
    bd.Usage=D3D11_USAGE_STAGING;bd.BindFlags=0;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Check(gpu.device->CreateBuffer(&bd,nullptr,&readback),"field readback");
    ComPtr<ID3D11UnorderedAccessView> fieldOutput;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=3;
    Check(gpu.device->CreateUnorderedAccessView(field.Get(),&ud,&fieldOutput),"field output");
    ComPtr<ID3D11ShaderResourceView> fieldView;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=ud.Format;sd.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;sd.Buffer.NumElements=3;
    Check(gpu.device->CreateShaderResourceView(field.Get(),&sd,&fieldView),"field SRV");
    auto read=[&] {
        gpu.context->CopyResource(readback.Get(),field.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(gpu.context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"read field");std::array<Float4,3> result{};
        std::memcpy(result.data(),mapped.pData,48);gpu.context->Unmap(readback.Get(),0);return result;
    };
    auto dispatch=[&] {
        gpu.context->UpdateSubresource(params.Get(),0,nullptr,parameters.data(),0,0);
        gpu.context->UpdateSubresource(cameraCB.Get(),0,nullptr,camera.data(),0,0);
        auto* cb=params.Get();gpu.context->CSSetConstantBuffers(0,1,&cb);cb=cameraCB.Get();gpu.context->CSSetConstantBuffers(12,1,&cb);
        auto* output=fieldOutput.Get();gpu.context->CSSetUnorderedAccessViews(0,1,&output,nullptr);
        gpu.context->CSSetShader(update.Get(),nullptr,0);gpu.context->Dispatch(1,1,1);
        output=nullptr;gpu.context->CSSetUnorderedAccessViews(0,1,&output,nullptr);
    };
    dispatch();Require(read()[0]==Float4{0,0,0,1},"field anchored at actual stereo midpoint");
    parameters={0,1,10000,4};camera[59][0]+=100;camera[60][0]+=100;dispatch();
    Require(read()[0][0]==0,"walking does not drag the cloud shadow field");
    camera[59][0]+=16000;camera[60][0]+=16000;dispatch();
    Require(read()[0][0]==16100 && read()[1][0]==0 && read()[2][0]==1,"large travel reanchors without an immediate jump");
    parameters[1]=3;dispatch();Require(std::abs(read()[2][0]-0.5f)<1e-6f,"reanchor crossfade midpoint");
    parameters[1]=5;dispatch();Require(read()[2][0]==0,"reanchor crossfade completes");
    parameters[0]=1;camera[59]={-2,0,0,0};camera[60]={2,0,0,0};dispatch();
    const std::string projection=R"(
cbuffer NativeCamera : register(b12) { float4 Camera[61]; };
cbuffer Test : register(b0) { float4 Position[2];float4 Sun;float4 Controls; };
TextureCube<float> CloudOcclusion : register(t0);
SamplerState CloudSampler : register(s0);
Buffer<float4> CloudField : register(t1);
#include "CloudProjection.hlsli"
RWBuffer<float4> Result : register(u0);
[numthreads(2,1,1)] void main(uint3 id : SV_DispatchThreadID) {
 float3 receiver=CloudWorldVector(Position[id.x].xyz)+Camera[59+id.x].xyz;
 Result[id.x]=CapturedCloudVisibility(receiver,Sun.xyz,Controls.x,10000,446148448);
}
)";
    auto shader=compile(projection,root/"package/Shaders/Community/CloudShadows/Test.hlsl");
    std::array<Float4,4> input{{{2,0,0,0},{-2,0,0,0},{1,0,0.1f,0},{0.75f,0,0,0}}};
    auto testCB=gpu.Constants(input.data(),sizeof(input));
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=td.MipLevels=td.SampleDesc.Count=1;td.ArraySize=6;
    td.Format=DXGI_FORMAT_R32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;td.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
    ComPtr<ID3D11Texture2D> cube;ComPtr<ID3D11ShaderResourceView> cubeView;
    Check(gpu.device->CreateTexture2D(&td,nullptr,&cube),"projection cube");Check(gpu.device->CreateShaderResourceView(cube.Get(),nullptr,&cubeView),"projection cube view");
    const float coverages[]{1,0,0.2f,0.3f,0.5f,0.9f};
    for(unsigned face=0;face<6;++face) gpu.context->UpdateSubresource(cube.Get(),face,nullptr,&coverages[face],4,0);
    auto sampler=gpu.Sampler();auto* sample=sampler.Get();gpu.context->CSSetSamplers(0,1,&sample);
    // Reuse the readback buffer, with a distinct output to avoid a field SRV/UAV hazard.
    bd={};bd.ByteWidth=48;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Buffer> result;ComPtr<ID3D11UnorderedAccessView> resultOutput;
    Check(gpu.device->CreateBuffer(&bd,nullptr,&result),"projection result");Check(gpu.device->CreateUnorderedAccessView(result.Get(),&ud,&resultOutput),"projection output");
    auto project=[&] {
        gpu.context->UpdateSubresource(testCB.Get(),0,nullptr,input.data(),0,0);
        gpu.context->UpdateSubresource(cameraCB.Get(),0,nullptr,camera.data(),0,0);
        auto* cb=testCB.Get();gpu.context->CSSetConstantBuffers(0,1,&cb);
        ID3D11ShaderResourceView* views[]{cubeView.Get(),fieldView.Get()};gpu.context->CSSetShaderResources(0,2,views);
        auto* output=resultOutput.Get();gpu.context->CSSetUnorderedAccessViews(0,1,&output,nullptr);
        gpu.context->CSSetShader(shader.Get(),nullptr,0);gpu.context->Dispatch(1,1,1);
        output=nullptr;gpu.context->CSSetUnorderedAccessViews(0,1,&output,nullptr);
        gpu.context->CopyResource(readback.Get(),result.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(gpu.context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"projection readback");std::array<Float4,2> value{};
        std::memcpy(value.data(),mapped.pData,sizeof(value));gpu.context->Unmap(readback.Get(),0);return value;
    };
    auto expected=project();Require(expected[0][0]==0.25f && expected[1][0]==0.25f,"both eyes sample the same world cloud");
    camera[20]={0,-1,0,0};camera[21]={1,0,0,0};input[0]={0,-2,0,0};input[1]={0,2,0,0};input[2]={0,-1,0.1f,0};
    Require(project()==expected,"head rotation cannot rotate the shadow field");
    camera[59][0]+=1000;camera[60][0]+=1000;input[0][1]+=1000;input[1][1]+=1000;
    Require(project()==expected,"world-fixed receiver survives camera translation");
    input[2][2]=-0.1f;Require(project()[0][0]==1,"below-horizon light is neutral");
    input[2][2]=0.1f;input[3][0]=0;Require(project()[0][0]==1,"zero strength is neutral");
    input[3][0]=1;const float zero=0;for(unsigned face=0;face<6;++face) gpu.context->UpdateSubresource(cube.Get(),face,nullptr,&zero,4,0);
    Require(project()[0][0]==1,"clear sky is exactly neutral");
    gpu.context->ClearState();gpu.CheckMessages();
    std::cout<<"GPU world anchoring, reanchor crossfade, head rotation, stereo and clear-sky neutrality passed\n";
}

int main(int argc,char** argv) {
    try {
        Require(argc==2,"repository path required");
        static_assert(Motion::opacityReplayInstanceCount(0,0)==0);
        static_assert(Motion::opacityReplayInstanceCount(1,0)==1);
        static_assert(Motion::opacityReplayInstanceCount(2,0)==1);
        static_assert(Motion::opacityReplayInstanceCount(2,1)==2);
        for (bool hardware : {false,true}) {
            GPU gpu(hardware);
            RunFieldTests(gpu,argv[1]);
            RunGeometryCaptureTests(gpu, Motion::SkyConstantLayout::kVr);
            RunGeometryCaptureTests(gpu, Motion::SkyConstantLayout::kVr,0.0f);
        }
        std::cout << "Cloud opacity, blend/fade/lerp, stereo duplicates and graphics-state restoration passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
