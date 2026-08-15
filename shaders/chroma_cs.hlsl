// Shared 2D chroma accumulation for the vectorscope and CIE chromaticity scope.
// Each source pixel maps to a point in [0,1]^2 and increments a square bin grid.
//   mode 0 = vectorscope (Rec.709-matrix Y'CbCr; PQ or Rec.709-gamma encoding)
//   mode 1 = CIE 1931 xy
//   mode 2 = CIE 1976 u'v'
#include "colorspaces.hlsli"

cbuffer CCB : register(b0)
{
    uint  gSize, gMode, gSampleW, gSampleH;
    uint  gUseBilinear, gVectorPQ, _pb, _pc;
    int   gCropX, gCropY, gCropW, gCropH;
    uint  gSrcW, gSrcH, _pd, _pe;
    float gMinX, gMaxX, gMinY, gMaxY; // plot range (CIE) / unused for vector
    float gScale, gSdrNorm, _pg, _ph; // vector chroma scale; SDR-white scRGB (=nits/80)
};

// Rec.709 OETF, sign/range preserving (handles negatives and > 1.0).
float3 Rec709OETF(float3 x) {
    float3 a = abs(x);
    float3 v = (a < 0.018) ? (4.5 * a) : (1.0993 * pow(a, 0.45) - 0.0993);
    return sign(x) * v;
}

// ST.2084 PQ encode, sign preserving (125.0 scRGB = 10,000 nits -> 1.0). Local
// because colorspaces.hlsli's LinearToPQ trips a spurious X4000 warning here.
float3 PQEncode(float3 x) {
    const float N = 0.1593017578125, M = 78.84375;
    const float C1 = 0.8359375, C2 = 18.8515625, C3 = 18.6875;
    float3 v = pow(abs(x) / 125.0, N);
    return sign(x) * pow((C1 + C2 * v) / (1.0 + C3 * v), M);
}

Texture2D<float4> gSource : register(t0);
SamplerState      gLinear : register(s0);
RWTexture2D<uint> gAccum  : register(u0); // gSize x gSize

// fxc reports a spurious X4000 ("potentially uninitialized variable") on
// MapPoint's early-return shape; every variable is assigned on all paths.
#pragma warning(push)
#pragma warning(disable : 4000)
bool MapPoint(float3 c, out float2 p)
{
    p = float2(0, 0);
    if (gMode == 0) {
        // Rec.709-matrix Y'CbCr vectorscope, two encodings:
        //  PQ (default): absolute-nits ST.2084 per Rec.709 channel — HDR-style
        //   response: chroma amplitude grows only slowly with brightness and is
        //   independent of the SDR-white level. (Not a broadcast-exact BT.2100
        //   scope: the source is the composited scRGB desktop, not code values,
        //   so authored HDR bar patterns need not land exactly on the targets.)
        //  Rec.709 gamma: classic SDR vectorscope (matches DaVinci SDR timelines) —
        //   normalized to SDR white so 100% SDR primaries hit full amplitude.
        float3 sig = gVectorPQ ? PQEncode(c)
                               : Rec709OETF(c / max(gSdrNorm, 1e-4));
        float Y  = dot(sig, float3(0.2126, 0.7152, 0.0722));
        float Cb = (sig.b - Y) / 1.8556;
        float Cr = (sig.r - Y) / 1.5748;
        p = float2(0.5 + Cb * gScale, 0.5 - Cr * gScale);
        return true;
    }
    // CIE: do NOT clamp negatives — wide-gamut colors (scRGB channels < 0) must be
    // allowed to plot outside the Rec.709 triangle.
    float3 XYZ = Rec709_to_XYZ(c);
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
#pragma warning(pop)

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
