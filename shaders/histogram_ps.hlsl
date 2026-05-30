// Histogram render. X = nits (PQ), Y = pixel count. Modes: LRGB (4 stacked
// bands), overlaid, luma. X axis takes the pan/zoom transform; Y is the count.
cbuffer GCB : register(b0)
{
    uint  gBins, gMode, gChanMask, gColorize;   // mode 0=LRGB rows,1=overlay,2=luma
    float gGain; float gUVScaleX, gUVOffX, gXAxisTop01; // X top (PQ pos01) for SDR-white zoom
};
Texture2D<uint> gHist : register(t0); // (gBins, 4)

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VSMain(uint vid : SV_VertexID) {
    VSOut o; float2 uv = float2((vid << 1) & 2, vid & 2);
    o.uv = uv; o.pos = float4(uv.x * 2 - 1, 1 - uv.y * 2, 0, 1); return o;
}

static const float3 kC[4] = {
    float3(0.9,0.9,0.9), float3(1.0,0.18,0.18), float3(0.18,1.0,0.18), float3(0.3,0.55,1.0)
};

float H(uint row, uint bin) { return saturate(1.0 - exp(-(float)gHist.Load(int3(bin, row, 0)) * gGain)); }

float4 PSMain(VSOut i) : SV_Target
{
    float ux = gUVOffX + i.uv.x * gUVScaleX;
    if (ux < 0 || ux > 1) return float4(0, 0, 0, 1);
    // Map display X into PQ position, optionally zoomed to the SDR-white range.
    uint bin = min((uint)(ux * gXAxisTop01 * (gBins - 1) + 0.5), gBins - 1);
    float3 rgb = float3(0, 0, 0);

    if (gMode == 0) {
        // 4 stacked bands top->bottom: L, R, G, B.
        float fb = i.uv.y * 4.0;
        uint band = min((uint)fb, 3u);
        float ly = frac(fb);             // 0 top .. 1 bottom of band
        float h = H(band, bin);
        if ((1.0 - ly) <= h) rgb = kC[band];
        // faint band separators
        if (ly < 0.012) rgb = max(rgb, float3(0.15, 0.15, 0.15));
    } else if (gMode == 2) {
        float h = H(0, bin);
        if ((1.0 - i.uv.y) <= h) rgb = float3(0.9, 0.9, 0.9);
    } else {
        for (uint ch = 1; ch <= 3; ++ch) {
            if (!(gChanMask & (1u << (ch - 1)))) continue;
            float h = H(ch, bin);
            if ((1.0 - i.uv.y) <= h) rgb += gColorize ? kC[ch] : float3(0.5, 0.5, 0.5);
        }
    }
    return float4(rgb, 1.0);
}
