// Waveform render-side resolve. Samples the uint bins texture, applies gain,
// colorize, channel mask, the pan/zoom transform and the SDR-white vertical
// axis scaling. Extents "points" style (0) is drawn here; the "white line"
// style is drawn CPU-side as a polyline. In luminance mode, colorize tints the
// trace by the real source colour accumulated per cell (Resolve-style).

cbuffer GraphCB : register(b0)
{
    uint  gGraphCols, gBins, gChannels, gMode;   // mode 0=luma,1=rgb
    float gGain; uint gColorize; uint gExtentsPoints; uint gChanMask;
    float gUVScaleX, gUVScaleY, gUVOffX, gUVOffY;
    float gYAxisTop01; uint gExtentCols; float gExtentsOpacity, _p1;
};

Texture2D<uint> gBinsTex    : register(t0); // (gGraphCols, gBins * gChannels)
Texture2D<uint> gExtentsTex : register(t1); // (gExtentCols, 2 * gChannels)
Texture2D<uint> gColorTex   : register(t2); // (gGraphCols, gBins * 3) luma colour sums

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint vid : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((vid << 1) & 2, vid & 2);
    o.uv = uv;
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return o;
}

static const float3 kChan[3] = {
    float3(1.0, 0.10, 0.10), float3(0.10, 1.0, 0.10), float3(0.25, 0.5, 1.0)
};
static const float3 kLumaGreen = float3(0.30, 1.0, 0.45);
static const float3 kWhite     = float3(1.0, 1.0, 1.0);

float Intensity(uint count) { return saturate(1.0 - exp(-(float)count * gGain)); }

float3 ChannelColor(uint ch)
{
    if (gMode == 0) return gColorize ? kLumaGreen : kWhite;           // luminance
    return gColorize ? kChan[ch] : float3(0.85, 0.85, 0.85);          // rgb
}

float4 PSMain(VSOut i) : SV_Target
{
    float2 uv = float2(gUVOffX, gUVOffY) + i.uv * float2(gUVScaleX, gUVScaleY);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return float4(0, 0, 0, 1);

    uint col = min((uint)(uv.x * gGraphCols), gGraphCols - 1);
    // Screen y=0 is top of the (possibly SDR-white-scaled) axis.
    float fullPos = (1.0 - uv.y) * gYAxisTop01;
    int   bin = (int)(fullPos * (gBins - 1) + 0.5);
    if (bin < 0 || bin >= (int)gBins) return float4(0, 0, 0, 1);

    uint extCol = min((uint)(uv.x * gExtentCols), gExtentCols - 1);

    float3 rgb = float3(0, 0, 0);
    uint chN = (gMode == 0) ? 1u : 3u;
    for (uint ch = 0; ch < chN; ++ch) {
        if (gMode == 1 && !(gChanMask & (1u << ch))) continue;
        uint count = gBinsTex.Load(int3(col, ch * gBins + bin, 0));

        float3 traceCol;
        if (gMode == 0 && gColorize) {
            // Average source colour accumulated at this cell (normalized, /1023).
            float sr = gColorTex.Load(int3(col, 0 * gBins + bin, 0));
            float sg = gColorTex.Load(int3(col, 1 * gBins + bin, 0));
            float sb = gColorTex.Load(int3(col, 2 * gBins + bin, 0));
            float n = max((float)count, 1.0);
            traceCol = saturate(float3(sr, sg, sb) / (n * 1023.0));
        } else {
            traceCol = ChannelColor(ch);
        }
        rgb += traceCol * Intensity(count);

        if (gExtentsPoints) {
            uint lo = gExtentsTex.Load(int3(extCol, 2 * ch + 0, 0));
            uint hi = gExtentsTex.Load(int3(extCol, 2 * ch + 1, 0));
            if (lo <= hi && ((uint)bin == lo || (uint)bin == hi))
                rgb += ChannelColor(ch) * 0.6 * gExtentsOpacity;
        }
    }
    return float4(rgb, 1.0);
}
