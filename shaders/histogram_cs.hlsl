// Histogram compute: 1D pixel count per PQ-nit bin, for L, R, G, B (4 rows).
// X axis = nits (PQ-spaced), Y = count. Sampled on a grid with optional bilinear.
#include "colorspaces.hlsli"
#define MAX_PQ_SCRGB 125.0

cbuffer HCB : register(b0)
{
    uint gBins, gSampleW, gSampleH, gUseBilinear;
    int  gCropX, gCropY, gCropW, gCropH;
    uint gSrcW, gSrcH, _p0, _p1;
};

Texture2D<float4> gSource : register(t0);
SamplerState      gLinear : register(s0);
RWTexture2D<uint> gHist   : register(u0); // (gBins, 4): row 0=L,1=R,2=G,3=B

float ScrgbLum(float3 c) { return dot(c, float3(0.2126390, 0.7151686, 0.0721923)); }
int ToBin(float v) { float p = LinearToPQY(max(v, 0.0), MAX_PQ_SCRGB); return clamp((int)(p * (gBins - 1) + 0.5), 0, (int)gBins - 1); }

[numthreads(8, 8, 1)]
void CSHistogram(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gSampleW || id.y >= gSampleH) return;
    float u = (id.x + 0.5) / gSampleW, v = (id.y + 0.5) / gSampleH;
    float3 c;
    if (gUseBilinear) {
        float2 t = float2((gCropX + u * gCropW) / gSrcW, (gCropY + v * gCropH) / gSrcH);
        c = gSource.SampleLevel(gLinear, t, 0).rgb;
    } else {
        int2 s = clamp(int2(gCropX + (int)(u * gCropW), gCropY + (int)(v * gCropH)), int2(0, 0), int2(gSrcW - 1, gSrcH - 1));
        c = gSource.Load(int3(s, 0)).rgb;
    }
    c = Clamp_scRGB(c);
    InterlockedAdd(gHist[uint2(ToBin(ScrgbLum(c)), 0)], 1u);
    InterlockedAdd(gHist[uint2(ToBin(c.r), 1)], 1u);
    InterlockedAdd(gHist[uint2(ToBin(c.g), 2)], 1u);
    InterlockedAdd(gHist[uint2(ToBin(c.b), 3)], 1u);
}
