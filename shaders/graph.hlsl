// Render-side resolve: samples the uint bins texture and produces the waveform
// trace. Normalization (gain/brightness) and the pan/zoom/stretch transform all
// live here, so changing them never re-dispatches the histogram.
//
// Output is scRGB-ish linear intensity written into the graph's offscreen RT.

cbuffer GraphCB : register(b0)
{
    uint  gGraphCols;
    uint  gBins;
    uint  gChannels;   // 1 = luma, 3 = RGB
    uint  gMode;       // 0 = luma, 1 = RGB

    float gGain;       // brightness / accumulation gain
    uint  gExtents;    // 1 = draw over/undershoot outline
    float2 gUVScale;   // pan/zoom: sampled_uv = gUVOffset + uv * gUVScale

    float2 gUVOffset;
    float2 _pad;
};

Texture2D<uint> gBinsTex    : register(t0); // (gGraphCols, gBins * gChannels)
Texture2D<uint> gExtentsTex : register(t1); // (gGraphCols, 2 * gChannels)

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

// Fullscreen triangle, uv (0,0) top-left .. (1,1) bottom-right.
VSOut VSMain(uint vid : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((vid << 1) & 2, vid & 2); // (0,0),(2,0),(0,2)
    o.uv  = uv;
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return o;
}

static const float3 kLumaColor = float3(0.85, 1.0, 0.85);
static const float3 kChanColor[3] = {
    float3(1.0, 0.12, 0.12),
    float3(0.12, 1.0, 0.12),
    float3(0.20, 0.45, 1.0),
};

float Intensity(uint count)
{
    // Soft accumulation curve: builds up like a real waveform monitor.
    return saturate(1.0 - exp(-(float)count * gGain));
}

float4 PSMain(VSOut i) : SV_Target
{
    float2 uv = gUVOffset + i.uv * gUVScale;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return float4(0, 0, 0, 1);

    uint col = (uint)(uv.x * (gGraphCols - 1) + 0.5);
    // Screen y=0 is top of axis (10,000 nits); bin index grows with nits.
    int  bin = (int)((1.0 - uv.y) * (gBins - 1) + 0.5);
    bin = clamp(bin, 0, (int)gBins - 1);

    float3 rgb = float3(0, 0, 0);
    uint chN = (gMode == 0) ? 1u : 3u;
    for (uint ch = 0; ch < chN; ++ch) {
        uint count = gBinsTex.Load(int3(col, ch * gBins + bin, 0));
        float v = Intensity(count);
        float3 color = (gMode == 0) ? kLumaColor : kChanColor[ch];
        rgb += color * v;

        if (gExtents) {
            uint lo = gExtentsTex.Load(int3(col, 2 * ch + 0, 0));
            uint hi = gExtentsTex.Load(int3(col, 2 * ch + 1, 0));
            // Thin outline at the min and max excursions of this column.
            if (lo <= hi && ((uint)bin == lo || (uint)bin == hi))
                rgb += color * 0.5;
        }
    }
    return float4(rgb, 1.0);
}
