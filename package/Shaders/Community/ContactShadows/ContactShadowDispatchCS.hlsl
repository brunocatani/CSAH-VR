// Copyright 2023 Sony Interactive Entertainment.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Adapted for FO4VR from the Bend screen-space shadow wavefront algorithm.

#pragma warning(disable: 3556)

Texture2D<float> SceneDepth : register(t0);
cbuffer ContactShadowSettings : register(b13)
{
    float4 ContactParams0,ContactParams1,ContactParams2,CloudParams,RenderViewport;
};

cbuffer NativeDFLight : register(b2)
{
    float4 DFLight[46];
};

cbuffer NativeCamera : register(b12)
{
    float4 Camera[51];
};

struct DispatchRecord
{
    float4 lightCoordinate;
    int2 waveOffset;
    uint eye;
    uint active;
};

RWStructuredBuffer<DispatchRecord> DispatchRecords : register(u0);
RWByteAddressBuffer DispatchArguments : register(u1);

static const uint kWaveSize = 64u;
static const uint kDispatchesPerEye = 8u;

float4 ProjectViewDirection(float3 direction, uint eye)
{
    const uint row = 4u + eye * 4u;
    const float4 homogeneousDirection = float4(direction, 0.0f);
    return float4(
        dot(Camera[row + 0u], homogeneousDirection),
        dot(Camera[row + 1u], homogeneousDirection),
        dot(Camera[row + 2u], homogeneousDirection),
        dot(Camera[row + 3u], homogeneousDirection));
}

void StoreInactive(uint recordIndex)
{
    DispatchRecord record;
    record.lightCoordinate = 0.0f;
    record.waveOffset = 0;
    record.eye = recordIndex / kDispatchesPerEye;
    record.active = 0u;
    DispatchRecords[recordIndex] = record;

    const uint argumentOffset = recordIndex * 12u;
    DispatchArguments.Store(argumentOffset + 0u, 0u);
    DispatchArguments.Store(argumentOffset + 4u, 0u);
    DispatchArguments.Store(argumentOffset + 8u, 0u);
}

void BuildEyeDispatches(uint eye, int2 viewportSize)
{
    const uint recordBase = eye * kDispatchesPerEye;
    [unroll]
    for (uint index = 0u; index < kDispatchesPerEye; ++index) {
        StoreInactive(recordBase + index);
    }

    const float3 towardLight = normalize(DFLight[eye + 1u].xyz);
    const float4 lightProjection = ProjectViewDirection(towardLight, eye);
    float xyLightW = lightProjection.w;
    const float floatingPointLimit = 0.000002f * (float)kWaveSize;
    if (xyLightW >= 0.0f && xyLightW < floatingPointLimit) {
        xyLightW = floatingPointLimit;
    } else if (xyLightW < 0.0f && xyLightW > -floatingPointLimit) {
        xyLightW = -floatingPointLimit;
    }

    float4 lightCoordinate;
    lightCoordinate.x =
        (lightProjection.x / xyLightW * 0.5f + 0.5f) *
        (float)viewportSize.x;
    lightCoordinate.y =
        (lightProjection.y / xyLightW * -0.5f + 0.5f) *
        (float)viewportSize.y;
    lightCoordinate.z = abs(lightProjection.w) > 1.0e-7f ?
        lightProjection.z / lightProjection.w : 0.0f;
    lightCoordinate.w = lightProjection.w > 0.0f ? 1.0f : -1.0f;

    const int2 lightPixel = int2(lightCoordinate.xy + 0.5f);
    const int4 biasedBounds = int4(
        -lightPixel.x,
        -(viewportSize.y - lightPixel.y),
        viewportSize.x - lightPixel.x,
        lightPixel.y);

    int3 waveCounts[8];
    int2 waveOffsets[8];
    uint dispatchCount = 0u;

    [unroll]
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        const bool vertical = quadrant == 0 || quadrant == 3;
        const int4 bounds = int4(
            max(0, (quadrant & 1) != 0 ?
                biasedBounds.x : -biasedBounds.z) / (int)kWaveSize,
            max(0, (quadrant & 2) != 0 ?
                biasedBounds.y : -biasedBounds.w) / (int)kWaveSize,
            max(0, ((quadrant & 1) != 0 ?
                biasedBounds.z : -biasedBounds.x) +
                (int)kWaveSize * (vertical ? 1 : 2) - 1) /
                (int)kWaveSize,
            max(0, ((quadrant & 2) != 0 ?
                biasedBounds.w : -biasedBounds.y) +
                (int)kWaveSize * (vertical ? 2 : 1) - 1) /
                (int)kWaveSize);

        if (bounds.z <= bounds.x || bounds.w <= bounds.y) {
            continue;
        }

        const int biasX = quadrant >= 2 ? 1 : 0;
        const int biasY = quadrant == 1 || quadrant == 3 ? 1 : 0;
        uint dispatchIndex = dispatchCount++;
        waveCounts[dispatchIndex] = int3(
            (int)kWaveSize,
            bounds.z - bounds.x,
            bounds.w - bounds.y);
        waveOffsets[dispatchIndex] = int2(
            ((quadrant & 1) != 0 ? bounds.x : -bounds.z) + biasX,
            ((quadrant & 2) != 0 ? -bounds.w : bounds.y) + biasY);

        int axisDelta = biasedBounds.x - biasedBounds.y;
        if (quadrant == 1) {
            axisDelta = biasedBounds.z + biasedBounds.y;
        } else if (quadrant == 2) {
            axisDelta = -biasedBounds.x - biasedBounds.w;
        } else if (quadrant == 3) {
            axisDelta = -biasedBounds.z + biasedBounds.w;
        }
        axisDelta = (axisDelta + (int)kWaveSize - 1) /
            (int)kWaveSize;
        if (axisDelta <= 0 || dispatchCount >= kDispatchesPerEye) {
            continue;
        }

        const uint splitIndex = dispatchCount++;
        waveCounts[splitIndex] = waveCounts[dispatchIndex];
        waveOffsets[splitIndex] = waveOffsets[dispatchIndex];
        if (quadrant == 0) {
            waveCounts[splitIndex].z = min(
                waveCounts[dispatchIndex].z, axisDelta);
            waveCounts[dispatchIndex].z -= waveCounts[splitIndex].z;
            waveOffsets[splitIndex].y =
                waveOffsets[dispatchIndex].y +
                waveCounts[dispatchIndex].z;
            waveOffsets[splitIndex].x--;
            waveCounts[splitIndex].y++;
        } else if (quadrant == 1) {
            waveCounts[splitIndex].y = min(
                waveCounts[dispatchIndex].y, axisDelta);
            waveCounts[dispatchIndex].y -= waveCounts[splitIndex].y;
            waveOffsets[splitIndex].x =
                waveOffsets[dispatchIndex].x +
                waveCounts[dispatchIndex].y;
            waveCounts[splitIndex].z++;
        } else if (quadrant == 2) {
            waveCounts[splitIndex].y = min(
                waveCounts[dispatchIndex].y, axisDelta);
            waveCounts[dispatchIndex].y -= waveCounts[splitIndex].y;
            waveOffsets[dispatchIndex].x += waveCounts[splitIndex].y;
            waveCounts[splitIndex].z++;
            waveOffsets[splitIndex].y--;
        } else {
            waveCounts[splitIndex].z = min(
                waveCounts[dispatchIndex].z, axisDelta);
            waveCounts[dispatchIndex].z -= waveCounts[splitIndex].z;
            waveOffsets[dispatchIndex].y += waveCounts[splitIndex].z;
            waveCounts[splitIndex].y++;
        }

        if (waveCounts[splitIndex].y <= 0 ||
            waveCounts[splitIndex].z <= 0) {
            dispatchCount--;
        }
        if (waveCounts[dispatchIndex].y <= 0 ||
            waveCounts[dispatchIndex].z <= 0) {
            --dispatchCount;
            waveCounts[dispatchIndex] = waveCounts[dispatchCount];
            waveOffsets[dispatchIndex] = waveOffsets[dispatchCount];
        }
    }

    [loop]
    for (uint outputIndex = 0u;
         outputIndex < dispatchCount && outputIndex < kDispatchesPerEye;
         ++outputIndex) {
        DispatchRecord record;
        record.lightCoordinate = lightCoordinate;
        record.waveOffset = waveOffsets[outputIndex] * (int)kWaveSize;
        record.eye = eye;
        record.active = 1u;
        DispatchRecords[recordBase + outputIndex] = record;

        const uint argumentOffset = (recordBase + outputIndex) * 12u;
        DispatchArguments.Store(
            argumentOffset + 0u,
            asuint(max(waveCounts[outputIndex].x, 0)));
        DispatchArguments.Store(
            argumentOffset + 4u,
            asuint(max(waveCounts[outputIndex].y, 0)));
        DispatchArguments.Store(
            argumentOffset + 8u,
            asuint(max(waveCounts[outputIndex].z, 0)));
    }
}

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThread : SV_DispatchThreadID)
{
    const uint width = (uint)RenderViewport.z;
    const uint height = (uint)RenderViewport.w;
    if (dispatchThread.x != 0u || width < 2u || height == 0u ||
        (width & 1u) != 0u) {
        return;
    }

    const int2 viewportSize = int2(width / 2u, height);
    BuildEyeDispatches(0u, viewportSize);
    BuildEyeDispatches(1u, viewportSize);
}
