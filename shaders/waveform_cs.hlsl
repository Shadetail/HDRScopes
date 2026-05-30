// Waveform compute: clear -> histogram (counts at graphCols) + per-column
// min/max extents (at a possibly higher extentCols, for supersampled extents).
// Source is sampled on an arbitrary grid (sampleW x sampleH); bilinear filtering
// averages neighbourhoods to suppress the noise patterns nearest sampling leaves.
//
// Low-pass: a Resolve-style pre-plot filter that averages the source-derived
// signal horizontally (along source X, per scanline) BEFORE binning, so the
// trace gets cleaner without the rendered scope being smeared.
//
// Vertical axis: scRGB -> 0..1 via LinearToPQ(value, 125); 125 scRGB = 10,000 nits.
#include "colorspaces.hlsli"

#define MAX_PQ_SCRGB 125.0

cbuffer WaveformCB : register(b0)
{
    uint gGraphCols, gBins, gChannels, gMode;          // mode 0=luma,1=rgb
    uint gExtentCols, gSampleW, gSampleH, gUseBilinear;
    int  gCropX, gCropY, gCropW, gCropH;
    uint gSrcW, gSrcH, gLowPassRadius, gBlurExtents;   // lowpass radius (samples); blur->extents flag
};

Texture2D<float4> gSource    : register(t0); // possibly source-blurred (counts)
Texture2D<float4> gSourceRaw : register(t1); // unblurred original (extents)
SamplerState      gLinear    : register(s0);
RWTexture2D<uint> gBinsRW    : register(u0); // (gGraphCols, gBins * gChannels)
RWTexture2D<uint> gExtentsRW : register(u1); // (gExtentCols, 2 * gChannels)
RWTexture2D<uint> gColorRW   : register(u2); // (gGraphCols, gBins * 3) luma colour sums

float ScrgbLuminance(float3 c)
{
    return dot(c, float3(0.2126390039920806884765625,
                         0.715168654918670654296875,
                         0.072192318737506866455078125));
}

int ValueToBin(float scrgbValue)
{
    float x = pow(max(scrgbValue, 0.0) / MAX_PQ_SCRGB, PQ.N);
    float nd = (PQ.C1 + PQ.C2 * x) / (1.0 + PQ.C3 * x);
    float pos01 = pow(nd, PQ.M);
    int bin = (int)(pos01 * (gBins - 1) + 0.5);
    return clamp(bin, 0, (int)gBins - 1);
}

// Sample the source at sample-space (u,v), honouring the bilinear/nearest mode.
float3 SampleAt(Texture2D<float4> tex, float u, float v)
{
    float3 c;
    if (gUseBilinear) {
        float2 texUV = float2((gCropX + u * gCropW) / gSrcW,
                              (gCropY + v * gCropH) / gSrcH);
        c = tex.SampleLevel(gLinear, texUV, 0).rgb;
    } else {
        int2 src = int2(gCropX + (int)(u * gCropW), gCropY + (int)(v * gCropH));
        src = clamp(src, int2(0, 0), int2(gSrcW - 1, gSrcH - 1));
        c = tex.Load(int3(src, 0)).rgb;
    }
    return c;
}

[numthreads(8, 8, 1)]
void CSClearExtents(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gExtentCols || id.y >= gChannels) return;
    gExtentsRW[uint2(id.x, 2 * id.y + 0)] = gBins; // min starts high
    gExtentsRW[uint2(id.x, 2 * id.y + 1)] = 0;     // max starts low
}

void AddCount(uint col, uint ch, int bin) { InterlockedAdd(gBinsRW[uint2(col, ch * gBins + (uint)bin)], 1u); }
void AddExtent(uint extCol, uint ch, int bin)
{
    InterlockedMin(gExtentsRW[uint2(extCol, 2 * ch + 0)], (uint)bin);
    InterlockedMax(gExtentsRW[uint2(extCol, 2 * ch + 1)], (uint)bin);
}

[numthreads(8, 8, 1)]
void CSHistogram(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gSampleW || id.y >= gSampleH) return;

    float u = (id.x + 0.5) / gSampleW;
    float v = (id.y + 0.5) / gSampleH;

    // Counts/density source — optionally horizontally low-passed along source X.
    float3 c = SampleAt(gSource, u, v);
    if (gLowPassRadius > 0u) {
        float3 acc = c; float wsum = 1.0;
        for (uint k = 1; k <= gLowPassRadius; ++k) {
            float du = (float)k / gSampleW;
            acc += SampleAt(gSource, u - du, v);
            acc += SampleAt(gSource, u + du, v);
            wsum += 2.0;
        }
        c = acc / wsum;
    }
    c = Clamp_scRGB(c);

    // Extents source: the raw (unblurred) signal unless blur should affect them.
    float3 cExt = (gBlurExtents == 0u) ? Clamp_scRGB(SampleAt(gSourceRaw, u, v)) : c;

    uint col    = min((uint)(u * gGraphCols),  gGraphCols - 1);
    uint extCol = min((uint)(u * gExtentCols), gExtentCols - 1);

    if (gMode == 0) {
        int bin = ValueToBin(ScrgbLuminance(c));
        AddCount(col, 0, bin);
        // Accumulate the source pixel's normalized colour at this cell so the
        // luma trace can be tinted by the real image colour (Resolve-style).
        float mx = max(c.r, max(c.g, c.b));
        float3 norm = (mx > 1e-5) ? saturate(c / mx) : float3(1, 1, 1);
        uint3 q = (uint3)(norm * 1023.0 + 0.5);
        InterlockedAdd(gColorRW[uint2(col, 0u * gBins + (uint)bin)], q.r);
        InterlockedAdd(gColorRW[uint2(col, 1u * gBins + (uint)bin)], q.g);
        InterlockedAdd(gColorRW[uint2(col, 2u * gBins + (uint)bin)], q.b);
        AddExtent(extCol, 0, ValueToBin(ScrgbLuminance(cExt)));
    } else {
        AddCount(col, 0, ValueToBin(c.r));
        AddCount(col, 1, ValueToBin(c.g));
        AddCount(col, 2, ValueToBin(c.b));
        AddExtent(extCol, 0, ValueToBin(cExt.r));
        AddExtent(extCol, 1, ValueToBin(cExt.g));
        AddExtent(extCol, 2, ValueToBin(cExt.b));
    }
}
