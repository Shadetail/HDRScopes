// Waveform compute: clear -> histogram (with per-column min/max for Extents).
// Resolve/normalize happens render-side in graph_ps.hlsl over the bins texture.
//
// Vertical axis is PQ-spaced nits over a fixed 0..10,000 range. scRGB is linear
// Rec.709 with 1.0 = 80 nits, so 10,000 nits == scRGB 125. Feeding the scRGB
// value straight into LinearToPQ(value, 125) yields the 0..1 vertical position.
#include "colorspaces.hlsli"

#define MAX_PQ_SCRGB 125.0  // scRGB value that maps to 10,000 nits (top of axis)

cbuffer WaveformCB : register(b0)
{
    uint gGraphCols;   // horizontal resolution of the graph (decoupled from display)
    uint gBins;        // vertical resolution (PQ-axis subdivisions)
    uint gChannels;    // 1 = luminance, 3 = RGB
    uint gMode;        // 0 = luminance, 1 = RGB per-channel

    int  gCropX, gCropY, gCropW, gCropH; // source region in capture-texel space
    uint gSrcW, gSrcH;                   // capture texture dimensions
    uint _pad0, _pad1;
};

Texture2D<float4> gSource   : register(t0);
RWTexture2D<uint> gBinsRW    : register(u0); // (gGraphCols, gBins * gChannels)
RWTexture2D<uint> gExtentsRW : register(u1); // (gGraphCols, 2 * gChannels): 2c=min,2c+1=max bin

// scRGB luminance (linear, Rec.709 primaries — the Y row of Rec709->XYZ). This
// is physics for Rec.709-primaried data, not an SDR luma choice.
float ScrgbLuminance(float3 c)
{
    return dot(c, float3(0.2126390039920806884765625,
                         0.715168654918670654296875,
                         0.072192318737506866455078125));
}

// Map an scRGB value to a 0..(gBins-1) row, clamped (>=10,000 nits -> top row).
int ValueToBin(float scrgbValue)
{
    float pos01 = LinearToPQY(max(scrgbValue, 0.0), MAX_PQ_SCRGB);
    int bin = (int)(pos01 * (gBins - 1) + 0.5);
    return clamp(bin, 0, (int)gBins - 1);
}

[numthreads(8, 8, 1)]
void CSClear(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gGraphCols || id.y >= gBins * gChannels) return;
    gBinsRW[id.xy] = 0;

    // Initialise per-column extents once per channel row band.
    if (id.y < gChannels) {
        gExtentsRW[uint2(id.x, 2 * id.y + 0)] = gBins; // min starts high
        gExtentsRW[uint2(id.x, 2 * id.y + 1)] = 0;      // max starts low
    }
}

void Accumulate(uint col, uint ch, float scrgbValue)
{
    int bin = ValueToBin(scrgbValue);
    uint row = ch * gBins + (uint)bin;
    InterlockedAdd(gBinsRW[uint2(col, row)], 1u);
    InterlockedMin(gExtentsRW[uint2(col, 2 * ch + 0)], (uint)bin);
    InterlockedMax(gExtentsRW[uint2(col, 2 * ch + 1)], (uint)bin);
}

[numthreads(8, 8, 1)]
void CSHistogram(uint3 id : SV_DispatchThreadID)
{
    if ((int)id.x >= gCropW || (int)id.y >= gCropH) return;

    int2 src = int2(gCropX + (int)id.x, gCropY + (int)id.y);
    if (src.x < 0 || src.y < 0 || src.x >= (int)gSrcW || src.y >= (int)gSrcH) return;

    float3 c = Clamp_scRGB(gSource.Load(int3(src, 0)).rgb);

    // Column the pixel falls into (graph X is decoupled from source width).
    uint col = (uint)(((float)id.x / (float)max(gCropW, 1)) * gGraphCols);
    col = min(col, gGraphCols - 1);

    if (gMode == 0) {
        Accumulate(col, 0, ScrgbLuminance(c));
    } else {
        Accumulate(col, 0, c.r);
        Accumulate(col, 1, c.g);
        Accumulate(col, 2, c.b);
    }
}
