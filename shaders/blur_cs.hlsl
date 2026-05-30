// Separable Gaussian blur of the captured scRGB texture (source blur option).
// Run twice (horizontal then vertical) ping-ponging between two RGBA16F targets.
cbuffer BCB : register(b0)
{
    uint gW, gH; float gSigma; float gDirX;
    float gDirY, _p0, _p1, _p2;
};
Texture2D<float4> gIn  : register(t0);
SamplerState      gLin : register(s0);
RWTexture2D<float4> gOut : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gW || id.y >= gH) return;
    float2 dim = float2(gW, gH);
    float2 uv = (id.xy + 0.5) / dim;
    float2 dir = float2(gDirX, gDirY) / dim;
    float sigma = max(gSigma, 0.0001);
    float step = max(sigma / 3.0, 0.5);

    float4 sum = 0; float wsum = 0;
    [loop] for (int i = -8; i <= 8; ++i) {
        float d = i * step;                      // distance in source px
        float w = exp(-(d * d) / (2.0 * sigma * sigma));
        sum += w * gIn.SampleLevel(gLin, uv + dir * d, 0);
        wsum += w;
    }
    gOut[id.xy] = sum / max(wsum, 1e-4);
}
