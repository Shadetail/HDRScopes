#include "scope/CIEScope.h"
#include "scope/ChromaMath.h"
#include <vector>

namespace {
// CIE 1931 2-deg spectral locus (xy), ~ key wavelengths.
const double kLocus[][2] = {
    {0.1741,0.0050},{0.1440,0.0297},{0.1241,0.0578},{0.0913,0.1327},{0.0454,0.2950},
    {0.0082,0.5384},{0.0139,0.7502},{0.0743,0.8338},{0.1547,0.8059},{0.2296,0.7543},
    {0.3016,0.6923},{0.3731,0.6245},{0.4441,0.5547},{0.5125,0.4866},{0.5752,0.4242},
    {0.6270,0.3725},{0.6658,0.3340},{0.6915,0.3083},{0.7079,0.2920},{0.7190,0.2809},
    {0.7347,0.2653},
};
struct Tri { double r[2], g[2], b[2]; ImU32 col; const char* name; };
}

// xy -> plot[0,1]^2 matching the shader's MapPoint for the active diagram.
static ImVec2 XyToPlot(double x, double y, int mode, float minX, float maxX, float minY, float maxY) {
    if (mode == 1) return ImVec2((float)((x - minX) / (maxX - minX)), (float)(1.0 - (y - minY) / (maxY - minY)));
    double up, vp; chroma::xyToUv(x, y, up, vp);
    return ImVec2((float)((up - minX) / (maxX - minX)), (float)(1.0 - (vp - minY) / (maxY - minY)));
}

void CIEScope::DrawOverlay(ImDrawList* dl, const ScopeFrame& f, Settings& s) {
    PlotRange r = Range(s);
    const ImVec2 g0 = f.graphP0, g1 = f.graphP1;
    const float W = g1.x - g0.x, H = g1.y - g0.y;
    auto S = [&](ImVec2 p) { return ImVec2(g0.x + p.x * W, g0.y + p.y * H); };
    auto XY = [&](double x, double y) { return S(XyToPlot(x, y, r.mode, r.minX, r.maxX, r.minY, r.maxY)); };
    const ImU32 col = ImGui::GetColorU32(ImVec4(s.graticuleColor.x, s.graticuleColor.y, s.graticuleColor.z, s.graticuleOpacity * 0.8f));
    const ImU32 colText = ImGui::GetColorU32(ImVec4(0.82f, 0.82f, 0.82f, std::max(0.5f, s.graticuleOpacity)));

    // Spectral locus (closed with the line of purples).
    const int n = (int)(sizeof(kLocus) / sizeof(kLocus[0]));
    std::vector<ImVec2> loc; loc.reserve(n + 1);
    for (int i = 0; i < n; ++i) loc.push_back(XY(kLocus[i][0], kLocus[i][1]));
    loc.push_back(loc.front());
    dl->AddPolyline(loc.data(), (int)loc.size(), col, 0, 1.0f);

    // Gamut triangles.
    Tri tris[3] = {
        { {0.640,0.330},{0.300,0.600},{0.150,0.060}, IM_COL32(180,180,180,220), "Rec.709" },
        { {0.680,0.320},{0.265,0.690},{0.150,0.060}, IM_COL32(160,200,255,220), "P3" },
        { {0.708,0.292},{0.170,0.797},{0.131,0.046}, IM_COL32(120,255,160,220), "Rec.2020" },
    };
    bool show[3] = { s.cieShowRec709, s.cieShowP3, s.cieShowRec2020 };
    for (int i = 0; i < 3; ++i) {
        if (!show[i]) continue;
        ImVec2 R = XY(tris[i].r[0], tris[i].r[1]);
        ImVec2 G = XY(tris[i].g[0], tris[i].g[1]);
        ImVec2 B = XY(tris[i].b[0], tris[i].b[1]);
        dl->AddTriangle(R, G, B, tris[i].col, 1.4f);
        dl->AddText(ImVec2(G.x + 4, G.y - 4), tris[i].col, tris[i].name);
    }

    // D65 white point.
    ImVec2 wp = XY(0.3127, 0.3290);
    dl->AddCircle(wp, 3.5f, colText, 12, 1.5f);

    // Hover probe point.
    if (s.showHoverProbe && f.probeValid) {
        double X = 0.4123908 * f.probeRGB[0] + 0.3575843 * f.probeRGB[1] + 0.1804808 * f.probeRGB[2];
        double Y = 0.2126390 * f.probeRGB[0] + 0.7151687 * f.probeRGB[1] + 0.0721923 * f.probeRGB[2];
        double Z = 0.0193308 * f.probeRGB[0] + 0.1191948 * f.probeRGB[1] + 0.9505322 * f.probeRGB[2];
        double sum = X + Y + Z;
        if (sum > 1e-6) {
            ImVec2 p = XY(X / sum, Y / sum);
            dl->AddCircle(p, 5.0f, IM_COL32(255, 255, 255, 255), 0, 1.6f);
        }
    }
}

void CIEScope::DrawControls(Settings& s) {
    const char* diags[] = { "xy (1931)", "u'v' (1976)" };
    ImGui::SetNextItemWidth(150); ImGui::Combo("Diagram", &s.cieDiagram, diags, 2);
    ImGui::SliderFloat("Brightness", &s.cieGain, 0.005f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox("Colorize", &s.colorize);
    ImGui::Checkbox("Rec.709", &s.cieShowRec709); ImGui::SameLine();
    ImGui::Checkbox("P3", &s.cieShowP3); ImGui::SameLine();
    ImGui::Checkbox("Rec.2020", &s.cieShowRec2020);
}
