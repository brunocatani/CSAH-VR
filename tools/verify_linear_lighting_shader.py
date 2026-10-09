from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from generate_complex_environment_contracts import build_inventory


EXPECTED_FRAME_FIELDS = [
    ("enableLinearLighting", 0),
    ("isDirLightLinear", 4),
    ("dirLightMult", 8),
    ("lightGamma", 12),
    ("colorGamma", 16),
    ("emitColorGamma", 20),
    ("glowmapGamma", 24),
    ("ambientGamma", 28),
    ("fogGamma", 32),
    ("fogAlphaGamma", 36),
    ("effectGamma", 40),
    ("effectAlphaGamma", 44),
    ("skyGamma", 48),
    ("waterGamma", 52),
    ("vlGamma", 56),
    ("vanillaDiffuseColorMult", 60),
    ("directionalLightMult", 64),
    ("pointLightMult", 68),
    ("ambientMult", 72),
    ("emitColorMult", 76),
    ("glowmapMult", 80),
    ("effectLightingMult", 84),
    ("membraneEffectMult", 88),
    ("bloodEffectMult", 92),
    ("projectedEffectMult", 96),
    ("deferredEffectMult", 100),
    ("otherEffectMult", 104),
    ("linearLightingPad0", 108),
]


def fail(message: str) -> None:
    raise RuntimeError(message)


def find_fxc() -> Path:
    candidates = [
        Path(r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\fxc.exe"),
        Path(r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x86\fxc.exe"),
    ]
    found = shutil.which("fxc.exe") or shutil.which("fxc")
    if found:
        candidates.insert(0, Path(found))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    fail("fxc.exe was not found")


def run_command(arguments: list[str], label: str) -> None:
    completed = subprocess.run(
        arguments,
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        details = (completed.stderr or completed.stdout).strip()
        fail(f"{label} failed: {details}")


def run_fxc(fxc: Path, arguments: list[str]) -> None:
    run_command([str(fxc), "/nologo", *arguments], "fxc")


def parse_dcl_contract(assembly: str) -> dict[str, object]:
    constant_buffers: dict[int, int] = {}
    samplers: set[int] = set()
    textures: set[int] = set()
    inputs: list[str] = []
    outputs: set[int] = set()
    global_flags = ""
    input_signature: list[tuple[str, int, str, int, str, str, str]] = []

    signature_text = assembly.partition("// Input signature:")[2].partition(
        "// Output signature:"
    )[0]
    for raw_line in signature_text.splitlines():
        if not raw_line.startswith("//"):
            continue
        columns = raw_line[2:].split()
        if (
            len(columns) in (6, 7)
            and columns[1].isdigit()
            and columns[3].isdigit()
        ):
            input_signature.append(
                (
                    columns[0],
                    int(columns[1]),
                    columns[2],
                    int(columns[3]),
                    columns[4],
                    columns[5],
                    columns[6] if len(columns) == 7 else "",
                )
            )

    for raw_line in assembly.splitlines():
        line = raw_line.strip()
        if line.startswith("dcl_globalFlags "):
            global_flags = line
        elif match := re.fullmatch(
            r"dcl_constantbuffer CB(\d+)\[(\d+)\], (?:immediate|dynamic)Indexed",
            line,
        ):
            constant_buffers[int(match.group(1))] = int(match.group(2))
        elif match := re.match(r"dcl_sampler s(\d+)", line):
            samplers.add(int(match.group(1)))
        elif match := re.match(r"dcl_resource_texture\w+.*\st(\d+)", line):
            textures.add(int(match.group(1)))
        elif match := re.match(r"dcl_input[^\s]*\s+(.+)", line):
            inputs.append(match.group(1))
        elif match := re.match(r"dcl_output o(\d+)", line):
            outputs.add(int(match.group(1)))

    return {
        "constant_buffers": constant_buffers,
        "samplers": sorted(samplers),
        "textures": sorted(textures),
        "inputs": inputs,
        "input_signature": input_signature,
        "outputs": sorted(outputs),
        "global_flags": global_flags,
    }


def validate_frame_reflection(assembly: str) -> None:
    frame_start = assembly.find("// cbuffer LinearLightingFrame")
    geometry_start = assembly.find("// cbuffer LinearLightingGeometry")
    if frame_start < 0 or geometry_start <= frame_start:
        fail("Linear Lighting reflection blocks are missing")

    frame_block = assembly[frame_start:geometry_start]
    field_pattern = re.compile(
        r"//\s+(?:uint|float)\s+(\w+);\s+// Offset:\s+(\d+)"
    )
    reflected = [
        (match.group(1), int(match.group(2)))
        for match in field_pattern.finditer(frame_block)
    ]
    if reflected != EXPECTED_FRAME_FIELDS:
        fail(f"LinearLightingFrame layout drifted: {reflected!r}")

    geometry_block = assembly[geometry_start:]
    if not re.search(
        r"float emissiveMult;\s+// Offset:\s+0 Size:\s+4", geometry_block
    ):
        fail("LinearLightingGeometry emissiveMult is not reflected at offset zero")


def load_contracts(root: Path) -> tuple[Path, list[dict[str, object]]]:
    manifest_path = (
        root / "package" / "Shaders" / "Community" / "LinearLightingContracts.json"
    )
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(manifest, list) or len(manifest) != 288:
        fail("Linear Lighting manifest must contain exactly 288 contracts")

    reconstruction = root / "package" / "Shaders" / "Community" / "Reconstruction"
    verified = root / "package" / "Shaders" / "Community" / "VerifiedLinearLighting"
    names: set[str] = set()
    resources: set[str] = set()
    contracts: list[dict[str, object]] = []
    for index, entry in enumerate(manifest):
        if not isinstance(entry, dict):
            fail(f"manifest entry {index} is not an object")
        name = entry.get("name")
        resource = entry.get("resource")
        if not isinstance(name, str) or not name:
            fail(f"manifest entry {index} has an invalid name")
        if not isinstance(resource, str) or not resource.startswith("IDR_"):
            fail(f"manifest entry {index} has an invalid resource symbol")
        if name in names:
            fail(f"duplicate shader contract name: {name}")
        if resource in resources:
            fail(f"duplicate shader contract resource: {resource}")
        names.add(name)
        resources.add(resource)
        contracts.append(
            {
                "label": name,
                "resource": resource,
                "source": reconstruction
                / f"{name}.LinearLightingCandidate.hlsl",
                "packaged": reconstruction
                / f"{name}.LinearLightingCandidate.dxbc",
                "vanilla": verified / f"{name}.dxbc",
            }
        )
    return manifest_path, contracts


def verify_source_contracts(
    root: Path, contracts: list[dict[str, object]], shared: Path
) -> None:
    projected_source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "Reconstruction"
        / "DefaultProjectedFiveMrt_L4_00008002.LinearLightingCandidate.hlsl"
    )
    six_mrt_source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "Reconstruction"
        / "DefaultSixMrt_L4_00000002.LinearLightingCandidate.hlsl"
    )
    base_source_texts = [
        projected_source.read_text(encoding="utf-8"),
        six_mrt_source.read_text(encoding="utf-8"),
    ]
    dismemberment_source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "Reconstruction"
        / "DismembermentTessellatedSixMrt_L4_00280042.LinearLightingCandidate.hlsl"
    )
    dismemberment_source_text = dismemberment_source.read_text(encoding="utf-8")
    for token in (
        "float4 cb2[10]",
        "Texture2D<float4> TexDismembermentDiffuse : register(t9)",
        "Texture2D<float4> TexDismembermentNormal : register(t10)",
        "Texture2D<float4> TexDismembermentSpecular : register(t11)",
        "input.dismembermentSelector.y > 0.0",
        "LINEAR_LIGHTING_DISMEMBERMENT_BASIS_X cb2[5]",
        "LINEAR_LIGHTING_DISMEMBERMENT_BASIS_X cb2[6]",
        "LINEAR_LIGHTING_DISMEMBERMENT_DEPTH cb2[10]",
        "LINEAR_LIGHTING_DISMEMBERMENT_ALPHA_MASK cb2[9]",
        "TexAdditionalAlphaNoise.Load",
        "SampAdditionalAlpha, input.uv",
        "LinearLightingGlowmap(TexGlow.Sample(SampGlow, input.uv).xyz)",
        "TexGradientRemap.SampleLevel",
        "gradientRemapRow += pow(input.vertexColor.x, 0.454545) - 1.0",
        "specularSample.y *= gradientRemap.w",
        "diffuse = gradientRemap.xyz",
        "diffuse = lerp(diffuse, skinTint, cb2[2].w)",
        "const float3 transformedDiffuse = useDismemberment ?",
        "LinearLightingDiffuse(diffuse)",
        "LinearLightingDecodedDiffuse(diffuse)",
        "LinearLightingEmitColor(cb2[1].xyz)",
        "input.eyeIndex * 4u",
    ):
        if token not in dismemberment_source_text:
            fail(f"dismemberment shader is missing verified contract: {token}")
    if "[earlydepthstencil]" in dismemberment_source_text:
        fail("baseline dismemberment shaders must preserve no-early-depth behavior")
    dismemberment_blend_source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "Reconstruction"
        / "DismembermentBlendFiveMrt_L3NoEarlyDepth_00288047.LinearLightingCandidate.hlsl"
    )
    dismemberment_blend_source_text = dismemberment_blend_source.read_text(
        encoding="utf-8"
    )
    for token in (
        "float4 cb2[11]",
        "float4 cb2[12]",
        "float4 cb12[51]",
        "LINEAR_LIGHTING_BLEND_BASIS_X cb2[6]",
        "LINEAR_LIGHTING_BLEND_ALPHA_MASK cb2[10]",
        "LINEAR_LIGHTING_BLEND_DEPTH cb2[11]",
        "baseDiffuseSample.w * input.vertexColor.w",
        "baseDiffuseSample.xyz * input.vertexColor.xyz",
        "TexDismembermentDiffuse.Sample",
        "output.target1.z = -projectedNormal.z",
        "const float3 transformedDiffuse = useDismemberment ?",
        "LinearLightingDiffuse(diffuse)",
        "LinearLightingDecodedDiffuse(diffuse)",
        "LinearLightingEmitColor(cb2[1].xyz)",
    ):
        if token not in dismemberment_blend_source_text:
            fail(
                "dismemberment blend shader is missing verified contract: "
                f"{token}"
            )
    if "[earlydepthstencil]" in dismemberment_blend_source_text:
        fail("dismemberment blend shaders must preserve no-early-depth behavior")
    combined_source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "Reconstruction"
        / "CombinedAlphaTestSixMrt_L4_10000142.LinearLightingCandidate.hlsl"
    )
    combined_source_text = combined_source.read_text(encoding="utf-8")
    for token in (
        "struct CombinedMaterialData",
        "StructuredBuffer<CombinedMaterialData> CombinedMaterials : register(t4)",
        "StructuredBuffer<CombinedDepthData> CombinedDepths : register(t6)",
        "Texture2DArray<float4> TexDiffuse : register(t0)",
        "float3(material.textureSlices.xyz)",
        "CombinedMaterials[input.materialIndex]",
        "LINEAR_LIGHTING_COMBINED_FLAGS cb2[5]",
        "material.interpolationAndProperties.yx",
        "LinearLightingDecodedDiffuse(diffuse)",
        "LinearLightingEmitColor(",
        "input.eyeIndex * 4u",
    ):
        if token not in combined_source_text:
            fail(f"combined-material shader is missing verified contract: {token}")
    if "[earlydepthstencil]" in combined_source_text:
        fail("combined-material shaders must preserve no-early-depth behavior")
    landscape_source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "Reconstruction"
        / "LandscapeFourLayerSixMrt_L3_00000023.LinearLightingCandidate.hlsl"
    )
    landscape_source_text = landscape_source.read_text(encoding="utf-8")
    for token in (
        "struct LandscapeLayerIndices",
        "StructuredBuffer<LandscapeLayerIndices> LandscapeIndices : register(t4)",
        "Texture2DArray<float4> TexDiffuseLayers : register(t0)",
        "Texture2D<float4> TexDiffuse3 : register(t3)",
        "Texture2D<float4> TexNormal3 : register(t7)",
        "Texture2D<float4> TexSpecular3 : register(t11)",
        "AccumulateLandscapeLayer(",
        "lodMultiplier * landscapeLodDiffuse",
        "LinearLightingDecodedDiffuse(diffuse)",
        "LinearLightingEmitColor(cb2[1].xyz)",
        "input.eyeIndex * 4u",
    ):
        if token not in landscape_source_text:
            fail(f"four-layer landscape shader is missing contract: {token}")
    meat_cuff_source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "Reconstruction"
        / "MeatCuffProjectedFiveMrt_L4_00408042.LinearLightingCandidate.hlsl"
    )
    meat_cuff_source_text = meat_cuff_source.read_text(encoding="utf-8")
    for token in (
        "float4 cb2[9]",
        "float4 cb12[51]",
        "Texture2D<float4> TexMeatCuffDiffuse : register(t9)",
        "Texture2D<float4> TexMeatCuffNormal : register(t10)",
        "Texture2D<float4> TexMeatCuffSpecular : register(t11)",
        "input.cuffIndex.x < 0.0 || input.cuffIndex.x > 10.0",
        "LINEAR_LIGHTING_MEAT_CUFF_ORIENTATION_X cb2[6]",
        "LINEAR_LIGHTING_MEAT_CUFF_ORIENTATION_X cb2[7]",
        "LINEAR_LIGHTING_MEAT_CUFF_DEPTH cb2[9]",
        "LINEAR_LIGHTING_MEAT_CUFF_ALPHA_MASK cb2[8]",
        "TexAdditionalAlphaNoise.Load",
        "SampAdditionalAlpha, baseUv",
        "LinearLightingGlowmap(TexGlow.Sample(SampGlow, baseUv).xyz)",
        "TexGradientRemap.SampleLevel",
        "gradientRemapRow += pow(input.vertexColor.x, 0.454545) - 1.0",
        "diffuseSample.xyz = gradientRemap.xyz",
        "specularSample.y *= gradientRemap.w",
        "lerp(diffuseSample.xyz, skinTint, cb2[3].w)",
        "clip(-1.0)",
        "clip(alpha - 0.015686)",
        "LinearLightingDiffuse(diffuseSample.xyz)",
        "LinearLightingEmitColor(cb2[1].xyz)",
        "output.target1.w = alpha",
        "output.target4.w = alpha",
    ):
        if token not in meat_cuff_source_text:
            fail(f"meat-cuff shader is missing verified contract: {token}")
    if "[earlydepthstencil]" in meat_cuff_source_text:
        fail("baseline meat-cuff shaders must preserve no-early-depth behavior")
    shared_text = shared.read_text(encoding="utf-8")
    if any("enableGammaCorrection" in text for text in base_source_texts) or (
        "enableGammaCorrection" in shared_text
    ):
        fail("removed upstream setting enableGammaCorrection returned")
    decoded_diffuse_match = re.search(
        r"float3\s+LinearLightingDecodedDiffuse\s*\(float3\s+color\)\s*"
        r"\{(?P<body>.*?)\}",
        shared_text,
        re.DOTALL,
    )
    if decoded_diffuse_match is None:
        fail("shared shader is missing decoded-diffuse transformation")
    decoded_diffuse_body = decoded_diffuse_match.group("body")
    if "color * vanillaDiffuseColorMult" not in decoded_diffuse_body:
        fail("decoded-diffuse transformation no longer preserves decoded color")
    if "pow(" in decoded_diffuse_body or "abs(" in decoded_diffuse_body:
        fail("decoded-diffuse transformation must not decode color again")

    for base_source_text in base_source_texts:
        for token in (
            "float2 normalSample = TexNormal.Sample(SampNormal, uv).xy;",
        ):
            if token not in base_source_text:
                fail(f"active shader is missing transformation contract: {token}")
    if "LinearLightingDecodedDiffuse(mappedDiffuse)" not in base_source_texts[0]:
        fail("projected shader is missing decoded-diffuse transformation")
    if "LinearLightingDecodedDiffuse(diffuse" not in base_source_texts[1]:
        fail("six-MRT shader is missing decoded-diffuse transformation")
    if "LinearLightingEmitColor(cb2[1].xyz)" not in base_source_texts[0]:
        fail("projected shader is missing emission transformation")
    if "LinearLightingEmitColor(emitColor)" not in base_source_texts[1]:
        fail("six-MRT shader is missing emission transformation")
    for token in (
        "LinearLightingGlowmap(TexGlow.Sample(SampGlow, uv).xyz)",
        "clip(alpha - cb2[1].w)",
        "TexHairDirection.Sample(SampHairDirection, uv).xyz",
        "output.target3.x = dot(normalize(input.tangent), hairDirection)",
        "output.target3.y = dot(normalize(input.bitangent), hairDirection)",
        "output.target3.z = dot(sourceNormal, hairDirection)",
        "output.target3.w = 0.003922;",
        "output.target0.w = gradientRemapSource;",
        "LINEAR_LIGHTING_BONE_TINT_ROW cb2[7]",
        "LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[8]",
    ):
        if token not in base_source_texts[1]:
            fail(f"six-MRT shader is missing semantic contract: {token}")
    if "clip(diffuse.w - cb2[1].w)" not in base_source_texts[0]:
        fail("projected shader no longer preserves alpha-reference testing")
    for token in (
        "TexAdditionalAlphaNoise.Load",
        "#define LINEAR_LIGHTING_ALPHA_MASK_PARAMETERS cb2[5]",
        "#define LINEAR_LIGHTING_ALPHA_MASK_PARAMETERS cb2[6]",
        "clip(LINEAR_LIGHTING_ALPHA_MASK_PARAMETERS.x - additionalAlpha)",
        "LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[6]",
        "LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[7]",
    ):
        if token not in base_source_texts[1]:
            fail(
                "six-MRT shader is missing additional-alpha-mask contract: "
                f"{token}"
            )
    for token in (
        "TexBoneTintLookup.Sample",
        "TexBoneTintPalette.Sample",
        "frac(LINEAR_LIGHTING_PROJECTED_BONE_TINT_ROW.x)",
        "LinearLightingDiffuse(boneTintPalette.xyz)",
        "boneTintPalette.w * boneTintLookup.w * input.boneTintColor.w * 4.0",
        "output.target0.xyz += boneTint",
    ):
        if token not in base_source_texts[0]:
            fail(
                "projected shader is missing bone-tint contract: "
                f"{token}"
            )
    for token in (
        "TexBoneTintLookup.Sample",
        "TexBoneTintPalette.Sample",
        "frac(LINEAR_LIGHTING_BONE_TINT_ROW.x)",
        "LinearLightingDiffuse(boneTintPalette.xyz)",
        "boneTintPalette.w * boneTintLookup.w * input.boneTintColor.w * 4.0",
        "output.target0.xyz += boneTint",
        "#define LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[6]",
    ):
        if token not in base_source_texts[1]:
            fail(
                "six-MRT shader is missing bone-tint contract: "
                f"{token}"
            )
    for token in (
        "TexLandscapeLodDiffuse.Sample",
        "TexLandscapeLodNormal.Sample",
        "diffuse *= landscapeLodDiffuse",
        "normalize(cross(float3(1.0, 0.0, 0.0), detailNormal))",
    ):
        if token not in base_source_texts[1]:
            fail(
                "six-MRT shader is missing landscape-LOD contract: "
                f"{token}"
            )
    for token in (
        "TexLandscapeLodDiffuse.Sample",
        "TexLandscapeLodNormal.Sample",
        "mappedDiffuse *= landscapeLodDiffuse",
        "cross(float3(1.0, 0.0, 0.0), detailNormal)",
    ):
        if token not in base_source_texts[0]:
            fail(
                "projected shader is missing landscape-LOD contract: "
                f"{token}"
            )
    for token in (
        "TexGradientRemap.SampleLevel",
        "pow(gradientRemapSource, 0.454545)",
        "gradientRemapRow += pow(input.vertexColor.x, 0.454545) - 1.0",
        "specularSample.y * gradientRemap.w",
    ):
        if token not in base_source_texts[1]:
            fail(
                "six-MRT shader is missing gradient-remap contract: "
                f"{token}"
            )

    for token in (
        "LinearLightingDiffuse(menuScreen)",
        "(-screenDirection.xy / screenDepth) + (tangentNormal.xy * 2.0)",
        "float2(screenOffset.x, -screenOffset.y) * cb0[0].y",
        "pow(TexScreen.Sample(SampScreen, screenUv).xyz, 2.2)",
        "pipboyScreen * cb0[0].z",
        "pipboyScreen * cb0[0].w",
        "output.target3.w = 0.015686;",
    ):
        if token not in base_source_texts[1]:
            fail(
                "six-MRT shader is missing screen-material contract: "
                f"{token}"
            )
    for token in (
        "TexFaceDetail.Sample(SampFaceDetail, uv)",
        "faceDetail.w * 2.0 - 1.0",
        "float faceDiffuseMask = faceDetail.w",
        "(1.0 - faceDiffuseMask) * input.faceFactor * 0.3",
        "float2 faceNormalXY = (faceDetail.xy * 2.0) - 1.0",
        "lerp(\n        detailModelNormal,\n        faceModelNormal",
        "output.target3.w = 0.019608;",
    ):
        if token not in base_source_texts[1]:
            fail(
                "six-MRT shader is missing face-detail contract: "
                f"{token}"
            )
    for token in (
        "float3 skinTintGamma = pow(abs(cb2[2].xyz), 0.454545)",
        "float3 skinBaseGamma = pow(abs(diffuse), 0.454545)",
        "skinBaseGamma * skinBaseGamma * (1.0 - (2.0 * skinTintGamma))",
        "sqrt(skinBaseGamma) * ((2.0 * skinTintGamma) - 1.0)",
        "(skinTintGamma < 0.5) ? skinTintDark : skinTintLight",
        "float3 tintedDiffuse = pow(abs(skinTintBlend), 2.2)",
        "diffuse = lerp(diffuse, tintedDiffuse, cb2[2].w)",
        "LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK && (LINEAR_LIGHTING_GRADIENT_REMAP || LINEAR_LIGHTING_SKIN_TINT)",
        "#elif LINEAR_LIGHTING_FACE_DETAIL || LINEAR_LIGHTING_SKIN_TINT",
        "output.target3.w = 0.019608;",
    ):
        if token not in base_source_texts[1]:
            fail(
                "six-MRT shader is missing skin-tint contract: "
                f"{token}"
            )
    for token in (
        "LINEAR_LIGHTING_BONE_TINTING && LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK",
        "#define LINEAR_LIGHTING_ALPHA_MASK_PARAMETERS cb2[5]",
        "#define LINEAR_LIGHTING_BONE_TINT_ROW cb2[6]",
        "#define LINEAR_LIGHTING_ALPHA_MASK_PARAMETERS cb2[6]",
        "#define LINEAR_LIGHTING_BONE_TINT_ROW cb2[7]",
        "#define LINEAR_LIGHTING_DEPTH_PARAMETERS cb2[8]",
    ):
        if token not in base_source_texts[1]:
            fail(
                "six-MRT shader is missing combined bone/mask layout: "
                f"{token}"
            )
    for token in (
        "TexGradientRemap.SampleLevel",
        "pow(diffuse.y, 0.454545)",
        "gradientRemapRow += pow(input.vertexColor.x, 0.454545) - 1.0",
        "specularSample.y * gradientRemap.w",
        "mappedDiffuse *= diffuse.y * 1.8",
        "saturate(max(alpha, 0.019608))",
    ):
        if token not in base_source_texts[0]:
            fail(
                "projected shader is missing gradient-remap contract: "
                f"{token}"
            )
    for token in (
        "#if LINEAR_LIGHTING_TESSELLATED_INPUTS",
        "float2 uv = input.uv",
        "float3 skinTintGamma = pow(abs(cb2[3].xyz), 0.454545)",
        "mappedDiffuse = lerp(mappedDiffuse, tintedDiffuse, cb2[3].w)",
        "LinearLightingDiffuse(menuScreen)",
    ):
        if token not in base_source_texts[0]:
            fail(
                "projected shader is missing final singleton contract: "
                f"{token}"
            )
    if "LinearLightingEmitColor(emitColor) * glow" not in base_source_texts[1]:
        fail("six-MRT shader is missing combined Pip-Boy/glow emission")

    untextured_source = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "Reconstruction"
        / "UntexturedSixMrt_L4_00000000.LinearLightingCandidate.hlsl"
    )
    untextured_source_text = untextured_source.read_text(encoding="utf-8")
    for token in (
        "float4 cb2[6]",
        "float4 cb12[71]",
        "asfloat(uint2(0x7FC00000u, 0x7FC00000u))",
        "LinearLightingDecodedDiffuse(float3(1.0, 1.0, 1.0))",
        "LinearLightingEmitColor(cb2[1].xyz)",
        "input.eyeIndex * 4u",
    ):
        if token not in untextured_source_text:
            fail(f"untextured shader is missing verified contract: {token}")
    for token in (
        "TexAdditionalAlphaNoise.Load",
        "#define LINEAR_LIGHTING_PROJECTED_ALPHA_MASK cb2[6]",
        "#define LINEAR_LIGHTING_PROJECTED_ALPHA_MASK cb2[7]",
        "#define LINEAR_LIGHTING_PROJECTED_BONE_TINT_ROW cb2[7]",
        "clip(LINEAR_LIGHTING_PROJECTED_ALPHA_MASK.x - additionalAlpha)",
        "#define LINEAR_LIGHTING_PROJECTED_DEPTH cb2[7]",
        "#define LINEAR_LIGHTING_PROJECTED_DEPTH cb2[8]",
        "float3 modelNormal = (TexNormal.Sample(SampNormal, uv).xyz * 2.0) - 1.0",
        "output.target3.w = pow(alpha, 0.1)",
        "#define LINEAR_LIGHTING_PROJECTED_INTERPOLATION cb2[4]",
        "#define LINEAR_LIGHTING_PROJECTED_PROPERTIES cb2[6]",
        "#define LINEAR_LIGHTING_PROJECTED_DEPTH cb2[7]",
        "output.target0.xyz = float3(0.0, 0.0, 0.0)",
        "LINEAR_LIGHTING_GRADIENT_HAIR || LINEAR_LIGHTING_HAIR",
    ):
        if token not in base_source_texts[0]:
            fail(
                "projected shader is missing additional-alpha-mask contract: "
                f"{token}"
            )

    vertex_contracts = 0
    glowmap_contracts = 0
    instanced_contracts = 0
    model_space_normal_contracts = 0
    tessellated_contracts = 0
    additional_alpha_mask_contracts = 0
    landscape_lod_contracts = 0
    gradient_remap_contracts = 0
    gradient_hair_contracts = 0
    lod_object_alpha_contracts = 0
    bone_tint_contracts = 0
    combined_glowmap_additional_alpha_contracts = 0
    menu_screen_contracts = 0
    pipboy_screen_contracts = 0
    combined_pipboy_additional_alpha_contracts = 0
    face_detail_contracts = 0
    skin_tint_contracts = 0
    combined_skin_tint_additional_alpha_contracts = 0
    combined_gradient_remap_glowmap_contracts = 0
    combined_gradient_remap_glowmap_additional_alpha_contracts = 0
    combined_face_detail_additional_alpha_contracts = 0
    combined_face_detail_bone_tint_contracts = 0
    combined_gradient_remap_bone_tint_contracts = 0
    combined_skin_tint_bone_tint_contracts = 0
    standalone_projected_bone_tint_contracts = 0
    standalone_lod_object_contracts = 0
    standalone_projected_model_space_contracts = 0
    standalone_hair_contracts = 0
    combined_hair_additional_alpha_contracts = 0
    six_mrt_gradient_hair_contracts = 0
    combined_gradient_hair_bone_tint_contracts = 0
    combined_gradient_hair_additional_alpha_contracts = 0
    combined_gradient_hair_additional_alpha_bone_tint_contracts = 0
    dismemberment_contracts = 0
    meat_cuff_contracts = 0
    combined_material_contracts = 0
    landscape_layer_contracts = 0
    instanced_landscape_layer_contracts = 0
    for contract in contracts:
        source = contract["source"]
        assert isinstance(source, Path)
        source_text = source.read_text(encoding="utf-8")
        if "#define LINEAR_LIGHTING_VERTEX_COLOR 1" in source_text:
            vertex_contracts += 1
        if "#define LINEAR_LIGHTING_TEXTURED_EMISSION 1" in source_text:
            glowmap_contracts += 1
        if "#define LINEAR_LIGHTING_INSTANCED 1" in source_text:
            instanced_contracts += 1
        if "#define LINEAR_LIGHTING_MODEL_SPACE_NORMALS 1" in source_text:
            model_space_normal_contracts += 1
        if "#define LINEAR_LIGHTING_TESSELLATED_INPUTS 1" in source_text:
            tessellated_contracts += 1
        if "#define LINEAR_LIGHTING_DISMEMBERMENT 1" in source_text:
            dismemberment_contracts += 1
        if "#define LINEAR_LIGHTING_MEAT_CUFF 1" in source_text:
            meat_cuff_contracts += 1
        if "Combined" in str(contract["label"]):
            combined_material_contracts += 1
        if "LandscapeFourLayer" in str(contract["label"]):
            landscape_layer_contracts += 1
            if "#define LINEAR_LIGHTING_INSTANCED_LANDSCAPE 1" in source_text:
                instanced_landscape_layer_contracts += 1
        if "#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 1" in source_text:
            additional_alpha_mask_contracts += 1
        if "#define LINEAR_LIGHTING_LANDSCAPE_LOD 1" in source_text:
            landscape_lod_contracts += 1
        if "#define LINEAR_LIGHTING_GRADIENT_REMAP 1" in source_text:
            gradient_remap_contracts += 1
        if "#define LINEAR_LIGHTING_GRADIENT_HAIR 1" in source_text:
            gradient_hair_contracts += 1
        if "#define LINEAR_LIGHTING_LOD_OBJECT_ALPHA 1" in source_text:
            lod_object_alpha_contracts += 1
        if "#define LINEAR_LIGHTING_BONE_TINTING 1" in source_text:
            bone_tint_contracts += 1
        if "#define LINEAR_LIGHTING_HAIR 1" in source_text:
            standalone_hair_contracts += 1
            if "#define LINEAR_LIGHTING_TEXTURED_EMISSION 1" in source_text:
                fail("hair-direction contracts cannot use textured emission")
        if (
            "#define LINEAR_LIGHTING_GRADIENT_HAIR 1" in source_text
            and '"DefaultSixMrt_L4_00000002.LinearLightingCandidate.hlsl"'
            in source_text
        ):
            six_mrt_gradient_hair_contracts += 1
        if (
            "#define LINEAR_LIGHTING_GRADIENT_HAIR 1" in source_text
            and "#define LINEAR_LIGHTING_HAIR 1" in source_text
            and "#define LINEAR_LIGHTING_BONE_TINTING 1" in source_text
        ):
            combined_gradient_hair_bone_tint_contracts += 1
        if (
            "#define LINEAR_LIGHTING_GRADIENT_HAIR 1" in source_text
            and "#define LINEAR_LIGHTING_HAIR 1" in source_text
            and "#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 1" in source_text
        ):
            combined_gradient_hair_additional_alpha_contracts += 1
            if "#define LINEAR_LIGHTING_BONE_TINTING 1" in source_text:
                combined_gradient_hair_additional_alpha_bone_tint_contracts += 1
        if (
            "#define LINEAR_LIGHTING_HAIR 1" in source_text
            and "#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 1" in source_text
        ):
            combined_hair_additional_alpha_contracts += 1
        if (
            "#define LINEAR_LIGHTING_TEXTURED_EMISSION 1" in source_text
            and "#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 1" in source_text
        ):
            combined_glowmap_additional_alpha_contracts += 1
        if "#define LINEAR_LIGHTING_MENU_SCREEN 1" in source_text:
            menu_screen_contracts += 1
        if "#define LINEAR_LIGHTING_PIPBOY_SCREEN 1" in source_text:
            pipboy_screen_contracts += 1
            if "#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 1" in source_text:
                combined_pipboy_additional_alpha_contracts += 1
        if "#define LINEAR_LIGHTING_FACE_DETAIL 1" in source_text:
            face_detail_contracts += 1
        if "#define LINEAR_LIGHTING_SKIN_TINT 1" in source_text:
            skin_tint_contracts += 1
        if (
            "#define LINEAR_LIGHTING_SKIN_TINT 1" in source_text
            and "#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 1" in source_text
        ):
            combined_skin_tint_additional_alpha_contracts += 1
        if (
            "#define LINEAR_LIGHTING_GRADIENT_REMAP 1" in source_text
            and "#define LINEAR_LIGHTING_TEXTURED_EMISSION 1" in source_text
        ):
            combined_gradient_remap_glowmap_contracts += 1
            if "#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 1" in source_text:
                combined_gradient_remap_glowmap_additional_alpha_contracts += 1
        if (
            "#define LINEAR_LIGHTING_FACE_DETAIL 1" in source_text
            and "#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 1" in source_text
        ):
            combined_face_detail_additional_alpha_contracts += 1
        if (
            "#define LINEAR_LIGHTING_FACE_DETAIL 1" in source_text
            and "#define LINEAR_LIGHTING_BONE_TINTING 1" in source_text
        ):
            combined_face_detail_bone_tint_contracts += 1
        if (
            "#define LINEAR_LIGHTING_GRADIENT_REMAP 1" in source_text
            and "#define LINEAR_LIGHTING_BONE_TINTING 1" in source_text
        ):
            combined_gradient_remap_bone_tint_contracts += 1
        if (
            "#define LINEAR_LIGHTING_SKIN_TINT 1" in source_text
            and "#define LINEAR_LIGHTING_BONE_TINTING 1" in source_text
        ):
            combined_skin_tint_bone_tint_contracts += 1
        if (
            "#define LINEAR_LIGHTING_BONE_TINTING 1" in source_text
            and "#define LINEAR_LIGHTING_GRADIENT_REMAP 1" not in source_text
            and '"DefaultProjectedFiveMrt_L4_00008002.LinearLightingCandidate.hlsl"'
            in source_text
        ):
            standalone_projected_bone_tint_contracts += 1
        if (
            "#define LINEAR_LIGHTING_LOD_OBJECT_ALPHA 1" in source_text
            and "#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 1" not in source_text
            and "#define LINEAR_LIGHTING_BONE_TINTING 1" not in source_text
        ):
            if "#define LINEAR_LIGHTING_NORMAL_XY 0" in source_text:
                fail(
                    "standalone projected LOD-object contracts must sample "
                    "the verified normal-map XY channels"
                )
            standalone_lod_object_contracts += 1
        if (
            "#define LINEAR_LIGHTING_MODEL_SPACE_NORMALS 1" in source_text
            and "#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 1" not in source_text
            and '"DefaultProjectedFiveMrt_L4_00008002.LinearLightingCandidate.hlsl"'
            in source_text
        ):
            standalone_projected_model_space_contracts += 1
    if vertex_contracts != 147:
        fail(f"expected 147 COLOR0 contracts, found {vertex_contracts}")
    if glowmap_contracts != 42:
        fail(f"expected 42 glowmap contracts, found {glowmap_contracts}")
    if instanced_contracts != 8:
        fail(f"expected 8 instanced contracts, found {instanced_contracts}")
    if model_space_normal_contracts != 28:
        fail(
            "expected 28 model-space-normal contracts, "
            f"found {model_space_normal_contracts}"
        )
    if tessellated_contracts != 38:
        fail(f"expected 38 tessellated contracts, found {tessellated_contracts}")
    if dismemberment_contracts != 22:
        fail(
            f"expected 22 dismemberment contracts, found {dismemberment_contracts}"
        )
    if meat_cuff_contracts != 20:
        fail(f"expected 20 meat-cuff contracts, found {meat_cuff_contracts}")
    if additional_alpha_mask_contracts != 85:
        fail(
            "expected 85 additional-alpha-mask contracts, "
            f"found {additional_alpha_mask_contracts}"
        )
    if landscape_lod_contracts != 14:
        fail(
            "expected 14 landscape-LOD contracts, "
            f"found {landscape_lod_contracts}"
        )
    if gradient_remap_contracts != 83:
        fail(
            "expected 83 gradient-remap contracts, "
            f"found {gradient_remap_contracts}"
        )
    if combined_material_contracts != 7:
        fail(
            "expected 7 combined-material contracts, "
            f"found {combined_material_contracts}"
        )
    if landscape_layer_contracts != 3:
        fail(
            "expected 3 four-layer landscape contracts, "
            f"found {landscape_layer_contracts}"
        )
    if instanced_landscape_layer_contracts != 1:
        fail(
            "expected 1 instanced four-layer landscape contract, "
            f"found {instanced_landscape_layer_contracts}"
        )
    if gradient_hair_contracts != 27:
        fail(
            "expected 27 gradient-hair contracts, "
            f"found {gradient_hair_contracts}"
        )
    if lod_object_alpha_contracts != 9:
        fail(
            "expected 9 projected LOD-object-alpha contracts, "
            f"found {lod_object_alpha_contracts}"
        )
    if bone_tint_contracts != 44:
        fail(f"expected 44 bone-tint contracts, found {bone_tint_contracts}")
    if combined_glowmap_additional_alpha_contracts != 14:
        fail(
            "expected 14 combined glowmap/additional-alpha contracts, "
            f"found {combined_glowmap_additional_alpha_contracts}"
        )
    if menu_screen_contracts != 9:
        fail(f"expected 9 menu-screen contracts, found {menu_screen_contracts}")
    if pipboy_screen_contracts != 5:
        fail(
            f"expected 5 Pip-Boy-screen contracts, found {pipboy_screen_contracts}"
        )
    if combined_pipboy_additional_alpha_contracts != 2:
        fail(
            "expected 2 combined Pip-Boy/additional-alpha contracts, "
            f"found {combined_pipboy_additional_alpha_contracts}"
        )
    if face_detail_contracts != 19:
        fail(f"expected 19 face-detail contracts, found {face_detail_contracts}")
    if skin_tint_contracts != 29:
        fail(f"expected 29 skin-tint contracts, found {skin_tint_contracts}")
    if combined_skin_tint_additional_alpha_contracts != 11:
        fail(
            "expected 11 combined skin-tint/additional-alpha contracts, "
            f"found {combined_skin_tint_additional_alpha_contracts}"
        )
    if combined_gradient_remap_glowmap_contracts != 10:
        fail(
            "expected 10 combined gradient-remap/glowmap contracts, "
            f"found {combined_gradient_remap_glowmap_contracts}"
        )
    if combined_gradient_remap_glowmap_additional_alpha_contracts != 3:
        fail(
            "expected 3 gradient-remap/glowmap/additional-alpha contracts, "
            "found "
            f"{combined_gradient_remap_glowmap_additional_alpha_contracts}"
        )
    if combined_face_detail_additional_alpha_contracts != 5:
        fail(
            "expected 5 combined face-detail/additional-alpha contracts, "
            f"found {combined_face_detail_additional_alpha_contracts}"
        )
    if combined_face_detail_bone_tint_contracts != 6:
        fail(
            "expected 6 combined face-detail/bone-tint contracts, "
            f"found {combined_face_detail_bone_tint_contracts}"
        )
    if combined_gradient_remap_bone_tint_contracts != 22:
        fail(
            "expected 22 combined gradient-remap/bone-tint contracts, "
            f"found {combined_gradient_remap_bone_tint_contracts}"
        )
    if combined_skin_tint_bone_tint_contracts != 4:
        fail(
            "expected 4 combined skin-tint/bone-tint contracts, "
            f"found {combined_skin_tint_bone_tint_contracts}"
        )
    if standalone_projected_bone_tint_contracts != 7:
        fail(
            "expected 7 standalone projected bone-tint contracts, "
            f"found {standalone_projected_bone_tint_contracts}"
        )
    if standalone_lod_object_contracts != 6:
        fail(
            "expected 6 standalone projected LOD-object contracts, "
            f"found {standalone_lod_object_contracts}"
        )
    if standalone_projected_model_space_contracts != 2:
        fail(
            "expected 2 standalone projected model-space contracts, "
            f"found {standalone_projected_model_space_contracts}"
        )
    if standalone_hair_contracts != 23:
        fail(
            "expected 23 hair-direction contracts, "
            f"found {standalone_hair_contracts}"
        )
    if combined_hair_additional_alpha_contracts != 7:
        fail(
            "expected 7 combined hair/additional-alpha contracts, "
            f"found {combined_hair_additional_alpha_contracts}"
        )
    if six_mrt_gradient_hair_contracts != 12:
        fail(
            "expected 12 six-MRT gradient-hair contracts, "
            f"found {six_mrt_gradient_hair_contracts}"
        )
    if combined_gradient_hair_bone_tint_contracts != 5:
        fail(
            "expected 5 gradient-hair/bone-tint contracts, "
            f"found {combined_gradient_hair_bone_tint_contracts}"
        )
    if combined_gradient_hair_additional_alpha_contracts != 3:
        fail(
            "expected 3 gradient-hair/additional-alpha contracts, "
            f"found {combined_gradient_hair_additional_alpha_contracts}"
        )
    if combined_gradient_hair_additional_alpha_bone_tint_contracts != 1:
        fail(
            "expected 1 fully combined gradient-hair mask/bone contract, "
            "found "
            f"{combined_gradient_hair_additional_alpha_bone_tint_contracts}"
        )


def verify(root: Path) -> None:
    shared = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "LinearLighting"
        / "LinearLighting.hlsli"
    )
    runtime_source = (
        root
        / "src"
        / "Features"
        / "linear_lighting"
        / "LinearLightingRuntime.cpp"
    )
    generated_contracts = (
        root
        / "src"
        / "Features"
        / "linear_lighting"
        / "GeneratedLinearLightingContracts.inl"
    )
    resources_rc = root / "src" / "resources.rc"
    parity_source = root / "tests" / "LinearLightingShaderParityTests.cpp"
    generator = root / "tools" / "generate_linear_lighting_runtime_contracts.py"
    manifest_path, contracts = load_contracts(root)
    complex_environment_contracts, _ = build_inventory(root)
    complex_environment_names = {
        contract.name for contract in complex_environment_contracts
    }

    required_files = [
        manifest_path,
        shared,
        runtime_source,
        generated_contracts,
        resources_rc,
        parity_source,
        generator,
    ]
    for contract in contracts:
        required_files.extend(
            (contract["source"], contract["packaged"], contract["vanilla"])
        )
    for required in required_files:
        if not isinstance(required, Path) or not required.is_file():
            fail(f"required shader artifact is missing: {required}")

    verify_source_contracts(root, contracts, shared)
    runtime_text = runtime_source.read_text(encoding="utf-8")
    resources_text = resources_rc.read_text(encoding="utf-8")
    parity_text = parity_source.read_text(encoding="utf-8")
    for token in (
        "output.eyeIndex = TEST_EYE_INDEX;",
        '{ "TEST_EYE_INDEX", eyeIndex == 0 ? "0" : "1" }',
        "std::array<ComPtr<ID3D11VertexShader>, 65536>",
        "for (std::uint32_t eyeIndex = 0; eyeIndex < 2; ++eyeIndex)",
        "output.dismembermentSelector = float2(",
        '{ "HAS_DISMEMBERMENT", hasDismemberment ? "1" : "0" }',
        "surfaceVariant < surfaceVariantCount;",
        '{ "HAS_MEAT_CUFF", hasMeatCuff ? "1" : "0" }',
        '"meat-cuff-invalid-low"',
        '"meat-cuff-alpha-reject"',
        '{ "HAS_COMBINED_MATERIAL", hasCombinedMaterial ? "1" : "0" }',
        "createStructuredBuffer",
        "#if USES_TESSELLATED_INPUTS || HAS_DISMEMBERMENT",
        "if (mrtCount == 5)",
        "values[55] =",
        "values[67] =",
        "paired-eye fixture produced identical motion vectors",
        "usesDiffuseTexture",
        "std::isnan(lhs) && std::isnan(rhs)",
    ):
        if token not in parity_text:
            fail(f"WARP parity is missing paired-eye coverage: {token}")
    if "values[6] = { 1.25F, 0.0F, 0.0F, 0.0F };" not in parity_text:
        fail(
            "WARP parity is missing the combined "
            "bone-tint/gradient-remap material layout"
        )
    parity_entries = re.findall(
        r'ShaderContract\{\s*"([^"]+)",\s*(\d+),\s*(true|false),\s*'
        r'(true|false)(?:,\s*(true|false))?(?:,\s*(true|false))?'
        r'(?:,\s*(true|false))?(?:,\s*(true|false))?'
        r'(?:,\s*(true|false))?(?:,\s*(true|false))?'
        r'(?:,\s*(true|false))?(?:,\s*(true|false))?'
        r'(?:,\s*(true|false))?(?:,\s*(true|false))?'
        r'(?:,\s*(true|false))?(?:,\s*(true|false))?'
        r'(?:,\s*(true|false))?(?:,\s*(true|false))?'
        r'(?:,\s*(true|false))?(?:,\s*(true|false))?'
        r'(?:,\s*(true|false))?\s*\}',
        parity_text,
    )
    parity_contracts: dict[
        str,
        tuple[
            int,
            bool,
            bool,
            bool,
            bool,
            bool,
            bool,
            bool,
            bool,
            bool,
            bool,
            bool,
            bool,
            bool,
            bool,
            bool,
            bool,
            bool,
            bool,
            bool,
        ],
    ] = {}
    for (
        name,
        mrt_count,
        has_vertex_color,
        is_instanced,
        uses_tessellated_inputs,
        has_additional_alpha_mask,
        has_landscape_lod,
        has_gradient_remap,
        has_gradient_hair,
        has_bone_tint,
        has_menu_screen,
        has_pipboy_screen,
        has_face_detail,
        face_uses_model_space_normals,
        has_skin_tint,
        has_standalone_hair,
        has_dismemberment,
        has_meat_cuff,
        has_combined_material,
        has_landscape_layers,
        has_instanced_landscape_layers,
    ) in parity_entries:
        if name in parity_contracts:
            fail(f"duplicate WARP parity contract: {name}")
        parity_contracts[name] = (
            int(mrt_count),
            has_vertex_color == "true",
            is_instanced == "true",
            uses_tessellated_inputs == "true",
            has_additional_alpha_mask == "true",
            has_landscape_lod == "true",
            has_gradient_remap == "true",
            has_gradient_hair == "true",
            has_bone_tint == "true",
            has_menu_screen == "true",
            has_pipboy_screen == "true",
            has_face_detail == "true",
            face_uses_model_space_normals == "true",
            has_skin_tint == "true",
            has_standalone_hair == "true",
            has_dismemberment == "true",
            has_meat_cuff == "true",
            has_combined_material == "true",
            has_landscape_layers == "true",
            has_instanced_landscape_layers == "true",
        )
    expected_names = {str(contract["label"]) for contract in contracts}
    if set(parity_contracts) != expected_names:
        missing = sorted(expected_names - set(parity_contracts))
        unexpected = sorted(set(parity_contracts) - expected_names)
        fail(
            "WARP parity manifest differs from runtime manifest: "
            f"missing={missing!r}, unexpected={unexpected!r}"
        )
    for token in (
        "GeneratedLinearLightingContracts.inl",
        "kShaderContracts",
        "expectedChecksum.size()) == 0",
    ):
        if token not in runtime_text:
            fail(f"runtime shader identity gate is missing: {token}")

    run_command(
        [
            sys.executable,
            str(generator),
            "--root",
            str(root),
            "--output",
            str(generated_contracts),
            "--check",
        ],
        "Linear Lighting contract generator",
    )

    for contract in contracts:
        label = str(contract["label"])
        resource = str(contract["resource"])
        packaged = contract["packaged"]
        assert isinstance(packaged, Path)
        expected_resource = (
            f'{resource} RCDATA "../package/Shaders/Community/Reconstruction/'
            f'{packaged.name}"'
        )
        if expected_resource not in resources_text:
            fail(f"embedded resource path differs for {label}")

    fxc = find_fxc()
    with tempfile.TemporaryDirectory(prefix="fo4vr-cs-ll-") as temporary:
        temp = Path(temporary)
        for index, contract in enumerate(contracts):
            label = str(contract["label"])
            source = contract["source"]
            packaged = contract["packaged"]
            vanilla = contract["vanilla"]
            assert isinstance(source, Path)
            assert isinstance(packaged, Path)
            assert isinstance(vanilla, Path)
            compiled = temp / f"candidate-{index}.dxbc"
            candidate_assembly_path = temp / f"candidate-{index}.asm"
            vanilla_assembly_path = temp / f"vanilla-{index}.asm"
            compile_arguments = [
                    "/T",
                    "ps_5_0",
                    "/E",
                    "PSMain",
                    "/O3",
                    "/Fo",
                    str(compiled),
                    "/Fc",
                    str(candidate_assembly_path),
                ]
            if label in complex_environment_names:
                compile_arguments.extend(
                    [
                        "/D",
                        "LINEAR_LIGHTING_COMPLEX_ENVIRONMENT=1",
                    ]
                )
            compile_arguments.append(str(source))
            run_fxc(fxc, compile_arguments)
            run_fxc(
                fxc,
                ["/dumpbin", "/Fc", str(vanilla_assembly_path), str(vanilla)],
            )

            compiled_bytes = compiled.read_bytes()
            if compiled_bytes != packaged.read_bytes():
                fail(f"{label} packaged DXBC is stale relative to source")
            if compiled_bytes == vanilla.read_bytes():
                fail(f"{label} replacement unexpectedly matches vanilla")

            candidate_assembly = candidate_assembly_path.read_text(
                encoding="utf-8", errors="replace"
            )
            vanilla_assembly = vanilla_assembly_path.read_text(
                encoding="utf-8", errors="replace"
            )
            candidate = parse_dcl_contract(candidate_assembly)
            original = parse_dcl_contract(vanilla_assembly)
            source_text = source.read_text(encoding="utf-8")
            expected_parity_metadata = (
                len(original["outputs"]),
                "#define LINEAR_LIGHTING_VERTEX_COLOR 1" in source_text,
                13 in original["constant_buffers"],
                "#define LINEAR_LIGHTING_TESSELLATED_INPUTS 1" in source_text,
                12 in original["samplers"]
                and 12 in original["textures"]
                and 15 in original["textures"],
                0 in original["constant_buffers"]
                and 13 in original["samplers"]
                and 15 in original["samplers"]
                and 13 in original["textures"]
                and 15 in original["textures"],
                "#define LINEAR_LIGHTING_GRADIENT_REMAP 1" in source_text
                and original["constant_buffers"].get(2)
                in (6, 7, 8, 9, 10, 11, 12)
                and 5 in original["samplers"]
                and 5 in original["textures"],
                "#define LINEAR_LIGHTING_GRADIENT_HAIR 1" in source_text,
                "#define LINEAR_LIGHTING_BONE_TINTING 1" in source_text
                and original["constant_buffers"].get(2) in (7, 8, 9)
                and 13 in original["samplers"]
                and 14 in original["samplers"]
                and 13 in original["textures"]
                and 14 in original["textures"]
                and any(
                    semantic[0] == "COLOR" and semantic[1] == 1
                    for semantic in original["input_signature"]
                ),
                "#define LINEAR_LIGHTING_MENU_SCREEN 1" in source_text
                and 4 in original["samplers"]
                and 4 in original["textures"]
                and (
                    0 not in original["constant_buffers"]
                    or (
                        "#define LINEAR_LIGHTING_LANDSCAPE_LOD 1"
                        in source_text
                        and original["constant_buffers"].get(0) == 1
                    )
                ),
                "#define LINEAR_LIGHTING_PIPBOY_SCREEN 1" in source_text
                and original["constant_buffers"].get(0) == 1
                and 4 in original["samplers"]
                and 4 in original["textures"]
                and any(
                    semantic[0] == "TEXCOORD" and semantic[1] == 6
                    for semantic in original["input_signature"]
                ),
                "#define LINEAR_LIGHTING_FACE_DETAIL 1" in source_text
                and 8 in original["samplers"]
                and 8 in original["textures"]
                and any(
                    semantic[0] == "TEXCOORD"
                    and semantic[1] == 6
                    and semantic[2] == "w"
                    for semantic in original["input_signature"]
                ),
                "#define LINEAR_LIGHTING_FACE_DETAIL 1" in source_text
                and "#define LINEAR_LIGHTING_MODEL_SPACE_NORMALS 1"
                in source_text,
                "#define LINEAR_LIGHTING_SKIN_TINT 1" in source_text
                and original["constant_buffers"].get(2) in (7, 8, 9, 10, 11, 12),
                "#define LINEAR_LIGHTING_HAIR 1" in source_text
                and (
                    (
                        len(original["outputs"]) == 6
                        and original["constant_buffers"].get(2)
                        == (
                            9
                            if (
                                "#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 1"
                                in source_text
                                and "#define LINEAR_LIGHTING_BONE_TINTING 1"
                                in source_text
                            )
                            else 8
                            if (
                                "#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 1"
                                in source_text
                                or "#define LINEAR_LIGHTING_BONE_TINTING 1"
                                in source_text
                            )
                            else 7
                        )
                        and 3 in original["samplers"]
                        and 3 in original["textures"]
                        and 2 not in original["samplers"]
                        and 2 not in original["textures"]
                    )
                    or (
                        len(original["outputs"]) == 5
                        and original["constant_buffers"].get(2)
                        == (
                            9
                            if "#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 1"
                            in source_text
                            else 8
                        )
                        and 2 in original["samplers"]
                        and 2 in original["textures"]
                        and any(
                            semantic[0] == "COLOR"
                            and semantic[1] == 0
                            and semantic[6] == "w"
                            for semantic in original["input_signature"]
                        )
                    )
                ),
                "#define LINEAR_LIGHTING_DISMEMBERMENT 1" in source_text
                and original["constant_buffers"].get(2)
                == (
                    11
                    if "#define LINEAR_LIGHTING_DISMEMBERMENT_BLEND 1"
                    in source_text
                    else 10
                    + int(
                        "#define LINEAR_LIGHTING_SKIN_TINT 1" in source_text
                        or "#define LINEAR_LIGHTING_GRADIENT_REMAP 1"
                        in source_text
                    )
                )
                + int(
                    "#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 1"
                    in source_text
                )
                and all(slot in original["samplers"] for slot in (9, 10, 11))
                and all(slot in original["textures"] for slot in (9, 10, 11))
                and any(
                    semantic[0] == "TEXCOORD" and semantic[1] == 5
                    for semantic in original["input_signature"]
                ),
                "#define LINEAR_LIGHTING_MEAT_CUFF 1" in source_text
                and len(original["outputs"]) == 5
                and original["constant_buffers"].get(2)
                == 9
                + int(
                    "#define LINEAR_LIGHTING_SKIN_TINT 1" in source_text
                    or "#define LINEAR_LIGHTING_GRADIENT_REMAP 1"
                    in source_text
                )
                + int(
                    "#define LINEAR_LIGHTING_ADDITIONAL_ALPHA_MASK 1"
                    in source_text
                )
                and original["constant_buffers"].get(12) == 51
                and all(slot in original["samplers"] for slot in (9, 10, 11))
                and all(slot in original["textures"] for slot in (9, 10, 11))
                and any(
                    semantic[0] == "TEXCOORD" and semantic[1] == 6
                    for semantic in original["input_signature"]
                ),
                "Combined" in label
                and "dcl_resource_structured t4, 92" in vanilla_assembly
                and "dcl_resource_structured t6, 100" in vanilla_assembly
                and any(
                    semantic[0] == "COLOR" and semantic[1] == 2
                    for semantic in original["input_signature"]
                ),
                "LandscapeFourLayer" in label
                and original["constant_buffers"].get(2) == 9
                and any(
                    semantic[0] == "TEXCOORD" and semantic[1] == 7
                    for semantic in original["input_signature"]
                ),
                "#define LINEAR_LIGHTING_INSTANCED_LANDSCAPE 1" in source_text
                and "dcl_resource_structured t4, 76" in vanilla_assembly
                and all(
                    f"dcl_resource_texture2darray (float,float,float,float) t{slot}"
                    in vanilla_assembly
                    for slot in (0, 1, 2, 3)
                ),
            )
            if parity_contracts[label] != expected_parity_metadata:
                fail(
                    f"{label} WARP parity metadata differs from shader reflection: "
                    f"{parity_contracts[label]!r} != {expected_parity_metadata!r}"
                )
            candidate_buffers = dict(candidate["constant_buffers"])
            frame_registers = candidate_buffers.pop(5, None)
            geometry_registers = candidate_buffers.pop(8, None)
            complex_environment_registers = candidate_buffers.pop(11, None)
            if frame_registers not in (5, 6):
                fail(f"{label} has an invalid LinearLightingFrame binding")
            if geometry_registers != 1:
                fail(f"{label} has an invalid LinearLightingGeometry binding")
            expected_complex_environment_registers = (
                1 if label in complex_environment_names else None
            )
            if complex_environment_registers != (
                expected_complex_environment_registers
            ):
                fail(
                    f"{label} has an invalid complex environment draw binding"
                )
            if candidate_buffers != original["constant_buffers"]:
                fail(
                    f"{label} replacement constant buffers differ from vanilla: "
                    f"{candidate_buffers!r} != {original['constant_buffers']!r}"
                )
            for key in (
                "samplers",
                "textures",
                "inputs",
                "input_signature",
                "outputs",
                "global_flags",
            ):
                if candidate[key] != original[key]:
                    fail(f"{label} replacement {key} differs from vanilla")
            if re.search(r"dcl_resource_.*\bt55\b", vanilla_assembly):
                fail(f"{label} original shader occupies the private LOD parameter slot")
            if re.search(r"dcl_resource_.*\bt55\b", candidate_assembly) and "dcl_resource_buffer (float,float,float,float) t55" not in candidate_assembly:
                fail(f"{label} LOD parameter transport changed")
            validate_frame_reflection(candidate_assembly)

    print(f"Linear Lighting shader contracts verified: {len(contracts)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        verify(arguments.root.resolve())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"Linear Lighting shader verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
