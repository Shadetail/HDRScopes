// PQ-spaced nit graticule drawn with ImGui over the graph image: decade major
// lines (1/10/100/1000/10000 nits), 10% minor ticks, nit labels, and the two
// draggable reference lines. The vertical mapping mirrors the shader exactly:
// scRGB = nits/80, pos01 = LinearToPQ(scRGB, 125), screen y = top + (1-pos01)*h.
#pragma once

#include "imgui.h"
#include <cmath>

namespace graticule {

// scRGB value mapping to 10,000 nits (top of axis).
constexpr double kMaxPQ = 125.0;

inline double LinearToPQ(double x, double maxPQValue) {
    const double N  = 2610.0 / 4096.0 / 4.0;
    const double M  = 2523.0 / 4096.0 * 128.0;
    const double C1 = 3424.0 / 4096.0;
    const double C2 = 2413.0 / 4096.0 * 32.0;
    const double C3 = 2392.0 / 4096.0 * 32.0;
    if (x <= 0.0) return 0.0;
    double xx = std::pow(x / maxPQValue, N);
    double nd = (C1 + C2 * xx) / (1.0 + C3 * xx);
    return std::pow(nd, M);
}

// 0..1 vertical position for a nit value (0 = bottom, 1 = top).
inline double NitsToPos01(double nits) {
    return LinearToPQ(nits / 80.0, kMaxPQ);
}

// Screen-space Y for a nit value, given the image's top/height in pixels.
inline float NitsToY(double nits, float top, float height) {
    double pos = NitsToPos01(nits);
    return top + (float)((1.0 - pos) * height);
}

// Convert a screen Y back to nits (for reference-line readout / dragging).
inline double YToNits(float y, float top, float height) {
    double pos01 = 1.0 - (double)(y - top) / (double)height;
    pos01 = pos01 < 0 ? 0 : (pos01 > 1 ? 1 : pos01);
    // Invert LinearToPQ then scale to nits.
    const double N  = 2610.0 / 4096.0 / 4.0;
    const double M  = 2523.0 / 4096.0 * 128.0;
    const double C1 = 3424.0 / 4096.0;
    const double C2 = 2413.0 / 4096.0 * 32.0;
    const double C3 = 2392.0 / 4096.0 * 32.0;
    double xp = std::pow(pos01, 1.0 / M);
    double num = std::fmax(xp - C1, 0.0);
    double den = (C2 - C3 * xp);
    double lin = (den != 0.0) ? std::pow(num / den, 1.0 / N) : 0.0;
    return lin * kMaxPQ * 80.0;
}

inline void DrawGraticule(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float opacity) {
    const float top = p0.y, h = p1.y - p0.y, left = p0.x, right = p1.x;
    const ImU32 colMajor = ImGui::GetColorU32(ImVec4(0.55f, 0.55f, 0.55f, opacity));
    const ImU32 colMinor = ImGui::GetColorU32(ImVec4(0.30f, 0.30f, 0.30f, opacity));
    const ImU32 colText  = ImGui::GetColorU32(ImVec4(0.80f, 0.80f, 0.80f, opacity));

    const double decades[] = { 1, 10, 100, 1000, 10000 };
    for (double dec : decades) {
        // Minor ticks at 2..9 within this decade, on the left edge.
        if (dec < 10000.0) {
            for (int m = 2; m <= 9; ++m) {
                double nits = dec * m;
                if (nits > 10000.0) break;
                float y = NitsToY(nits, top, h);
                dl->AddLine(ImVec2(left, y), ImVec2(left + 6.0f, y), colMinor, 1.0f);
                dl->AddLine(ImVec2(right - 6.0f, y), ImVec2(right, y), colMinor, 1.0f);
            }
        }
        // Major decade line across the full width.
        float y = NitsToY(dec, top, h);
        dl->AddLine(ImVec2(left, y), ImVec2(right, y), colMajor, 1.0f);

        char label[32];
        if (dec >= 1000.0) snprintf(label, sizeof(label), "%gk", dec / 1000.0);
        else               snprintf(label, sizeof(label), "%g", dec);
        dl->AddText(ImVec2(left + 8.0f, y - 14.0f), colText, label);
        dl->AddText(ImVec2(left + 2.0f, y + 1.0f), colText, "nits");
    }
}

// Draw one reference line at the given nits with a value readout.
inline void DrawRefLine(ImDrawList* dl, ImVec2 p0, ImVec2 p1, double nits, ImU32 col) {
    float y = NitsToY(nits, p0.y, p1.y - p0.y);
    dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), col, 1.5f);
    char label[48];
    snprintf(label, sizeof(label), "%.0f nits", nits);
    dl->AddText(ImVec2(p1.x - 70.0f, y - 14.0f), col, label);
}

} // namespace graticule
