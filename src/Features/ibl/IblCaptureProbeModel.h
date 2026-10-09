#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace csah::ibl
{
    struct CaptureProbeContract
    {
        std::size_t bytecodeSize{};
        std::array<std::uint8_t, 16> checksum{};
    };

    struct CaptureProbeShaderBinding
    {
        bool isDFComposite{};
        std::uint16_t environmentContractPlusOne{};
        bool tiledDiffuse{};
        bool deferredDiffuse{};
    };

    struct CaptureProbePassState
    {
        bool inDFComposite{};
        std::uint16_t lastEnvironmentContractPlusOne{};
    };

    namespace detail
    {
        [[nodiscard]] consteval std::uint8_t hexNibble(char value)
        {
            if (value >= '0' && value <= '9') {
                return static_cast<std::uint8_t>(value - '0');
            }
            if (value >= 'a' && value <= 'f') {
                return static_cast<std::uint8_t>(value - 'a' + 10);
            }
            if (value >= 'A' && value <= 'F') {
                return static_cast<std::uint8_t>(value - 'A' + 10);
            }
            return 0xFF;
        }

        template <std::size_t N>
        [[nodiscard]] consteval std::array<std::uint8_t, 16> dxbcChecksum(
            const char (&text)[N])
        {
            static_assert(N == 33, "DXBC checksum must contain 32 hex digits");
            std::array<std::uint8_t, 16> result{};
            for (std::size_t index = 0; index < result.size(); ++index) {
                const auto high = hexNibble(text[index * 2]);
                const auto low = hexNibble(text[index * 2 + 1]);
                if (high > 0x0F || low > 0x0F) {
                    throw "DXBC checksum contains a non-hex character";
                }
                result[index] = static_cast<std::uint8_t>((high << 4) | low);
            }
            return result;
        }
    }

    // All unique TextureCubeArray-t8 DFComposite PS identities from the
    // locally authoritative Shaders012_VR.fxp. These are the exact existing
    // environment consumers. The build verifier checks identity,
    // declarations, aliases, and complete 41-identity/83-record coverage
    // against the local FXP.
    inline constexpr std::array<CaptureProbeContract, 41>
        kCaptureProbeContracts{ {
            { 11104, detail::dxbcChecksum("05f5cabf7edcaaf695805ffb328ad36d") },
            { 11208, detail::dxbcChecksum("05fa298224cd5b2e250b7a7521131fba") },
            { 3424, detail::dxbcChecksum("127c7bb7c39363bfb67e02cbeb310818") },
            { 5204, detail::dxbcChecksum("12876af2f385b41c0ddd4558b859f5f7") },
            { 5096, detail::dxbcChecksum("182696bc27b5ed3394b950dcc15266af") },
            { 10888, detail::dxbcChecksum("237ceacb0120711a605b69c1bf788b3b") },
            { 9252, detail::dxbcChecksum("33ca4f05dc8bbd98e96859113bea32c9") },
            { 5016, detail::dxbcChecksum("3843512b6ec9938745dda58b4cb7de15") },
            { 9464, detail::dxbcChecksum("3b4ace2ce4b13a967fa38dc8580e99a5") },
            { 3588, detail::dxbcChecksum("3c3cc20b57a82e9f490fdffbec68d273") },
            { 5320, detail::dxbcChecksum("3ec58c35949bf97ae2377b909d61e7f7") },
            { 5124, detail::dxbcChecksum("4c76cbf23925bac1b6bcf47508f6644d") },
            { 9564, detail::dxbcChecksum("4d6870ba7d5498e729b3195f7dcdf978") },
            { 5312, detail::dxbcChecksum("4eff5e0df8329d29e1721897f428d237") },
            { 5408, detail::dxbcChecksum("5a125823477142e0fdc16714247367dd") },
            { 3272, detail::dxbcChecksum("618ab40ad839b752b54678586d987365") },
            { 3304, detail::dxbcChecksum("67fc05f4bebd9fff8a81e6f64fe08b29") },
            { 5160, detail::dxbcChecksum("69644eba8219584adad6054886da80a0") },
            { 5192, detail::dxbcChecksum("83191af2685bcd2c24e3a546eb62a132") },
            { 9348, detail::dxbcChecksum("93edc6af41cbb2d290962e995a4fce25") },
            { 3372, detail::dxbcChecksum("93f4d80a489c3c1f9fb8457bbbb6da4b") },
            { 10992, detail::dxbcChecksum("a1c51753f0ccd83f96990dcd0940a536") },
            { 10780, detail::dxbcChecksum("a2f0b6c3c1d83fbc750048fbe30f062a") },
            { 5204, detail::dxbcChecksum("a5f7c7bd0cba2f606c45c253eb7611e9") },
            { 10996, detail::dxbcChecksum("a8327fffef3f9dd21f6669f912185203") },
            { 3520, detail::dxbcChecksum("aa07c7869032eb3a4bd1bd1646efda22") },
            { 3392, detail::dxbcChecksum("af6880283597f0a13f3a390edd684848") },
            { 3492, detail::dxbcChecksum("b4314b330c8d12a9f08542b8a5c022e9") },
            { 3384, detail::dxbcChecksum("bcd6111c0bad9e847797a6d031108aa8") },
            { 5300, detail::dxbcChecksum("bf35d56c338554073f37c94a66305252") },
            { 9248, detail::dxbcChecksum("c22cdaf9eba9124b3b7ef28e5013744b") },
            { 3488, detail::dxbcChecksum("c72125641444c8ac71ae923b3567954b") },
            { 9136, detail::dxbcChecksum("c80f14ce794c85a162db770bcb180099") },
            { 3316, detail::dxbcChecksum("ccb5c1700aca810b0cd03e080c3623bb") },
            { 3284, detail::dxbcChecksum("d2ea118dbb2a5fe0d2009f25cb272343") },
            { 5084, detail::dxbcChecksum("d4640e2752ee85c9c5872b4d8d7c04ed") },
            { 9036, detail::dxbcChecksum("e633fa0d0aab32daa3e9f28588c9e7bc") },
            { 9352, detail::dxbcChecksum("e7b1971482313136cd85d46a28d18186") },
            { 11100, detail::dxbcChecksum("eb839491ab08ee92fc485990945bbec5") },
            { 5104, detail::dxbcChecksum("f77db52d3d4d4a3a3ab0b02d4ea5ffa0") },
            { 11316, detail::dxbcChecksum("fc24da7bbdad0360e221fc9e45ad2e9f") },
        } };

    // All remaining DFComposite PS identities. Combining this set with the
    // exact environment consumers above covers the complete local family,
    // which prevents a non-environment DFComposite bind from being mistaken
    // for the end of the accumulated composite output.
    inline constexpr std::array<CaptureProbeContract, 38>
        kDFCompositeBoundaryContracts{ {
            { 1016, detail::dxbcChecksum("01f4f91dadaee37c9956c6cb82f1d49f") },
            { 8368, detail::dxbcChecksum("08b32e6bb789aab880b7a99071b6ff01") },
            { 3864, detail::dxbcChecksum("0e3b64ce3e6d19eb513d5261a5f4412d") },
            { 908, detail::dxbcChecksum("0e4b5ea195f97c5127bdb320903ec9e3") },
            { 400, detail::dxbcChecksum("1274cffd2681838ef86f2c775c9cc544") },
            { 1012, detail::dxbcChecksum("1288baa066e4b56df9423609cb5c5b56") },
            { 808, detail::dxbcChecksum("136d884809b2a603d453ab73fa1f1979") },
            { 3316, detail::dxbcChecksum("1678e3285c5602d459c27856b0cdd1ba") },
            { 3208, detail::dxbcChecksum("1fa207b7274c82bbf35b9a953865daac") },
            { 3972, detail::dxbcChecksum("29bc8dee3fa1fc5014cebbb33212a66d") },
            { 3864, detail::dxbcChecksum("2afbbcc558db8455495304d0bd49b1ea") },
            { 3756, detail::dxbcChecksum("2e4654a53a0c51f4c1bdb69039e628e7") },
            { 3952, detail::dxbcChecksum("380e3c6ace47c74f8c567bb0d528a390") },
            { 1024, detail::dxbcChecksum("42f18292dd49c114b77b7eef44e38607") },
            { 432, detail::dxbcChecksum("45d11db07ce0c152e4fc3d32089f61fb") },
            { 3736, detail::dxbcChecksum("4bd40f43863fb7a5f83a80249efd734a") },
            { 924, detail::dxbcChecksum("4f1cc66fbdf115e4673124ad3d63b375") },
            { 3756, detail::dxbcChecksum("526bb2bf9a377fc0b66a9c225ad4a3b4") },
            { 3540, detail::dxbcChecksum("618eab2dee6b57ba9b8fa73a98f9250c") },
            { 8664, detail::dxbcChecksum("67f8107ef9d5484d0b47d9bdd3d43b26") },
            { 7140, detail::dxbcChecksum("6b562dc36406c463cfeacaad3dd3451f") },
            { 3648, detail::dxbcChecksum("787e6c05810d79e41d07ec0c5143e1bc") },
            { 5640, detail::dxbcChecksum("8a6fe719c6a409f7b91c6a2d130b2cf6") },
            { 5244, detail::dxbcChecksum("8b86f75eb1ab76f2d3fb8c62d6b1c746") },
            { 3884, detail::dxbcChecksum("949730ee9bc59032d1cfce7e57f28a80") },
            { 3580, detail::dxbcChecksum("997fb519a37091cfb824f0e7850d0a21") },
            { 3472, detail::dxbcChecksum("9f5debda14fb9bc80f84cfaf9c6fbd86") },
            { 6844, detail::dxbcChecksum("a93bbf6c3d4e04918aa8c2566e11c482") },
            { 3648, detail::dxbcChecksum("b1512a7d974e5e70863fe919ae2b8d8b") },
            { 3668, detail::dxbcChecksum("b5f32ed3adc53a054ffb873c89fed4a9") },
            { 1140, detail::dxbcChecksum("bd1041b7366eac7999fb055d4ecd3726") },
            { 3628, detail::dxbcChecksum("c553e1abd5417c4275ce20b012559813") },
            { 3844, detail::dxbcChecksum("c6b4649b1670f8d6cf85348183133e3c") },
            { 1228, detail::dxbcChecksum("c885b3af623b2defabf2deb7af7db771") },
            { 5656, detail::dxbcChecksum("cb49f4ffda1fdd6ad4b8803345eb5af1") },
            { 1124, detail::dxbcChecksum("d932dcf0a7e5d1afd05a51aec61bb3c7") },
            { 800, detail::dxbcChecksum("efa8c560e817b48e5d07ae36cedde01b") },
            { 5228, detail::dxbcChecksum("f316ca0be6a6dfe7cb678ae3aa8cd2c2") },
        } };

    template <std::size_t N>
    [[nodiscard]] inline bool matchesCaptureProbeContractSet(
        const void* bytecode,
        std::size_t bytecodeSize,
        const std::array<CaptureProbeContract, N>& contracts,
        std::uint16_t* contractPlusOne = nullptr) noexcept
    {
        if (!bytecode || bytecodeSize < 20) {
            return false;
        }
        const auto* bytes = static_cast<const std::uint8_t*>(bytecode);
        if (bytes[0] != 'D' || bytes[1] != 'X' || bytes[2] != 'B' ||
            bytes[3] != 'C') {
            return false;
        }
        for (std::size_t contractIndex = 0;
             contractIndex < contracts.size();
             ++contractIndex) {
            const auto& contract = contracts[contractIndex];
            if (contract.bytecodeSize != bytecodeSize) {
                continue;
            }
            bool matches = true;
            for (std::size_t checksumIndex = 0;
                 checksumIndex < contract.checksum.size();
                 ++checksumIndex) {
                if (bytes[4 + checksumIndex] !=
                    contract.checksum[checksumIndex]) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                if (contractPlusOne) {
                    *contractPlusOne = static_cast<std::uint16_t>(
                        contractIndex + 1);
                }
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] inline std::uint16_t matchCaptureProbeContract(
        const void* bytecode,
        std::size_t bytecodeSize) noexcept
    {
        std::uint16_t contractPlusOne{};
        if (!matchesCaptureProbeContractSet(
                bytecode,
                bytecodeSize,
                kCaptureProbeContracts,
                &contractPlusOne)) {
            return 0;
        }
        return contractPlusOne;
    }

    [[nodiscard]] inline CaptureProbeShaderBinding
    classifyCaptureProbeShader(
        const void* bytecode,
        std::size_t bytecodeSize) noexcept
    {
        const auto environmentContractPlusOne = matchCaptureProbeContract(
            bytecode,
            bytecodeSize);
        if (environmentContractPlusOne != 0) {
            return { true, environmentContractPlusOne };
        }
        return {
            matchesCaptureProbeContractSet(
                bytecode,
                bytecodeSize,
                kDFCompositeBoundaryContracts),
            0
        };
    }

    [[nodiscard]] constexpr bool shouldCaptureCompletedProbePass(
        CaptureProbePassState previous,
        CaptureProbeShaderBinding next) noexcept
    {
        return previous.inDFComposite && !next.isDFComposite &&
            previous.lastEnvironmentContractPlusOne != 0;
    }

    [[nodiscard]] constexpr CaptureProbePassState advanceCaptureProbePass(
        CaptureProbePassState previous,
        CaptureProbeShaderBinding next) noexcept
    {
        if (!next.isDFComposite) {
            return {};
        }
        return {
            true,
            next.environmentContractPlusOne != 0 ?
                next.environmentContractPlusOne :
                (previous.inDFComposite ?
                    previous.lastEnvironmentContractPlusOne :
                    std::uint16_t{})
        };
    }
}
