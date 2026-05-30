#include "scope/VectorScope.h"
#include "scope/ChromaMath.h"
#include <cmath>

// Map ICtCp chroma -> plot [0,1]^2 with the same formula as the shader.
static ImVec2 IctcpToPlot(double r, double g, double b, float scale) {
    auto ic = chroma::Rec709toICtCp(r, g, b);
    return ImVec2((float)(0.5 + ic[1] * scale), (float)(0.5 - ic[2] * scale));
}

void VectorScope::DrawOverlay(ImDrawList* dl, const ScopeFrame& f, Settings& s) {
    const ImVec2 g0 = f.graphP0, g1 = f.graphP1;
    const float W = g1.x - g0.x, H = g1.y - g0.y;
    auto P = [&](ImVec2 p) {
        return ImVec2(g0.x + (p.x - f.panX) * f.zoom * W, g0.y + (p.y - f.panY) * f.zoom * H);
    };
    dl->PushClipRect(g0, g1, true);
    const ImU32 col = ImGui::GetColorU32(ImVec4(s.graticuleColor.x, s.graticuleColor.y, s.graticuleColor.z, s.graticuleOpacity));
    const ImU32 colText = ImGui::GetColorU32(ImVec4(0.82f, 0.82f, 0.82f, std::max(0.5f, s.graticuleOpacity)));

    ImVec2 c = P(ImVec2(0.5f, 0.5f));
    float radius = std::min(W, H) * 0.5f;
    dl->AddCircle(c, radius, col, 64, 1.0f);
    dl->AddLine(ImVec2(c.x - radius, c.y), ImVec2(c.x + radius, c.y), col, 1.0f);
    dl->AddLine(ImVec2(c.x, c.y - radius), ImVec2(c.x, c.y + radius), col, 1.0f);

    // Primary/secondary targets (approximate, at a reference intensity).
    const double v = 2.0;
    struct T { double r, g, b; const char* n; ImU32 c; };
    const T targets[] = {
        { v, 0, 0, "R", IM_COL32(255,80,80,255) },
        { v, v, 0, "Yl", IM_COL32(230,230,80,255) },
        { 0, v, 0, "G", IM_COL32(80,230,80,255) },
        { 0, v, v, "Cy", IM_COL32(80,230,230,255) },
        { 0, 0, v, "B", IM_COL32(110,140,255,255) },
        { v, 0, v, "Mg", IM_COL32(230,110,230,255) },
    };
    for (auto& t : targets) {
        ImVec2 p = P(IctcpToPlot(t.r, t.g, t.b, kScale));
        dl->AddRect(ImVec2(p.x - 5, p.y - 5), ImVec2(p.x + 5, p.y + 5), t.c, 0, 0, 1.5f);
        dl->AddText(ImVec2(p.x + 6, p.y - 6), colText, t.n);
    }

    // Skin-tone line (from centre toward a typical skin chroma direction).
    if (s.vectorShowSkin) {
        ImVec2 skin = IctcpToPlot(2.0, 1.2, 0.9, kScale);
        ImVec2 dir = ImVec2(skin.x - 0.5f, skin.y - 0.5f);
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > 1e-4f) {
            ImVec2 e = ImVec2(c.x + dir.x / len * radius, c.y + dir.y / len * radius);
            dl->AddLine(c, e, IM_COL32(255, 180, 140, 220), 1.5f);
        }
    }

    // Hover probe point.
    if (s.showHoverProbe && f.probeValid) {
        ImVec2 p = P(IctcpToPlot(f.probeRGB[0], f.probeRGB[1], f.probeRGB[2], kScale));
        dl->AddCircle(p, 5.0f, IM_COL32(255, 255, 255, 255), 0, 1.6f);
    }
    dl->PopClipRect();
}

void VectorScope::DrawControls(Settings& s) {
    ImGui::SliderFloat("Brightness", &s.vectorGain, 0.005f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox("Colorize", &s.colorize);
    ImGui::Checkbox("Skin tone line", &s.vectorShowSkin);
}
