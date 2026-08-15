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

// Per-channel encode matching chroma_cs: PQ (absolute nits, sign preserving)
// or Rec.709 gamma normalized to SDR white.
static double Encode(double x, double sdrNorm, bool usePQ) {
    if (usePQ) {
        double v = pq::LinearToPQ(std::fabs(x));
        return (x < 0 ? -v : v);
    }
    return OETF709(x / std::max(sdrNorm, 1e-4));
}

// scRGB (with given SDR-white) -> Y'CbCr plot point [0,1]^2, matching chroma_cs.
static ImVec2 ScrgbToPlot(double r, double g, double b, double sdrNorm, bool usePQ, float scale) {
    double rp = Encode(r, sdrNorm, usePQ), gp = Encode(g, sdrNorm, usePQ), bp = Encode(b, sdrNorm, usePQ);
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

    // Exact Rec.709 color-bar targets (Cb,Cr from the standard matrix): labelled
    // brackets at 75% amplitude (where 75% bars land), smaller ones at 100%.
    // Amplitudes are positions in Cb/Cr space, so they hold for both encodings —
    // what changes with the encoding is which signal reaches them.
    struct T { double rp, gp, bp; const char* n; ImU32 c; };
    const T prim[] = {
        { 1, 0, 0, "R", IM_COL32(255,90,90,255) },
        { 1, 1, 0, "Yl", IM_COL32(230,230,90,255) },
        { 0, 1, 0, "G", IM_COL32(90,230,90,255) },
        { 0, 1, 1, "Cy", IM_COL32(90,230,230,255) },
        { 0, 0, 1, "B", IM_COL32(120,150,255,255) },
        { 1, 0, 1, "Mg", IM_COL32(230,120,230,255) },
    };
    for (auto& t : prim) {
        double cb, cr; chroma::Rec709YCbCr(t.rp, t.gp, t.bp, cb, cr);
        for (double sat : { 0.75, 1.0 }) {
            ImVec2 p = P(ImVec2((float)(0.5 + cb * sat * s.vectorScale), (float)(0.5 - cr * sat * s.vectorScale)));
            // DaVinci-style corner-bracket box.
            float b = (sat == 0.75) ? 6.0f : 4.5f;
            dl->AddLine(ImVec2(p.x - b, p.y - b), ImVec2(p.x - b * 0.4f, p.y - b), t.c, 1.4f);
            dl->AddLine(ImVec2(p.x - b, p.y - b), ImVec2(p.x - b, p.y - b * 0.4f), t.c, 1.4f);
            dl->AddLine(ImVec2(p.x + b, p.y + b), ImVec2(p.x + b * 0.4f, p.y + b), t.c, 1.4f);
            dl->AddLine(ImVec2(p.x + b, p.y + b), ImVec2(p.x + b, p.y + b * 0.4f), t.c, 1.4f);
            if (sat == 0.75) dl->AddText(ImVec2(p.x + 7, p.y - 7), colText, t.n);
        }
    }

    // PQ mode: dots where 100% SDR primaries land at the current SDR-white level
    // (well inside the fixed targets — orientation aid, nonstandard).
    if (s.vectorPQ && s.vectorSdrMarkers) {
        const double sdrNorm = f.sdrWhiteNits / 80.0;
        for (auto& t : prim) {
            ImVec2 p = P(ScrgbToPlot(t.rp * sdrNorm, t.gp * sdrNorm, t.bp * sdrNorm, 1.0, true, s.vectorScale));
            dl->AddCircleFilled(p, 2.0f, (t.c & 0x00FFFFFF) | 0xA0000000);
        }
    }

    // Skin-tone / I-line at a configurable angle (math convention, Y up).
    if (s.vectorShowSkin) {
        double a = s.vectorSkinAngleDeg * 3.14159265358979 / 180.0;
        ImVec2 e = ImVec2(c.x + (float)std::cos(a) * radius, c.y - (float)std::sin(a) * radius);
        dl->AddLine(c, e, IM_COL32(255, 190, 150, 220), 1.4f);
    }

    // Hover probe point.
    if (s.showHoverProbe && f.probeValid) {
        ImVec2 p = P(ScrgbToPlot(f.probeRGB[0], f.probeRGB[1], f.probeRGB[2], f.sdrWhiteNits / 80.0, s.vectorPQ, s.vectorScale));
        dl->AddCircle(p, s.hoverCircleRadius, IM_COL32(255, 255, 255, 255), 0, 1.6f);
    }
    dl->PopClipRect();
}

void VectorScope::DrawControls(Settings& s) {
    int enc = s.vectorPQ ? 1 : 0;
    if (ImGui::Combo("Encoding", &enc, "Rec.709 (SDR)\0PQ (HDR)\0")) s.vectorPQ = (enc == 1);
    UiReset(s.vectorPQ, UiDefaults().vectorPQ);
    if (s.vectorPQ) {
        ImGui::Checkbox("SDR primary markers", &s.vectorSdrMarkers);
        UiReset(s.vectorSdrMarkers, UiDefaults().vectorSdrMarkers);
    }
    ImGui::SliderFloat("Brightness", &s.vectorGain, 0.00005f, 1.0f, "%.5f", ImGuiSliderFlags_Logarithmic);
    UiReset(s.vectorGain, UiDefaults().vectorGain);
    ImGui::SliderFloat("Scale", &s.vectorScale, 0.06f, 1.0f, "%.2f");
    UiReset(s.vectorScale, UiDefaults().vectorScale);
    ImGui::Checkbox("Colorize", &s.vectorColorize);
    UiReset(s.vectorColorize, UiDefaults().vectorColorize);
    ImGui::SliderInt("Dot size", &s.chromaDotRadius, 0, 6);
    UiReset(s.chromaDotRadius, UiDefaults().chromaDotRadius);
    ImGui::Checkbox("Extents (gamut outline)", &s.vectorExtents);
    UiReset(s.vectorExtents, UiDefaults().vectorExtents);
    if (s.vectorExtents) {
        ImGui::SameLine(); ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("##exop", &s.vectorExtentsOpacity, 0.0f, 1.0f, "opacity %.2f");
        UiReset(s.vectorExtentsOpacity, UiDefaults().vectorExtentsOpacity);
    }
    ImGui::Checkbox("Skin tone line", &s.vectorShowSkin);
    UiReset(s.vectorShowSkin, UiDefaults().vectorShowSkin);
    if (s.vectorShowSkin) {
        ImGui::SliderFloat("Skin angle", &s.vectorSkinAngleDeg, 90.0f, 160.0f, "%.1f deg");
        UiReset(s.vectorSkinAngleDeg, UiDefaults().vectorSkinAngleDeg);
    }
}
