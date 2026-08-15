// Per-channel peak (max) of the crop region: L (Rec.709 luminance), R, G, B.
// Note these are four independent maxima — the brightest pixel in luminance is
// not necessarily the brightest in any single channel.
//
// Output buffer holds 4 floats stored as uint bit patterns; non-negative IEEE
// floats compare correctly as uints, so InterlockedMax works.
#include "colorspaces.hlsli"

cbuffer PCB : register(b0)
{
    int  gCropX, gCropY, gCropW, gCropH;
    uint gSrcW, gSrcH, _p0, _p1;
};

Texture2D<float4>   gSource : register(t0);
RWByteAddressBuffer gPeaks  : register(u0); // 16 bytes: L,R,G,B float bits

groupshared uint4 sMax[64];

[numthreads(8, 8, 1)]
void CSPeak(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    uint4 v = uint4(0, 0, 0, 0);
    if ((int)id.x < gCropW && (int)id.y < gCropH) {
        int2 p = clamp(int2(gCropX + (int)id.x, gCropY + (int)id.y),
                       int2(0, 0), int2((int)gSrcW - 1, (int)gSrcH - 1));
        float3 c = max(Clamp_scRGB(gSource.Load(int3(p, 0)).rgb), 0.0);
        float lum = dot(c, float3(0.2126390, 0.7151686, 0.0721923));
        v = uint4(asuint(lum), asuint(c.r), asuint(c.g), asuint(c.b));
    }
    sMax[gi] = v;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint step = 32; step > 0; step >>= 1) {
        if (gi < step) sMax[gi] = max(sMax[gi], sMax[gi + step]);
        GroupMemoryBarrierWithGroupSync();
    }
    if (gi == 0) {
        uint prev;
        gPeaks.InterlockedMax(0,  sMax[0].x, prev);
        gPeaks.InterlockedMax(4,  sMax[0].y, prev);
        gPeaks.InterlockedMax(8,  sMax[0].z, prev);
        gPeaks.InterlockedMax(12, sMax[0].w, prev);
    }
}
