// Waveform compute: clear -> histogram (counts at graphCols) + per-column
// min/max extents (at a possibly higher extentCols, for supersampled extents).
// Source is sampled on an arbitrary grid (sampleW x sampleH); bilinear filtering
// averages neighbourhoods to suppress the noise patterns nearest sampling leaves.
//
// Vertical axis: scRGB -> 0..1 via LinearToPQ(value, 125); 125 scRGB = 10,000 nits.
#include "colorspaces.hlsli"

#define MAX_PQ_SCRGB 125.0

cbuffer WaveformCB : register(b0)
{
    uint gGraphCols, gBins, gChannels, gMode;          // mode 0=luma,1=rgb
    uint gExtentCols, gSampleW, gSampleH, gUseBilinear;
    int  gCropX, gCropY, gCropW, gCropH;
    uint gSrcW, gSrcH, gPad0, gPad1;
};

Texture2D<float4> gSource    : register(t0);
SamplerState      gLinear    : register(s0);
RWTexture2D<uint> gBinsRW    : register(u0); // (gGraphCols, gBins * gChannels)
RWTexture2D<uint> gExtentsRW : register(u1); // (gExtentCols, 2 * gChannels)

float ScrgbLuminance(float3 c)
{
    return dot(c, float3(0.2126390039920806884765625,
                         0.715168654918670654296875,
                         0.072192318737506866455078125));
}

int ValueToBin(float scrgbValue)
{
    float pos01 = LinearToPQY(max(scrgbValue, 0.0), MAX_PQ_SCRGB);
    int bin = (int)(pos01 * (gBins - 1) + 0.5);
    return clamp(bin, 0, (int)gBins - 1);
}

[numthreads(8, 8, 1)]
void CSClearExtents(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gExtentCols || id.y >= gChannels) return;
    gExtentsRW[uint2(id.x, 2 * id.y + 0)] = gBins; // min starts high
    gExtentsRW[uint2(id.x, 2 * id.y + 1)] = 0;     // max starts low
}

void Accumulate(uint col, uint extCol, uint ch, float scrgbValue)
{
    int bin = ValueToBin(scrgbValue);
    InterlockedAdd(gBinsRW[uint2(col, ch * gBins + (uint)bin)], 1u);
    InterlockedMin(gExtentsRW[uint2(extCol, 2 * ch + 0)], (uint)bin);
    InterlockedMax(gExtentsRW[uint2(extCol, 2 * ch + 1)], (uint)bin);
}

[numthreads(8, 8, 1)]
void CSHistogram(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gSampleW || id.y >= gSampleH) return;

    float u = (id.x + 0.5) / gSampleW;
    float v = (id.y + 0.5) / gSampleH;

    float3 c;
    if (gUseBilinear) {
        float2 texUV = float2((gCropX + u * gCropW) / gSrcW,
                              (gCropY + v * gCropH) / gSrcH);
        c = gSource.SampleLevel(gLinear, texUV, 0).rgb;
    } else {
        int2 src = int2(gCropX + (int)(u * gCropW), gCropY + (int)(v * gCropH));
        src = clamp(src, int2(0, 0), int2(gSrcW - 1, gSrcH - 1));
        c = gSource.Load(int3(src, 0)).rgb;
    }
    c = Clamp_scRGB(c);

    uint col    = min((uint)(u * gGraphCols),  gGraphCols - 1);
    uint extCol = min((uint)(u * gExtentCols), gExtentCols - 1);

    if (gMode == 0) {
        Accumulate(col, extCol, 0, ScrgbLuminance(c));
    } else {
        Accumulate(col, extCol, 0, c.r);
        Accumulate(col, extCol, 1, c.g);
        Accumulate(col, extCol, 2, c.b);
    }
}
