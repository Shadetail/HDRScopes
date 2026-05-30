// Renders the chroma accumulation grid. Colorize tints each point by its hue
// (vectorscope) or by the chromaticity's approximate sRGB colour (CIE).
#include "colorspaces.hlsli"

cbuffer GCB : register(b0)
{
    uint  gSize, gMode, gColorize, _p0;
    float gGain, gMinX, gMaxX, gMinY;
    float gMaxY, gScale, _p1, _p2;
};
Texture2D<uint> gAccum : register(t0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VSMain(uint vid : SV_VertexID) {
    VSOut o; float2 uv = float2((vid << 1) & 2, vid & 2);
    o.uv = uv; o.pos = float4(uv.x * 2 - 1, 1 - uv.y * 2, 0, 1); return o;
}

float3 HueWheel(float2 uv)
{
    float2 d = uv - 0.5;
    float ang = atan2(-d.y, d.x);                 // screen y down
    float h = (ang / 6.2831853 + 1.0);
    h = frac(h);
    float3 k = frac(h + float3(0.0, 2.0 / 3.0, 1.0 / 3.0));
    return saturate(abs(k * 6.0 - 3.0) - 1.0);
}

float3 CIEColor(float2 uv)
{
    // Reconstruct chromaticity, assume Y=1, to sRGB-ish for tinting.
    float px = uv.x, py = uv.y;
    float x, y;
    if (gMode == 1) { x = gMinX + px * (gMaxX - gMinX); y = gMinY + (1.0 - py) * (gMaxY - gMinY); }
    else {
        float up = gMinX + px * (gMaxX - gMinX);
        float vp = gMinY + (1.0 - py) * (gMaxY - gMinY);
        float den = (6.0 * up - 16.0 * vp + 12.0);
        x = 9.0 * up / den; y = 4.0 * vp / den;
    }
    if (y <= 1e-4) return float3(0.2, 0.2, 0.2);
    float3 XYZ = float3(x / y, 1.0, (1.0 - x - y) / y);
    float3 rgb = XYZ_to_Rec709(XYZ);
    rgb = max(rgb, 0.0);
    rgb /= max(max(rgb.r, max(rgb.g, rgb.b)), 1e-3);
    return saturate(rgb);
}

float4 PSMain(VSOut i) : SV_Target
{
    int2 px = int2(i.uv.x * gSize, i.uv.y * gSize);
    // Dilate by 1px in grid space so sparse points stay visible.
    uint count = 0;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx) {
        int2 q = clamp(px + int2(dx, dy), int2(0, 0), int2(gSize - 1, gSize - 1));
        count = max(count, gAccum.Load(int3(q, 0)));
    }
    float a = saturate(1.0 - exp(-(float)count * gGain));
    if (a <= 0.0) return float4(0, 0, 0, 1);
    float3 col;
    if (gColorize) col = (gMode == 0) ? HueWheel(i.uv) : CIEColor(i.uv);
    else           col = float3(0.85, 0.95, 0.85);
    return float4(col * a, 1.0);
}
