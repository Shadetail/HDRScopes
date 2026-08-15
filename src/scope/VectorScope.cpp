#include "scope/VectorScope.h"
#include "scope/ChromaMath.h"
#include "util/UiReset.h"
#include <cmath>

// Rec.709 OETF (sign/range preserving) — mirrors the compute shader.
static double OETF709(double x) {
    double a = std::fabs(x);
    double v = (a < 0.018) ? (4.5 * a) : (1.0993 * std::pow(a, 0.45) - 0.0993);
    return (x < 0 ? -v : v);
}

// scRGB (with given SDR-white) -> Y'CbCr plot point [0,1]^2, matching chroma_cs.
static ImVec2 ScrgbToPlot(double r, double g, double b, double sdrNorm, float scale) {
    double rp = OETF709(r / sdrNorm), gp = OETF709(g / sdrNorm), bp = OETF709(b / sdrNorm);
    double cb, cr; chroma::Rec709YCbCr(rp, gp, bp, cb, cr);
    return ImVec2((float)(0.5 + cb * scale), (float)(0.5 - cr * scale));
}

void VectorScope::DrawOverlay(ImDrawList* dl, const ScopeFrame& f, Settings& s) {
    const ImVec2 g0 = f.graphP0, g1 = f.graphP1;
    const float W = g1.x - g0.x, H = g1.y - g0.y;
    auto P = [&](ImVec2 p) {
        return ImVec2(g0.x + (p.x - f.panX) * f.zoom * W, g0.y + (p.y - f.panY) * f.zoom * H);
    };
    const ImU32 col = ImGui::GetColorU32(ImVec4(s.graticuleColor.x, s.graticuleColor.y, s.graticuleColor.z, s.graticuleOpacity));
    const ImU32 colText = ImGui::GetColorU32(ImVec4(0.82f, 0.82f, 0.82f, std::max(0.5f, s.graticuleOpacity)));
    dl->PushClipRect(g0, g1, true);

    ImVec2 c = P(ImVec2(0.5f, 0.5f));
    float radius = std::min(W, H) * 0.5f * f.zoom;
    dl->AddCircle(c, radius, col, 96, 1.0f);
    dl->AddLine(ImVec2(c.x - radius, c.y), ImVec2(c.x + radius, c.y), col, 1.0f);
    dl->AddLine(ImVec2(c.x, c.y - radius), ImVec2(c.x, c.y + radius), col, 1.0f);

    // Exact Rec.709 75% color-bar targets (Cb,Cr from the standard matrix).
    struct T { double rp, gp, bp; const char* n; ImU32 c; };
    const T prim[] = {
        { 1, 0, 0, "R", IM_COL32(255,90,90,255) },
        { 1, 1, 0, "Yl", IM_COL32(230,230,90,255) },
        { 0, 1, 0, "G", IM_COL32(90,230,90,255) },
        { 0, 1, 1, "Cy", IM_COL32(90,230,230,255) },
        { 0, 0, 1, "B", IM_COL32(120,150,255,255) },
        { 1, 0, 1, "Mg", IM_COL32(230,120,230,255) },
    };
    const double sat = 0.75; // DaVinci target boxes sit at 75%
    for (auto& t : prim) {
        double cb, cr; chroma::Rec709YCbCr(t.rp, t.gp, t.bp, cb, cr);
        ImVec2 p = P(ImVec2((float)(0.5 + cb * sat * s.vectorScale), (float)(0.5 - cr * sat * s.vectorScale)));
        // DaVinci-style corner-bracket box.
        float b = 6.0f;
        dl->AddLine(ImVec2(p.x - b, p.y - b), ImVec2(p.x - b * 0.4f, p.y - b), t.c, 1.4f);
        dl->AddLine(ImVec2(p.x - b, p.y - b), ImVec2(p.x - b, p.y - b * 0.4f), t.c, 1.4f);
        dl->AddLine(ImVec2(p.x + b, p.y + b), ImVec2(p.x + b * 0.4f, p.y + b), t.c, 1.4f);
        dl->AddLine(ImVec2(p.x + b, p.y + b), ImVec2(p.x + b, p.y + b * 0.4f), t.c, 1.4f);
        dl->AddText(ImVec2(p.x + 7, p.y - 7), colText, t.n);
    }

    // Skin-tone / I-line at a configurable angle (math convention, Y up).
    if (s.vectorShowSkin) {
        double a = s.vectorSkinAngleDeg * 3.14159265358979 / 180.0;
        ImVec2 e = ImVec2(c.x + (float)std::cos(a) * radius, c.y - (float)std::sin(a) * radius);
        dl->AddLine(c, e, IM_COL32(255, 190, 150, 220), 1.4f);
    }

    // Hover probe point.
    if (s.showHoverProbe && f.probeValid) {
        ImVec2 p = P(ScrgbToPlot(f.probeRGB[0], f.probeRGB[1], f.probeRGB[2], f.sdrWhiteNits / 80.0, s.vectorScale));
        dl->AddCircle(p, s.hoverCircleRadius, IM_COL32(255, 255, 255, 255), 0, 1.6f);
    }
    dl->PopClipRect();
}

void VectorScope::DrawControls(Settings& s) {
    ImGui::SliderFloat("Brightness", &s.vectorGain, 0.00005f, 1.0f, "%.5f", ImGuiSliderFlags_Logarithmic);
    UiResetSlider(s.vectorGain, UiDefaults().vectorGain);
    ImGui::SliderFloat("Scale", &s.vectorScale, 0.06f, 1.0f, "%.2f");
    UiResetSlider(s.vectorScale, UiDefaults().vectorScale);
    ImGui::Checkbox("Colorize", &s.vectorColorize);
    UiResetToggle(s.vectorColorize, UiDefaults().vectorColorize);
    ImGui::SliderInt("Dot size", &s.chromaDotRadius, 0, 6);
    UiResetSlider(s.chromaDotRadius, UiDefaults().chromaDotRadius);
    ImGui::Checkbox("Extents (gamut outline)", &s.vectorExtents);
    UiResetToggle(s.vectorExtents, UiDefaults().vectorExtents);
    if (s.vectorExtents) {
        ImGui::SameLine(); ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("##exop", &s.vectorExtentsOpacity, 0.0f, 1.0f, "opacity %.2f");
        UiResetSlider(s.vectorExtentsOpacity, UiDefaults().vectorExtentsOpacity);
    }
    ImGui::Checkbox("Skin tone line", &s.vectorShowSkin);
    UiResetToggle(s.vectorShowSkin, UiDefaults().vectorShowSkin);
    if (s.vectorShowSkin) {
        ImGui::SliderFloat("Skin angle", &s.vectorSkinAngleDeg, 90.0f, 160.0f, "%.1f deg");
        UiResetSlider(s.vectorSkinAngleDeg, UiDefaults().vectorSkinAngleDeg);
    }
}
