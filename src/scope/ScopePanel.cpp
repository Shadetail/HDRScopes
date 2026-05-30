#include "scope/ScopePanel.h"
#include "scope/ScopeFactory.h"
#include <algorithm>
#include <cmath>

void ScopePanel::Draw(int index, const ImVec2& panelP0, const ImVec2& panelP1,
                      const ScopeInput& input, Settings& s, float sdrWhiteNits,
                      const ScopeFrame& probe, float uiBrightness) {
    ScopeType want = s.panelScope[index];
    if (!scope_ || type_ != want) {
        if (scope_) scope_->Shutdown();
        scope_ = CreateScope(want);
        if (scope_) scope_->Init(device_);
        type_ = want;
    }
    if (!scope_) return;

    // Compute the histogram for this frame.
    scope_->Compute(input, s);

    // Lay out the graph rect inside the panel (reserve margins).
    Margins m = scope_->GetMargins(s);
    ImVec2 g0(panelP0.x + m.l, panelP0.y + m.t);
    ImVec2 g1(panelP1.x - m.r, panelP1.y - m.b);
    float gw = g1.x - g0.x, gh = g1.y - g0.y;
    float aspect = scope_->AspectRatio();
    if (aspect > 0.0f && gw > 0 && gh > 0) {
        // Centered square (or aspect) graph.
        float side = std::min(gw, gh * aspect);
        float w = side, h = side / aspect;
        float cx = (g0.x + g1.x) * 0.5f, cy = (g0.y + g1.y) * 0.5f;
        g0 = ImVec2(cx - w * 0.5f, cy - h * 0.5f);
        g1 = ImVec2(cx + w * 0.5f, cy + h * 0.5f);
        gw = w; gh = h;
    }
    if (gw < 8 || gh < 8) return;

    UINT pw = (UINT)gw, ph = (UINT)gh;

    ScopeFrame f;
    f.graphP0 = g0; f.graphP1 = g1;
    f.zoom = std::max(1.0f, s.zoom[index]);
    f.panX = s.panX[index]; f.panY = s.panY[index];
    f.sdrWhiteNits = sdrWhiteNits;
    f.probeValid = probe.probeValid;
    f.probeRGB[0] = probe.probeRGB[0]; f.probeRGB[1] = probe.probeRGB[1]; f.probeRGB[2] = probe.probeRGB[2];
    f.probeU = probe.probeU; f.probeV = probe.probeV;

    scope_->Render(pw, ph, f, s);

    // Draw the graph image. Tint cancels the global UI brightness so the graph
    // keeps its true scRGB/HDR values.
    float t = (uiBrightness > 0.0001f) ? 1.0f / uiBrightness : 1.0f;
    ImU32 tint = ImGui::GetColorU32(ImVec4(t, t, t, 1.0f));
    ImGui::GetWindowDrawList()->AddImage((ImTextureID)scope_->Result(), g0, g1,
                                         ImVec2(0, 0), ImVec2(1, 1), tint);

    // Overlay (graticule, labels, probe, interaction).
    ImDrawList* dl = ImGui::GetWindowDrawList();
    scope_->DrawOverlay(dl, f, s);

    // Input: zoom-to-mouse + middle-drag pan over the graph. Require the host
    // window to be the hovered one so the controls popup doesn't pass through.
    ImGuiIO& io = ImGui::GetIO();
    bool hov = ImGui::IsMouseHoveringRect(g0, g1) && ImGui::IsWindowHovered();
    if (hov && io.MouseWheel != 0.0f) {
        float z0 = std::max(1.0f, s.zoom[index]);
        float z1 = std::clamp(z0 * std::exp(io.MouseWheel * 0.15f), 1.0f, 64.0f);
        float s0 = 1.0f / z0, s1 = 1.0f / z1;
        float cux = (io.MousePos.x - g0.x) / gw;
        float cuy = (io.MousePos.y - g0.y) / gh;
        s.panX[index] += cux * (s0 - s1);
        s.panY[index] += cuy * (s0 - s1);
        s.zoom[index] = z1;
    }
    if (hov && io.MouseDown[2]) {
        float sc = 1.0f / std::max(1.0f, s.zoom[index]);
        s.panX[index] -= io.MouseDelta.x / gw * sc;
        s.panY[index] -= io.MouseDelta.y / gh * sc;
    }
    float span = 1.0f / std::max(1.0f, s.zoom[index]);
    s.panX[index] = std::clamp(s.panX[index], 0.0f, std::max(0.0f, 1.0f - span));
    s.panY[index] = std::clamp(s.panY[index], 0.0f, std::max(0.0f, 1.0f - span));
}
