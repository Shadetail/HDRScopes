// Synthetic known-nit test texture (Step 3). Writes vertical bands of exact
// nit levels into an scRGB FP16 texture so LinearToPQ axis placement can be
// validated before trusting live capture. scRGB = nits / 80.
//
// Bands (left -> right): 1, 10, 100, 1000, 10000 nits. Each band should land
// exactly on a decade gridline in the waveform.

RWTexture2D<float4> gOut : register(u0);

cbuffer TestCB : register(b0)
{
    uint gW, gH, _p0, _p1;
};

static const float kNits[5] = { 1.0, 10.0, 100.0, 1000.0, 10000.0 };

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gW || id.y >= gH) return;
    uint band = min((uint)((float)id.x / gW * 5.0), 4u);
    float nits = kNits[band];
    float scrgb = nits / 80.0;
    gOut[id.xy] = float4(scrgb, scrgb, scrgb, 1.0);
}
