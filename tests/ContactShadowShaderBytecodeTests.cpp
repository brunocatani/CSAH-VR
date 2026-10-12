#include <Windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;

    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    std::vector<char> readBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        require(stream.good(), "could not open " + path.string());
        return {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>(),
        };
    }
}

int main(int argumentCount, char** arguments)
{
    try {
        require(argumentCount == 3, "expected generated pixel and compute shaders");
        const auto path = std::filesystem::path(arguments[1]);
        const auto bytes = readBytes(path);
        const auto computePath = std::filesystem::path(arguments[2]);
        const auto computeBytes = readBytes(computePath);
        require(
            bytes.size() >= 20 && bytes[0] == 'D' && bytes[1] == 'X' &&
                bytes[2] == 'B' && bytes[3] == 'C',
            "generated Contact Shadows shader is not DXBC");

        ComPtr<ID3D11ShaderReflection> reflection;
        require(
            SUCCEEDED(D3DReflect(
                bytes.data(),
                bytes.size(),
                __uuidof(ID3D11ShaderReflection),
                reinterpret_cast<void**>(reflection.GetAddressOf()))) &&
                reflection,
            "D3DReflect rejected generated Contact Shadows shader");
        D3D11_SHADER_DESC shaderDescription{};
        require(
            SUCCEEDED(reflection->GetDesc(&shaderDescription)),
            "could not inspect generated Contact Shadows shader");
        require(
            shaderDescription.OutputParameters == 2,
            "directional DFLight must retain both render-target outputs");
        ComPtr<ID3DBlob> disassembly;
        require(
            SUCCEEDED(D3DDisassemble(
                bytes.data(),
                bytes.size(),
                0,
                nullptr,
                &disassembly)) &&
                disassembly,
            "D3DDisassemble rejected generated Contact Shadows shader");
        const std::string assembly(
            static_cast<const char*>(disassembly->GetBufferPointer()),
            disassembly->GetBufferSize());
        require(
            assembly.contains(
                "dcl_constantbuffer CB13[3], immediateIndexed"),
            "Contact Shadows settings buffer must remain at b13");
        require(
            assembly.contains(
                "dcl_resource_texture2d (float,float,float,float) t46"),
            "Contact Shadows mask input must remain at t46");
        require(
            !assembly.contains("dcl_constantbuffer CB11[") &&
                !assembly.contains("dcl_constantbuffer CB10["),
            "removed grass and hair feature buffers must not be present");
        require(
            assembly.contains(
                "dcl_constantbuffer CB7[4], immediateIndexed"),
            "PBR settings buffer must remain at private b7");
        require(
            assembly.contains(
                "dcl_resource_texture2d (float,float,float,float) t47"),
            "surface-classification input must remain at t47");
        require(
            std::regex_search(
                assembly,
                std::regex(R"(mul r1\.xyz, r1\.xyzx, r[0-9]+\.[xyzw])")) &&
                std::regex_search(
                    assembly,
                    std::regex(R"(mul r0\.xyz, r0\.xyzx, r[0-9]+\.[xyzw])")),
            "visibility must modulate both directional DFLight outputs");
        std::istringstream lines(assembly);
        std::string line;
        std::size_t returnCount = 0;
        while (std::getline(lines, line)) {
            const auto first = line.find_first_not_of(" \t\r");
            const auto last = line.find_last_not_of(" \t\r");
            if (first != std::string::npos &&
                line.substr(first, last - first + 1) == "ret") {
                ++returnCount;
            }
        }
        require(
            returnCount == 1,
            "generated shader must retain exactly one final return");

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL selected{};
        constexpr D3D_FEATURE_LEVEL requested{ D3D_FEATURE_LEVEL_11_0 };
        require(
            SUCCEEDED(D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                &requested,
                1,
                D3D11_SDK_VERSION,
                &device,
                &selected,
                &context)),
            "D3D11CreateDevice(WARP)");
        ComPtr<ID3D11PixelShader> shader;
        require(
            selected == D3D_FEATURE_LEVEL_11_0 &&
                SUCCEEDED(device->CreatePixelShader(
                    bytes.data(),
                    bytes.size(),
                    nullptr,
                    &shader)) &&
                shader,
            "WARP rejected generated Contact Shadows DFLight shader");
        require(
            computeBytes.size() >= 20 && computeBytes[0] == 'D' &&
                computeBytes[1] == 'X' && computeBytes[2] == 'B' &&
                computeBytes[3] == 'C',
            "generated Contact Shadows mask shader is not DXBC");
        ComPtr<ID3D11ShaderReflection> computeReflection;
        require(
            SUCCEEDED(D3DReflect(
                computeBytes.data(),
                computeBytes.size(),
                __uuidof(ID3D11ShaderReflection),
                reinterpret_cast<void**>(
                    computeReflection.GetAddressOf()))) &&
                computeReflection,
            "D3DReflect rejected Contact Shadows mask compute shader");
        D3D11_SHADER_DESC computeDescription{};
        require(
            SUCCEEDED(computeReflection->GetDesc(&computeDescription)) &&
                computeDescription.Version != 0,
            "could not inspect Contact Shadows mask compute shader");
        UINT threadWidth{};
        UINT threadHeight{};
        UINT threadDepth{};
        computeReflection->GetThreadGroupSize(
            &threadWidth,
            &threadHeight,
            &threadDepth);
        require(
            threadWidth == 64 && threadHeight == 1 && threadDepth == 1,
            "Contact Shadows wavefront mask group must remain 64x1x1");
        ComPtr<ID3D11ComputeShader> computeShader;
        require(
            SUCCEEDED(device->CreateComputeShader(
                computeBytes.data(),
                computeBytes.size(),
                nullptr,
                &computeShader)) &&
                computeShader,
            "WARP rejected generated Contact Shadows mask compute shader");

        std::cout
            << "FO4VR Contact Shadows mask and DFLight bytecode accepted by reflection, disassembly, and WARP.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
