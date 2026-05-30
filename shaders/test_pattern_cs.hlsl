// Synthetic test source in scRGB FP16 (scRGB = nits/80). Two zones:
//   top 60%  : 8 colour bars (W,R,G,B,C,M,Y,grey) at 100 nits — exercises the
//              RGB waveform, vectorscope targets, CIE primaries.
//   bottom 40%: greyscale nit staircase 1/10/100/1000/10000 — validates the PQ
//              axis (each band lands exactly on a decade gridline).
RWTexture2D<float4> gOut : register(u0);
cbuffer TestCB : register(b0) { uint gW, gH, _p0, _p1; };

static const float kNits[5] = { 1.0, 10.0, 100.0, 1000.0, 10000.0 };

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gW || id.y >= gH) return;
    float3 c;
    if (id.y < gH * 6 / 10) {
        uint bar = min((uint)((float)id.x / gW * 8.0), 7u);
        float n = 100.0 / 80.0; // 100 nits
        float3 bars[8] = {
            float3(1,1,1), float3(1,0,0), float3(0,1,0), float3(0,0,1),
            float3(0,1,1), float3(1,0,1), float3(1,1,0), float3(0.5,0.5,0.5)
        };
        c = bars[bar] * n;
    } else {
        uint band = min((uint)((float)id.x / gW * 5.0), 4u);
        float scrgb = kNits[band] / 80.0;
        c = float3(scrgb, scrgb, scrgb);
    }
    gOut[id.xy] = float4(c, 1.0);
}
