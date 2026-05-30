// Renders the chroma accumulation grid. Colorize tints each point by its hue
// (vectorscope) or by the chromaticity's approximate sRGB colour (CIE).
#include "colorspaces.hlsli"

cbuffer GCB : register(b0)
{
    uint  gSize, gMode, gColorize, gExtents;
    float gGain, gMinX, gMaxX, gMinY;
    float gMaxY, gScale, gUVScaleX, gUVScaleY;
    float gUVOffX, gUVOffY, gDotRadius, gExtentsOpacity;
};
Texture2D<uint> gAccum : register(t0);

// Max accumulation count in a square neighbourhood (grid space).
uint MaxCount(int2 c, int rad)
{
    uint m = 0;
    for (int dy = -rad; dy <= rad; ++dy)
    for (int dx = -rad; dx <= rad; ++dx) {
        int2 q = clamp(c + int2(dx, dy), int2(0, 0), int2(gSize - 1, gSize - 1));
        m = max(m, gAccum.Load(int3(q, 0)));
    }
    return m;
}

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VSMain(uint vid : SV_VertexID) {
    VSOut o; float2 uv = float2((vid << 1) & 2, vid & 2);
    o.uv = uv; o.pos = float4(uv.x * 2 - 1, 1 - uv.y * 2, 0, 1); return o;
}

// Reconstruct the true vectorscope colour at a plot position from its Cb/Cr,
// so a point lands on its target AND shows that target's colour (like DaVinci).
float3 VectorColor(float2 uv)
{
    float Cb = (uv.x - 0.5) / max(gScale, 1e-4);
    float Cr = (0.5 - uv.y) / max(gScale, 1e-4);
    float Y = 0.5;
    float r = Y + 1.5748 * Cr;
    float b = Y + 1.8556 * Cb;
    float g = (Y - 0.2126 * r - 0.0722 * b) / 0.7152;
    float3 rgb = max(float3(r, g, b), 0.0);
    rgb /= max(max(rgb.r, max(rgb.g, rgb.b)), 1e-3);
    return saturate(rgb);
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
    // Apply the pan/zoom transform (sampled_uv = off + screen_uv * scale).
    float2 uv = float2(gUVOffX, gUVOffY) + i.uv * float2(gUVScaleX, gUVScaleY);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return float4(0, 0, 0, 1);
    int2 px = int2(uv.x * gSize, uv.y * gSize);
    // Dilate by gDotRadius px in grid space so points are visible (size adjustable).
    int R = (int)(gDotRadius + 0.5);
    uint count = MaxCount(px, R);
    float a = saturate(1.0 - exp(-(float)count * gGain));

    float3 rgb = float3(0, 0, 0);
    if (a > 0.0) {
        float3 col = gColorize ? ((gMode == 0) ? VectorColor(uv) : CIEColor(uv))
                               : float3(0.85, 0.95, 0.85);
        rgb = col * a;
    }

    // Extents: a thin outline ring just OUTSIDE the occupied region, marking the
    // reach (gamut) of the signal. Uses its own occupancy radius (independent of
    // the cosmetic dot size) with a small minimum so sparse clouds still read as
    // a connected envelope rather than a ring around every isolated point.
    if (gExtents && gExtentsOpacity > 0.0) {
        int eR = max(R, 2);
        bool here = MaxCount(px, eR) > 0u;
        bool near = MaxCount(px, eR + 1) > 0u;
        if (!here && near)
            rgb = max(rgb, float3(1, 1, 1) * gExtentsOpacity);
    }

    if (all(rgb <= 0.0)) return float4(0, 0, 0, 1);
    return float4(rgb, 1.0);
}
