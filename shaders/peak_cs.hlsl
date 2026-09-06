// Per-channel peak (max) of the crop region: L (Rec.709 luminance), R, G, B —
// plus the SUM of luminance over the region, for the average. Note the peaks
// are four independent maxima — the brightest pixel in luminance is not
// necessarily the brightest in any single channel.
//
// Output buffer, 8 uints: { L, R, G, B, sumLo, sumHi, pad, pad }. The peaks
// are float bit patterns; non-negative IEEE floats compare correctly as uints,
// so InterlockedMax works. SM5 has no float atomics, so the sum is 64-bit fixed
// point (16 fractional bits) split over two uints: each group reduces its 64
// pixels in groupshared memory and adds one 64-bit integer per group.
#include "colorspaces.hlsli"

cbuffer PCB : register(b0)
{
    int  gCropX, gCropY, gCropW, gCropH;
    uint gSrcW, gSrcH, _p0, _p1;
};

Texture2D<float4>   gSource : register(t0);
RWByteAddressBuffer gPeaks  : register(u0); // 32 bytes: L,R,G,B float bits, sumLo, sumHi, pad, pad

groupshared uint4 sMax[64];
groupshared float sSum[64];

[numthreads(8, 8, 1)]
void CSPeak(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    uint4 v = uint4(0, 0, 0, 0);
    float lumSum = 0.0;
    if ((int)id.x < gCropW && (int)id.y < gCropH) {
        int2 p = clamp(int2(gCropX + (int)id.x, gCropY + (int)id.y),
                       int2(0, 0), int2((int)gSrcW - 1, (int)gSrcH - 1));
        float3 c = Clamp_scRGB(gSource.Load(int3(p, 0)).rgb);
        // L from the unclamped channels (negative out-of-Rec.709 components
        // included) so it matches the probe and the scopes; the per-channel
        // peaks need non-negative floats for the uint-compare InterlockedMax.
        float  lum = max(dot(c, float3(0.2126390, 0.7151686, 0.0721923)), 0.0);
        float3 cp  = max(c, 0.0);
        v = uint4(asuint(lum), asuint(cp.r), asuint(cp.g), asuint(cp.b));
        lumSum = lum;  // no cap: Clamp_scRGB already bounds each value at 65504
    }
    sMax[gi] = v;
    sSum[gi] = lumSum;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint step = 32; step > 0; step >>= 1) {
        if (gi < step) {
            sMax[gi] = max(sMax[gi], sMax[gi + step]);
            sSum[gi] += sSum[gi + step];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (gi == 0) {
        uint prev;
        gPeaks.InterlockedMax(0,  sMax[0].x, prev);
        gPeaks.InterlockedMax(4,  sMax[0].y, prev);
        gPeaks.InterlockedMax(8,  sMax[0].z, prev);
        gPeaks.InterlockedMax(12, sMax[0].w, prev);
        // Group sum <= 64 * 65504, so hi <= 63. The power-of-2 scaling and the
        // subtraction are exact in float, so lo < 2^32 (truncation loses at most
        // 2^-16 per group). Carry into the high word when the low add wrapped.
        float s  = sSum[0];
        uint  hi = (uint)(s * (1.0 / 65536.0));
        uint  lo = (uint)((s - hi * 65536.0) * 65536.0);
        gPeaks.InterlockedAdd(16, lo, prev);
        if (prev + lo < prev) ++hi;
        if (hi) gPeaks.InterlockedAdd(20, hi, prev);
    }
}
