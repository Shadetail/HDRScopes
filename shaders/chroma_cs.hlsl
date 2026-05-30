// Shared 2D chroma accumulation for the vectorscope and CIE chromaticity scope.
// Each source pixel maps to a point in [0,1]^2 and increments a square bin grid.
//   mode 0 = vectorscope (ICtCp Ct/Cp)
//   mode 1 = CIE 1931 xy
//   mode 2 = CIE 1976 u'v'
#include "colorspaces.hlsli"

cbuffer CCB : register(b0)
{
    uint  gSize, gMode, gSampleW, gSampleH;
    uint  gUseBilinear, _pa, _pb, _pc;
    int   gCropX, gCropY, gCropW, gCropH;
    uint  gSrcW, gSrcH, _pd, _pe;
    float gMinX, gMaxX, gMinY, gMaxY; // plot range (CIE) / unused for vector
    float gScale, _pf, _pg, _ph;      // vector chroma scale
};

Texture2D<float4> gSource : register(t0);
SamplerState      gLinear : register(s0);
RWTexture2D<uint> gAccum  : register(u0); // gSize x gSize

bool MapPoint(float3 c, out float2 p)
{
    p = float2(0, 0);
    if (gMode == 0) {
        float3 ictcp = Rec709toICtCp(max(c, 0.0));
        p = float2(0.5 + ictcp.y * gScale, 0.5 - ictcp.z * gScale);
        return true;
    }
    float3 XYZ = Rec709_to_XYZ(max(c, 0.0));
    float s = XYZ.x + XYZ.y + XYZ.z;
    if (s <= 1e-6) return false;
    if (gMode == 1) {
        float x = XYZ.x / s, y = XYZ.y / s;
        p = float2((x - gMinX) / (gMaxX - gMinX), 1.0 - (y - gMinY) / (gMaxY - gMinY));
    } else {
        float d = XYZ.x + 15.0 * XYZ.y + 3.0 * XYZ.z;
        float u = 4.0 * XYZ.x / d, v = 9.0 * XYZ.y / d;
        p = float2((u - gMinX) / (gMaxX - gMinX), 1.0 - (v - gMinY) / (gMaxY - gMinY));
    }
    return true;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
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
    float2 p;
    if (!MapPoint(c, p)) return;
    if (p.x < 0 || p.x >= 1 || p.y < 0 || p.y >= 1) return;
    uint2 px = uint2(p.x * gSize, p.y * gSize);
    InterlockedAdd(gAccum[px], 1u);
}
