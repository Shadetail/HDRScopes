// HDRScopes — real-time scopes for the live Windows HDR signal.
//
// Single-window UI: the scope(s) fill the client area; a gear button opens the
// controls popup; layout presets (1/2/4-up) live top-right; FPS and zoom sit in
// the bottom corners. Capture -> per-panel scope (compute+render) -> overlay.

#include "util/Common.h"
#include "util/Settings.h"
#include "util/SdrWhite.h"
#include "util/Format.h"
#include "util/UiReset.h"
#include "app/D3DContext.h"
#include "capture/CaptureSource.h"
#include "capture/Region.h"
#include "capture/RegionPicker.h"
#include "capture/PixelProbe.h"
#include "compute/TestPattern.h"
#include "compute/Blur.h"
#include "compute/PeakMeter.h"
#include "scope/ScopePanel.h"
#include "scope/ScopeFactory.h"
#include "resource.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <timeapi.h>
#include <algorithm>

// Widen the narrow HDRSCOPES_VERSION macro from CMake for the window title.
#define HS_WIDEN2(x) L##x
#define HS_WIDEN(x) HS_WIDEN2(x)
#include <cmath>
#include <cstring>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// Scope-type picker (the four implemented scopes).
static const ScopeType kScopeOrder[4] = { ScopeType::Waveform, ScopeType::Histogram, ScopeType::Vectorscope, ScopeType::CIE };
static const char*     kScopeNames[4] = { "Waveform", "Histogram", "Vectorscope", "CIE Chromaticity" };

// Width that fits the combo's current selection (preview text + arrow), so the
// floating pickers only cover as much of the scope as they need. The open
// popup still auto-sizes to the longest entry.
static float ScopeComboWidth(ScopeType st) {
    const char* name = kScopeNames[0];
    for (int i = 0; i < 4; ++i) if (kScopeOrder[i] == st) name = kScopeNames[i];
    return ImGui::CalcTextSize(name).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight();
}

static bool ScopeCombo(const char* id, ScopeType& st, bool fitWidth = false) {
    int cur = 0;
    for (int i = 0; i < 4; ++i) if (kScopeOrder[i] == st) cur = i;
    if (fitWidth) ImGui::SetNextItemWidth(ScopeComboWidth(st));
    if (ImGui::Combo(id, &cur, kScopeNames, 4)) { st = kScopeOrder[cur]; return true; }
    return false;
}

namespace {
D3DContext    g_d3d;
CaptureSource g_capture;
TestPattern   g_test;
Blur          g_blur;
PixelProbe    g_probe;
PeakMeter     g_peaks;
Settings      g_set;
ScopePanel    g_panels[4];
HWND          g_hwnd = nullptr;
int           g_lastCropW = 0, g_lastCropH = 0; // for the quality readout
bool          g_resize = false; UINT g_resizeW = 0, g_resizeH = 0;
bool          g_showControls = false;
float         g_sdrWhiteNits = 200.0f;    // captured monitor (scope math)
float         g_uiSdrWhiteNits = 200.0f;  // window's monitor (UI brightness)
ULONGLONG     g_lastSdrQuery = 0;
HWND          g_pickedWindow = nullptr;
ULONGLONG     g_pickArmUntil = 0;
float         g_uiScaleOverride = -1.0f;  // HDRSCOPES_UISCALE env (testing)
float         g_pendingUiScale = -1.0f;   // from WM_DPICHANGED; applied between frames
bool          g_controlsRescale = false;  // snap the controls popup to the new scale
float         g_ctrlAlpha = 1.0f;         // floating-controls fade (1 = fully visible)
}

// Rebuild fonts and style for the given UI scale (window DPI / 96). The
// default 13px bitmap font stays for 1x (pixel-crisp); above that we load
// Consolas (ships with Windows) at the scaled size. Never call mid-frame.
static void ApplyUiScale(float s) {
    UiScale() = s;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    io.FontGlobalScale = 1.0f;
    bool ttf = false;
    if (s > 1.001f) {
        char path[MAX_PATH];
        UINT n = GetWindowsDirectoryA(path, MAX_PATH);
        if (n > 0 && n < MAX_PATH - 20) {
            strcat_s(path, "\\Fonts\\consola.ttf");
            if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
                ttf = io.Fonts->AddFontFromFileTTF(path, (float)(int)(13.0f * s + 0.5f)) != nullptr;
        }
    }
    if (!ttf) {
        io.Fonts->AddFontDefault();
        // The default atlas is always 13px, so scale it in either direction.
        // This also makes the documented 0.5..1.0 test overrides scale the font
        // together with the style and explicitly-sized widgets.
        io.FontGlobalScale = s;
    }
    ImGui_ImplDX11_InvalidateDeviceObjects();  // font atlas re-uploads next frame

    // Fresh style then scale: ScaleAllSizes compounds, so never rescale in place.
    ImGuiStyle st;
    ImGui::StyleColorsDark(&st);
    // Tooltips only appear once the mouse has been parked on a control for a
    // moment, so they never flash by during ordinary use.
    st.HoverFlagsForTooltipMouse = ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_DelayNormal;
    st.HoverDelayNormal = 0.55f;
    st.HoverStationaryDelay = 0.25f;
    st.ScaleAllSizes(s);
    ImGui::GetStyle() = st;
    g_controlsRescale = true;  // ImGuiCond_FirstUseEver won't re-size it, so we do
}

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) { g_resize = true; g_resizeW = (UINT)LOWORD(lp); g_resizeH = (UINT)HIWORD(lp); }
        return 0;
    case WM_DPICHANGED: {
        // Per-monitor-v2 windows must rescale themselves; adopt the suggested
        // rect and rebuild the UI at the new scale between frames.
        if (g_uiScaleOverride <= 0.0f) g_pendingUiScale = (float)LOWORD(wp) / 96.0f;
        const RECT* r = (const RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static POINT DefaultCapturePoint() {
    auto outs = CaptureSource::EnumerateOutputs();
    for (auto& o : outs) if (o.hdr) return POINT{ (o.rect.left + o.rect.right) / 2, (o.rect.top + o.rect.bottom) / 2 };
    if (!outs.empty()) return POINT{ (outs[0].rect.left + outs[0].rect.right) / 2, (outs[0].rect.top + outs[0].rect.bottom) / 2 };
    return POINT{ 0, 0 };
}

// ---- controls popup ----------------------------------------------------------
static void DrawControlsWindow(float btnX, float btnY) {
    if (!g_showControls) return;
    const float u = UiScale();
    const float w = 380.0f * u;
    // After a DPI change, snap the popup to the newly scaled size once
    // (FirstUseEver would leave it at the old-scale dimensions forever). Clamp
    // the height so a high scale in a small window can't push it off-screen.
    const float maxH = ImGui::GetMainViewport()->WorkSize.y - (btnY - ImGui::GetMainViewport()->WorkPos.y) - 12 * u;
    ImGui::SetNextWindowSize(ImVec2(w, std::min(760 * u, maxH)),
                             g_controlsRescale ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    g_controlsRescale = false;
    // Appear just under the Controls button (right-aligned to it) each time it opens.
    ImGui::SetNextWindowPos(ImVec2(btnX + 60.0f * u - w, btnY), ImGuiCond_Appearing);
    if (!ImGui::Begin("Controls", &g_showControls)) { ImGui::End(); return; }
    UiTipsEnabled() = g_set.showTooltips;

    // Warm amber for the category headers/expanders — distinct from the blue
    // input widgets so section titles read clearly instead of blending in.
    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.46f, 0.30f, 0.09f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.56f, 0.38f, 0.13f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.63f, 0.44f, 0.17f, 1.0f));

    if (ImGui::CollapsingHeader("Capture", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto outs = CaptureSource::EnumerateOutputs();
        int cur = g_capture.OutputIndex();
        char preview[128] = "—";
        for (auto& o : outs) if (o.index == cur)
            snprintf(preview, sizeof(preview), "%d: %dx%d %s", o.index, o.rect.right - o.rect.left, o.rect.bottom - o.rect.top, o.hdr ? "HDR" : "SDR");
        ImGui::SetNextItemWidth(220 * u);
        if (ImGui::BeginCombo("Monitor", preview)) {
            for (auto& o : outs) {
                char b[128]; snprintf(b, sizeof(b), "%d: %dx%d %s", o.index, o.rect.right - o.rect.left, o.rect.bottom - o.rect.top, o.hdr ? "HDR" : "SDR");
                if (ImGui::Selectable(b, o.index == cur)) { g_capture.RetargetToIndex(o.index); g_set.outputIndex = o.index; }
            }
            ImGui::EndCombo();
        }
        ImGui::Text("Captured: %ux%u %s", g_capture.Width(), g_capture.Height(), g_capture.IsHDR() ? "HDR scRGB" : "SDR");
        ImGui::Text("SDR white: %.0f nits", g_sdrWhiteNits);

        ImGui::RadioButton("Full", &g_set.regionMode, 0); ImGui::SameLine();
        ImGui::RadioButton("Window", &g_set.regionMode, 1);
        UiTip("Scope a single window: arm the picker below, then hover the target window."); ImGui::SameLine();
        ImGui::RadioButton("Rect", &g_set.regionMode, 2);
        UiTip("Scope a fixed screen rectangle: drag one with the button below, or type desktop coordinates.");
        if (ImGui::Button("Select region on screen (drag)")) {
            RECT outRect = g_capture.DesktopRect();
            if (outRect.right <= outRect.left) outRect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
            ShowWindow(g_hwnd, SW_HIDE); Sleep(140);
            RECT picked;
            bool got = regionpicker::PickScreenRegion(outRect, picked);
            ShowWindow(g_hwnd, SW_SHOW); SetForegroundWindow(g_hwnd);
            if (got) {
                g_set.dragRect[0] = picked.left; g_set.dragRect[1] = picked.top;
                g_set.dragRect[2] = picked.right - picked.left; g_set.dragRect[3] = picked.bottom - picked.top;
                g_set.regionMode = 2;
            }
        }
        if (g_set.regionMode == 1) {
            if (ImGui::Button("Pick window under cursor (2s)")) g_pickArmUntil = GetTickCount64() + 2000;
            if (g_pickArmUntil) {
                long long left = (long long)g_pickArmUntil - (long long)GetTickCount64();
                if (left > 0) ImGui::Text("Hover target window... %lld ms", left);
                else { POINT pt; GetCursorPos(&pt); HWND w = WindowFromPoint(pt); if (w) g_pickedWindow = GetAncestor(w, GA_ROOT); g_pickArmUntil = 0; }
            }
        } else if (g_set.regionMode == 2) {
            ImGui::SetNextItemWidth(220 * u);
            ImGui::InputInt4("L,T,W,H", g_set.dragRect);
            UiTip("The captured rectangle in desktop coordinates: left, top, width, height.");
        }
    }

    if (ImGui::CollapsingHeader("Quality & display", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Continuous quality: 1 = per pixel (default), higher = downsample the
        // source sampling by that factor (for weaker GPUs).
        char qfmt[48];
        if (g_set.perPixelQuality()) snprintf(qfmt, sizeof(qfmt), "per pixel");
        else snprintf(qfmt, sizeof(qfmt), "1/%.2f resolution", g_set.qualityDownsample);
        ImGui::SetNextItemWidth(160 * u);
        if (ImGui::SliderFloat("Quality", &g_set.qualityDownsample, 1.0f, 8.0f, qfmt, ImGuiSliderFlags_Logarithmic))
            g_set.qualityDownsample = std::clamp(g_set.qualityDownsample, 1.0f, 8.0f);
        UiReset(g_set.qualityDownsample, UiDefaults().qualityDownsample);
        UiTip("How densely the source is sampled. Per pixel reads every captured pixel; "
              "lower quality samples a coarser grid - much cheaper on weak GPUs, at the "
              "cost of missing isolated single-pixel values.");
        if (!g_set.perPixelQuality() && g_lastCropW > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%dx%d", std::max(1, (int)std::lround(g_lastCropW / g_set.qualityDownsample)),
                                         std::max(1, (int)std::lround(g_lastCropH / g_set.qualityDownsample)));
        }
        if (!g_set.perPixelQuality()) { // sampling is 1:1 at per-pixel, filter is a no-op
            ImGui::Checkbox("Bilinear source downsample", &g_set.bilinearDownsample);
            UiReset(g_set.bilinearDownsample, UiDefaults().bilinearDownsample);
            UiTip("Average neighboring pixels when sampling below per-pixel quality, "
                  "instead of picking one. Suppresses sampling noise, but blends values "
                  "so outliers read slightly softer.");
        }
        const char* ss[] = { "Off", "2x", "4x" };
        int ssi = g_set.renderSupersample <= 1 ? 0 : (g_set.renderSupersample >= 4 ? 2 : 1);
        ImGui::SetNextItemWidth(160 * u);
        if (ImGui::Combo("Anti-alias (supersample)", &ssi, ss, 3)) g_set.renderSupersample = ssi == 0 ? 1 : (ssi == 1 ? 2 : 4);
        UiReset(g_set.renderSupersample, UiDefaults().renderSupersample);
        UiTip("Render the scope graphics at 2x/4x resolution and downscale - smoother "
              "traces and graticule lines for a bit more GPU work.");
        ImGui::SetNextItemWidth(160 * u);
        ImGui::SliderFloat("Source blur", &g_set.sourceBlur, 0.0f, 8.0f, "%.1f px");
        UiReset(g_set.sourceBlur, UiDefaults().sourceBlur);
        UiTip("Gaussian-blur the capture before it reaches the scopes, so dither and "
              "single-pixel noise stop registering and the traces show the underlying "
              "levels. Radius in source pixels.");
        if (g_set.sourceBlur > 0.05f) {
            ImGui::Checkbox("Blur affects waveform extents", &g_set.blurExtents);
            UiReset(g_set.blurExtents, UiDefaults().blurExtents);
            UiTip("Feed the blurred image to the waveform extents trace too. Off = "
                  "extents keep reading the sharp source, so true single-pixel peaks "
                  "stay visible while the main trace is smoothed.");
        }
        ImGui::Checkbox("Show FPS", &g_set.showFps);
        UiReset(g_set.showFps, UiDefaults().showFps);
        ImGui::SetNextItemWidth(160 * u);
        ImGui::SliderInt("FPS limit (0=vsync)", &g_set.fpsLimit, 0, 240);
        UiReset(g_set.fpsLimit, UiDefaults().fpsLimit);
        UiTip("Cap how often the scopes update, to spend less GPU. 0 = sync to the "
              "monitor's refresh rate.");
        ImGui::Checkbox("UI follows Windows SDR white", &g_set.uiFollowSdrWhite);
        UiReset(g_set.uiFollowSdrWhite, UiDefaults().uiFollowSdrWhite);
        UiTip("Draw the interface at the brightness set by the Windows SDR-white "
              "slider, matching other desktop apps. The scope traces are unaffected - "
              "they always keep their true HDR brightness.");
        ImGui::Checkbox("Hover probe markers", &g_set.showHoverProbe);
        UiReset(g_set.showHoverProbe, UiDefaults().showHoverProbe);
        UiTip("Mark where the pixel under your mouse lands on each scope: a white "
              "circle for luminance and R/G/B circles for the channels. Works while "
              "hovering anywhere on the captured screen area.");
        ImGui::SameLine(); ImGui::SetNextItemWidth(70 * u);
        ImGui::SliderFloat("Size", &g_set.hoverCircleRadius, 3.0f, 24.0f, "%.0f");
        UiReset(g_set.hoverCircleRadius, UiDefaults().hoverCircleRadius);
        ImGui::Checkbox("Hover/peak readout (top)", &g_set.showHoverReadout);
        UiReset(g_set.showHoverReadout, UiDefaults().showHoverReadout);
        UiTip("The numbers top-center: the hovered pixel's luminance and R/G/B in "
              "nits, or the frame's peak values when nothing is hovered.");
        ImGui::SameLine(); ImGui::Checkbox("8-bit SDR", &g_set.showSdr8bit);
        UiReset(g_set.showSdr8bit, UiDefaults().showSdr8bit);
        UiTip("Also show the readout values as 8-bit SDR code levels (0-255) relative "
              "to the current SDR white; anything brighter reads 255+.");
        if (g_set.showHoverReadout) {
            ImGui::Checkbox("Readout background", &g_set.readoutBg);
            UiReset(g_set.readoutBg, UiDefaults().readoutBg);
            UiTip("Draw a translucent dark box behind the top readout so it stays "
                  "legible over bright scope content.");
        }
        ImGui::Checkbox("Nit value at cursor", &g_set.showCursorNits);
        UiReset(g_set.showCursorNits, UiDefaults().showCursorNits);
        UiTip("Attach a small nit label to the mouse cursor while hovering the "
              "waveform or histogram, reading the axis value at that position.");
    }

    if (ImGui::CollapsingHeader("Graticule", ImGuiTreeNodeFlags_DefaultOpen)) {
        // (no right-click reset on the swatch: ColorEdit owns that gesture)
        ImGui::ColorEdit3("Color", &g_set.graticuleColor.x, ImGuiColorEditFlags_NoInputs);
        UiTip("The graticule is the measurement scale drawn over the scopes - axis "
              "lines, ticks and labels.");
        ImGui::SliderFloat("Opacity", &g_set.graticuleOpacity, 0.0f, 1.0f);
        UiReset(g_set.graticuleOpacity, UiDefaults().graticuleOpacity);
    }

    int count = (int)g_set.layout;
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(1000 + i);
        char hdr[64]; snprintf(hdr, sizeof(hdr), "Panel %d - %s", i + 1, ScopeTypeName(g_set.panelScope[i]));
        if (ImGui::CollapsingHeader(hdr, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SetNextItemWidth(180 * u);
            ScopeCombo("Scope", g_set.panelScope[i]);
            if (g_panels[i].Scope()) g_panels[i].Scope()->DrawControls(g_set);
        }
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("Preferences")) {
        ImGui::Checkbox("Show tooltips", &g_set.showTooltips);
        UiReset(g_set.showTooltips, UiDefaults().showTooltips);

        float fadePct = g_set.controlsFadeOpacity * 100.0f;
        ImGui::SetNextItemWidth(160 * u);
        if (ImGui::SliderFloat("Idle controls opacity", &fadePct, 0.0f, 100.0f, "%.0f%%",
                               ImGuiSliderFlags_AlwaysClamp))
            // The range check rejects NaN from ctrl-click text entry, which
            // would otherwise latch into the fade filter and the global alpha.
            g_set.controlsFadeOpacity = (fadePct >= 0.0f && fadePct <= 100.0f)
                                            ? fadePct / 100.0f : UiDefaults().controlsFadeOpacity;
        UiReset(g_set.controlsFadeOpacity, UiDefaults().controlsFadeOpacity);
        UiTip("How visible the floating controls (scope pickers, top-right buttons) "
              "stay while the mouse is outside the HDRScopes window, so they don't "
              "block the scopes when you're just watching. 100% = never fade.");

        if (ImGui::Button("Reset all settings to default")) {
            // Keep the window placement; reset everything else to defaults.
            Settings def;
            def.wndL = g_set.wndL; def.wndT = g_set.wndT; def.wndR = g_set.wndR; def.wndB = g_set.wndB;
            def.wndShow = g_set.wndShow; def.outputIndex = g_set.outputIndex;
            g_set = def;
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Tip: right-click any individual setting to reset just that one to its default.");
        ImGui::PopTextWrapPos();
    }

    if (ImGui::CollapsingHeader("Debug")) {
        ImGui::Checkbox("Show test pattern source", &g_set.debugShowTestPattern);
        UiTip("Expose a built-in synthetic test source (color sweeps and level steps) "
              "as a capture input - for checking the scopes themselves.");
        if (g_set.debugShowTestPattern) ImGui::Checkbox("Use test pattern", &g_set.useTestPattern);
        else g_set.useTestPattern = false;
    }

    ImGui::PopStyleColor(3);
    ImGui::End();
}

// sRGB-encode an SDR-normalized linear value to an 8-bit string ("255+" if HDR).
static void Sdr8(float scrgb, float sdrNorm, char* out, size_t n) {
    float v = scrgb / (sdrNorm > 1e-4f ? sdrNorm : 1.0f);
    if (v > 1.0f) { snprintf(out, n, "255+"); return; }
    if (v < 0.0f) v = 0.0f;
    float e = (v <= 0.0031308f) ? 12.92f * v : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
    snprintf(out, n, "%d", (int)(e * 255.0f + 0.5f));
}

// L/R/G/B nit readout (+ optional 8-bit SDR) top-center. Shows the hovered
// pixel when the cursor is over the target region; otherwise falls back to the
// per-channel PEAKS of the region (four independent maxima — the brightest
// pixel in luminance is not necessarily the brightest in any one channel).
static void DrawHoverReadout(const ScopeFrame& probe, const Settings& s, float sdrNits,
                             const float peakLRGB[4], bool peaksValid, ImVec2 a0, ImVec2 a1,
                             float avoidLeftX, float avoidRightX) {
    if (!s.showHoverReadout) return;
    const bool peaks = !probe.probeValid;
    if (peaks && !peaksValid) return;

    float rgb[3]; double lum;
    if (peaks) {
        lum = peakLRGB[0];
        rgb[0] = peakLRGB[1]; rgb[1] = peakLRGB[2]; rgb[2] = peakLRGB[3];
    } else {
        lum = 0.2126390 * probe.probeRGB[0] + 0.7151686 * probe.probeRGB[1] + 0.0721923 * probe.probeRGB[2];
        rgb[0] = probe.probeRGB[0]; rgb[1] = probe.probeRGB[1]; rgb[2] = probe.probeRGB[2];
    }
    char vl[32], vr[32], vg[32], vb[32];
    FormatNits(std::max(0.0, lum) * 80.0,     vl, sizeof(vl));
    FormatNits(std::max(0.0f, rgb[0]) * 80.0, vr, sizeof(vr));
    FormatNits(std::max(0.0f, rgb[1]) * 80.0, vg, sizeof(vg));
    FormatNits(std::max(0.0f, rgb[2]) * 80.0, vb, sizeof(vb));

    // Channel letters get channel colors (blue brightened so it reads on black).
    const ImU32 white = IM_COL32(235, 235, 235, 240);
    const ImU32 colR  = IM_COL32(255,  40,  40, 240);
    const ImU32 colG  = IM_COL32( 40, 220,  40, 240);
    const ImU32 colB  = IM_COL32(  0, 123, 255, 240);

    struct Seg { char txt[40]; ImU32 col; };
    Seg segs[12]; int nseg = 0;
    auto add = [&](const char* t, ImU32 c) {
        snprintf(segs[nseg].txt, sizeof(segs[nseg].txt), "%s", t); segs[nseg].col = c; ++nseg;
    };
    add("L ", white); add(vl, white); add("    ", white);
    add("R ", colR);  add(vr, white); add("   ", white);
    add("G ", colG);  add(vg, white); add("   ", white);
    add("B ", colB);  add(vb, white); add(peaks ? "   peak nits" : "   nits", white);

    float widths[12]; float total = 0;
    for (int i = 0; i < nseg; ++i) { widths[i] = ImGui::CalcTextSize(segs[i].txt).x; total += widths[i]; }

    char line2[160] = "";
    if (s.showSdr8bit) {
        float sn = sdrNits / 80.0f;
        char r[8], g[8], b[8];
        Sdr8(rgb[0], sn, r, sizeof(r)); Sdr8(rgb[1], sn, g, sizeof(g)); Sdr8(rgb[2], sn, b, sizeof(b));
        snprintf(line2, sizeof(line2), "SDR  %s, %s, %s", r, g, b);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cx = (a0.x + a1.x) * 0.5f;
    const float lineH = ImGui::GetTextLineHeight();
    const float su = UiScale();
    const float bw = std::max(total, line2[0] ? ImGui::CalcTextSize(line2).x : 0.0f);
    float y = a0.y + 11 * su;  // moved 5px down so it clears the 10k line
    // If the readout would run under the top-right button strip or (multi-panel
    // layouts) panel 1's top-left scope combo, drop it a row instead of colliding.
    if ((avoidLeftX  > 0.0f && cx + bw * 0.5f + 8 * su > avoidLeftX) ||
        (avoidRightX > 0.0f && cx - bw * 0.5f - 8 * su < avoidRightX))
        y += ImGui::GetFrameHeight() + 6 * su;
    float w2 = line2[0] ? ImGui::CalcTextSize(line2).x : 0.0f;

    // Optional translucent black plate behind the readout for legibility.
    if (s.readoutBg) {
        float bh = lineH + (line2[0] ? lineH + 2 * su : 0);
        ImVec2 p0(cx - bw * 0.5f - 6 * su, y - 3 * su);
        ImVec2 p1(cx + bw * 0.5f + 6 * su, y + bh + 3 * su);
        dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 140), 4.0f * su);
    }

    float x = cx - total * 0.5f;
    for (int i = 0; i < nseg; ++i) { dl->AddText(ImVec2(x, y), segs[i].col, segs[i].txt); x += widths[i]; }
    if (line2[0])
        dl->AddText(ImVec2(cx - w2 * 0.5f, y + lineH + 2 * su), IM_COL32(180, 200, 235, 230), line2);
}

// Quad-cell width that panel i's fixed-aspect scope (vectorscope/CIE) can
// actually use at the given cell height — its graph plus label margins. 0 for
// stretchy scopes that take whatever width they get (and if the scope failed
// to create, which keeps the split centered).
static float IdealCellW(int i, float cellH, const Settings& s) {
    IScope* sc = g_panels[i].Scope();
    float aspect = sc ? sc->AspectRatio() : 0.0f;
    if (aspect <= 0.0f) return 0.0f;
    Margins m = sc->GetMargins(s);
    const float u = UiScale();
    return (cellH - (m.t + m.b) * u) * aspect + (m.l + m.r) * u;
}

// Layout rects within the given content area. splitX: absolute x of the quad
// layout's vertical split (<0 = centered).
static void LayoutRects(ImVec2 p0, ImVec2 p1, int count, float splitX, ImVec2 out0[4], ImVec2 out1[4]) {
    float w = p1.x - p0.x, h = p1.y - p0.y;
    const float pad = 2.0f;
    if (count == 1) { out0[0] = p0; out1[0] = p1; }
    else if (count == 2) {
        out0[0] = p0; out1[0] = ImVec2(p0.x + w * 0.5f - pad, p1.y);
        out0[1] = ImVec2(p0.x + w * 0.5f + pad, p0.y); out1[1] = p1;
    } else {
        float mx = (splitX > 0.0f) ? splitX : p0.x + w * 0.5f, my = p0.y + h * 0.5f;
        out0[0] = p0;                          out1[0] = ImVec2(mx - pad, my - pad);
        out0[1] = ImVec2(mx + pad, p0.y);      out1[1] = ImVec2(p1.x, my - pad);
        out0[2] = ImVec2(p0.x, my + pad);      out1[2] = ImVec2(mx - pad, p1.y);
        out0[3] = ImVec2(mx + pad, my + pad);  out1[3] = p1;
    }
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_set.Load();

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = WndProc; wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"HDRScopesWnd";
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    const float sysScale = (float)GetDpiForSystem() / 96.0f;
    int wx = CW_USEDEFAULT, wy = CW_USEDEFAULT;
    int ww = (int)(1440 * sysScale), wh = (int)(900 * sysScale);
    if (g_set.wndR > g_set.wndL && g_set.wndB > g_set.wndT) {
        wx = g_set.wndL; wy = g_set.wndT; ww = g_set.wndR - g_set.wndL; wh = g_set.wndB - g_set.wndT;
    }
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName,
        L"HDRScopes " HS_WIDEN(HDRSCOPES_VERSION), WS_OVERLAPPEDWINDOW,
        wx, wy, ww, wh, nullptr, nullptr, hInst, nullptr);
    g_hwnd = hwnd;

    // The app is dark-themed throughout, so force a dark title bar regardless
    // of the OS light/dark setting (without this Windows paints it light when
    // the window is unfocused — and a scopes app lives unfocused).
    BOOL darkTitle = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkTitle, sizeof(darkTitle));

    if (!g_d3d.Init(hwnd)) { HDRLog("D3D init failed"); return 1; }

    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_Init(hwnd); ImGui_ImplDX11_Init(g_d3d.Device(), g_d3d.Context());

    // UI scale follows the window's monitor DPI (WM_DPICHANGED tracks moves);
    // HDRSCOPES_UISCALE=1.5 overrides it for testing.
    float uiScale = (float)GetDpiForWindow(hwnd) / 96.0f;
    if (const char* e = getenv("HDRSCOPES_UISCALE")) {
        float v = (float)atof(e);
        if (v >= 0.5f && v <= 4.0f) { g_uiScaleOverride = v; uiScale = v; }
    }
    ApplyUiScale(uiScale);

    g_test.Init(g_d3d.Device());
    g_blur.Init(g_d3d.Device());
    g_probe.Init(g_d3d.Device());
    g_peaks.Init(g_d3d.Device());
    g_capture.Init(g_d3d.Device(), g_set.outputIndex >= 0 ? POINT{ 0,0 } : DefaultCapturePoint());
    if (g_set.outputIndex >= 0) g_capture.RetargetToIndex(g_set.outputIndex);
    for (auto& p : g_panels) p.Init(g_d3d.Device());

    ShowWindow(hwnd, g_set.wndShow == SW_MAXIMIZE ? SW_MAXIMIZE : SW_SHOW);
    UpdateWindow(hwnd);
    timeBeginPeriod(1);

    LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
    LARGE_INTEGER prev; QueryPerformanceCounter(&prev);

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
        if (!running) break;

        // Apply a pending DPI rescale before the resize path so a cross-monitor
        // move rebuilds ImGui device objects once, not twice (ApplyUiScale only
        // invalidates; the resize block or NewFrame recreates).
        if (g_pendingUiScale > 0.0f) {
            if (fabsf(g_pendingUiScale - UiScale()) > 0.01f) ApplyUiScale(g_pendingUiScale);
            g_pendingUiScale = -1.0f;
        }

        if (g_resize) {
            g_d3d.Resize(g_resizeW, g_resizeH);
            ImGui_ImplDX11_InvalidateDeviceObjects(); ImGui_ImplDX11_CreateDeviceObjects();
            g_resize = false;
        }

        // SDR white level (refresh ~1/s; near-realtime for the UI + SDR-white zoom).
        // Scope math uses the *captured* monitor; UI brightness uses the monitor
        // the app window is on (they can differ on a mixed-HDR multi-monitor setup).
        ULONGLONG now = GetTickCount64();
        if (now - g_lastSdrQuery > 1000) {
            std::wstring capDev = g_capture.DeviceName();
            g_sdrWhiteNits = capDev.empty() ? sdrwhite::QueryPrimaryNits(200.0f)
                                            : sdrwhite::QueryNits(capDev.c_str(), 200.0f);
            MONITORINFOEXW mi = {}; mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi))
                g_uiSdrWhiteNits = sdrwhite::QueryNits(mi.szDevice, 200.0f);
            g_lastSdrQuery = now;
        }
        float uiB = (g_d3d.IsHDR() && g_set.uiFollowSdrWhite) ? (g_uiSdrWhiteNits / 80.0f) : 1.0f;
        ImGui_ImplDX11_SetUIBrightness(uiB);

        // ---- Source ----
        ID3D11ShaderResourceView* srcSRV = nullptr; UINT srcW = 0, srcH = 0; RECT outRect = {};
        bool usingTest = g_set.debugShowTestPattern && g_set.useTestPattern;
        if (usingTest) { g_test.Generate(); srcSRV = g_test.SRV(); srcW = g_test.Width(); srcH = g_test.Height(); outRect = { 0,0,(LONG)srcW,(LONG)srcH }; }
        else {
            if (g_capture.AcquireFrame()) {
                srcSRV = g_capture.SRV();
                srcW = g_capture.Width();
                srcH = g_capture.Height();
            }
            outRect = g_capture.DesktopRect();
        }

        // ---- Region crop ----
        Region region; region.mode = (RegionMode)g_set.regionMode;
        if (g_set.regionMode == 1) region.targetWindow = g_pickedWindow;
        else if (g_set.regionMode == 2) region.desktopRect = { g_set.dragRect[0], g_set.dragRect[1], g_set.dragRect[0] + g_set.dragRect[2], g_set.dragRect[1] + g_set.dragRect[3] };
        int cx = 0, cy = 0, cw = (int)srcW, ch = (int)srcH;
        if (!(usingTest || g_set.regionMode == 0))
            if (!region.ResolveToTexel(outRect, srcW, srcH, cx, cy, cw, ch)) { cx = cy = 0; cw = srcW; ch = srcH; }
        g_lastCropW = cw; g_lastCropH = ch;

        // Optional source blur (scopes read the blurred texture; the probe below
        // still reads the original pixels for an accurate readout).
        ID3D11ShaderResourceView* scopeSRV = srcSRV;
        if (srcSRV && g_set.sourceBlur > 0.05f)
            scopeSRV = g_blur.Apply(srcSRV, srcW, srcH, g_set.sourceBlur);

        ScopeInput input; input.srcSRV = scopeSRV; input.rawSRV = srcSRV; input.srcW = srcW; input.srcH = srcH;
        input.cropX = cx; input.cropY = cy; input.cropW = cw; input.cropH = ch;
        input.sdrWhiteNits = g_sdrWhiteNits;

        // ---- Region peaks (for the readout when the cursor is off-region) ----
        // Reads the unblurred source, like the probe, so peaks reflect the true
        // signal.
        float peakLRGB[4] = { 0, 0, 0, 0 };
        bool peaksValid = false;
        if (g_set.showHoverReadout && srcSRV)
            peaksValid = g_peaks.Measure(srcSRV, srcW, srcH, cx, cy, cw, ch, peakLRGB);

        // ---- Hover probe (source pixel under cursor) ----
        ScopeFrame probe;
        if ((g_set.showHoverProbe || g_set.showHoverReadout) && !usingTest && srcSRV) {
            POINT cur; GetCursorPos(&cur);
            if (PtInRect(&outRect, cur)) {
                int tx = cur.x - outRect.left, ty = cur.y - outRect.top;
                if (tx >= cx && tx < cx + cw && ty >= cy && ty < cy + ch) {
                    float rgb[3];
                    if (g_probe.Read(srcSRV, srcW, srcH, tx, ty, rgb)) {
                        probe.probeValid = true;
                        probe.probeRGB[0] = rgb[0]; probe.probeRGB[1] = rgb[1]; probe.probeRGB[2] = rgb[2];
                        probe.probeU = ((float)(tx - cx) + 0.5f) / (float)std::max(1, cw);
                        probe.probeV = ((float)(ty - cy) + 0.5f) / (float)std::max(1, ch);
                    }
                }
            }
        }

        // Fade the floating controls (scope pickers + top-right strip) when the
        // cursor is outside the app window, so they stop covering the scopes
        // while HDRScopes is just being watched. WindowFromPoint (not a rect
        // test) so a foreign window overlapping ours still counts as outside.
        {
            POINT cur; GetCursorPos(&cur);
            HWND under = WindowFromPoint(cur);
            bool inside = under && GetAncestor(under, GA_ROOT) == g_hwnd;
            float target = inside ? 1.0f : std::clamp(g_set.controlsFadeOpacity, 0.0f, 1.0f);
            float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
            g_ctrlAlpha += (target - g_ctrlAlpha) * std::min(1.0f, dt * 10.0f);
        }

        // ---- ImGui frame ----
        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##host", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

        ImVec2 area0 = ImGui::GetWindowPos();
        ImVec2 area1 = ImVec2(area0.x + ImGui::GetWindowSize().x, area0.y + ImGui::GetWindowSize().y);
        int count = (int)g_set.layout;
        const float u = UiScale();
        // Reserve a small strip at the very top so the "10k" nit label isn't clipped.
        ImVec2 scopeArea0 = ImVec2(area0.x, area0.y + 5.0f * u);

        // Quad layout: when one column holds only fixed-aspect scopes
        // (vectorscope/CIE) and the other only stretchy ones, shift the
        // vertical split so the square column gets exactly the width its
        // graphs can use and the stretchy column takes the reclaimed space.
        // Never below 50% for the stretchy column (narrow windows stay 50/50).
        float splitX = -1.0f;
        if (count == 4) {
            // Materialize any scope-type changes now so the split is computed
            // from the scopes actually drawn this frame (no one-frame lag).
            for (int i = 0; i < count; ++i) g_panels[i].EnsureScope(g_set.panelScope[i]);
            const float pad = 2.0f;  // matches LayoutRects
            float cellH = (area1.y - scopeArea0.y) * 0.5f - pad;
            auto colWidth = [&](int a, int b) {  // widest ideal width; 0 = has a stretchy panel
                float wa = IdealCellW(a, cellH, g_set), wb = IdealCellW(b, cellH, g_set);
                return (wa > 0.0f && wb > 0.0f) ? std::max(wa, wb) : 0.0f;
            };
            float li = colWidth(0, 2), ri = colWidth(1, 3);
            float cx = (scopeArea0.x + area1.x) * 0.5f;
            if (ri > 0.0f && li <= 0.0f)      splitX = std::max(cx, area1.x - pad - ri);
            else if (li > 0.0f && ri <= 0.0f) splitX = std::min(cx, scopeArea0.x + pad + li);
        }
        ImVec2 r0[4], r1[4]; LayoutRects(scopeArea0, area1, count, splitX, r0, r1);
        for (int i = 0; i < count; ++i)
            g_panels[i].Draw(i, r0[i], r1[i], input, g_set, g_sdrWhiteNits, probe, uiB);

        // Panel borders.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < count; ++i)
            dl->AddRect(r0[i], r1[i], IM_COL32(60, 60, 60, 255));

        // Per-panel combos sit right of each scope's left label margin (30px
        // for the waveform nit labels) so they never cover the numbers.
        auto panelComboX = [&](int i) {
            float ml = g_panels[i].Scope() ? g_panels[i].Scope()->GetMargins(g_set).l : 0.0f;
            return (ml + 8.0f) * u;
        };

        // Top-right strip geometry (the readout needs it to dodge the buttons).
        const float comboW = ScopeComboWidth(g_set.panelScope[0]);
        float stripY = area0.y + 11 * u;                                       // 5px down to clear the 10k line
        float stripX = area1.x - 240 * u - (count == 1 ? comboW + 8 * u : 0);  // 80px right, snug in the corner

        // Hover readout: centered over the wider top panel (with an off-center
        // quad split that's the stretchy scope, which has room to spare), so it
        // clears the divider and the fixed-aspect graphs.
        int rp = (count > 1 && r1[1].x - r0[1].x > r1[0].x - r0[0].x + 1.0f) ? 1 : 0;
        ImVec2 readout0 = (count == 1) ? area0 : ImVec2(r0[rp].x, area0.y);
        ImVec2 readout1 = (count == 1) ? area1 : ImVec2(r1[rp].x, area1.y);
        // That panel's scope combo sits at its top-left; dodge it (and the strip).
        const float avoidRightX = (count > 1)
            ? r0[rp].x + panelComboX(rp) + ScopeComboWidth(g_set.panelScope[rp]) + 8 * u : -1.0f;
        DrawHoverReadout(probe, g_set, g_sdrWhiteNits, peakLRGB, peaksValid, readout0, readout1,
                         stripX, avoidRightX);

        // Opaque widget backgrounds for the floating top strips (so they read
        // clearly over the scope graphs instead of being semi-transparent).
        auto pushOpaqueWidgets = [] {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.42f, 0.78f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.54f, 0.92f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.16f, 0.36f, 0.70f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.16f, 0.30f, 0.55f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.24f, 0.42f, 0.72f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0.28f, 0.48f, 0.80f, 1.0f));
        };

        // Top-right strip: (single layout) [scope combo] [Zoom] [1][2][4][Controls].
        // The whole floating-controls layer fades while the cursor is outside
        // the window (Preferences > Idle controls opacity).
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_ctrlAlpha);
        ImGui::SetCursorScreenPos(ImVec2(stripX, stripY));
        pushOpaqueWidgets();
        if (count == 1) {
            ImGui::PushID(3000);
            ScopeCombo("##sc0", g_set.panelScope[0], true); ImGui::PopID(); ImGui::SameLine();
        }
        char zlbl[32]; snprintf(zlbl, sizeof(zlbl), "Zoom %.2fx", g_set.zoom[0]);
        if (ImGui::Button(zlbl)) { for (int i = 0; i < 4; ++i) { g_set.zoom[i] = 1; g_set.panX[i] = g_set.panY[i] = 0; } }
        ImGui::SameLine();
        if (ImGui::Button("1")) g_set.layout = LayoutMode::Single; ImGui::SameLine();
        if (ImGui::Button("2")) g_set.layout = LayoutMode::SideBySide; ImGui::SameLine();
        if (ImGui::Button("4")) g_set.layout = LayoutMode::Quad; ImGui::SameLine();
        float ctrlBtnX = ImGui::GetCursorScreenPos().x;
        if (ImGui::Button("Controls")) g_showControls = !g_showControls;
        ImGui::PopStyleColor(6);

        // In multi-layout, each panel gets its own scope combo at its top-left.
        if (count > 1) {
            pushOpaqueWidgets();
            for (int i = 0; i < count; ++i) {
                ImGui::SetCursorScreenPos(ImVec2(r0[i].x + panelComboX(i), r0[i].y + 6 * u));
                ImGui::PushID(2000 + i);
                ScopeCombo("##sc", g_set.panelScope[i], true);
                ImGui::PopID();
            }
            ImGui::PopStyleColor(6);
        }
        ImGui::PopStyleVar();  // floating-controls fade alpha

        // Bottom-right FPS.
        if (g_set.showFps) {
            char fps[32]; snprintf(fps, sizeof(fps), "%.0f FPS", ImGui::GetIO().Framerate);
            float fsz = ImGui::GetFontSize() * 0.85f;            // slightly smaller font
            float w = ImGui::CalcTextSize(fps).x * 0.85f;
            dl->AddText(ImGui::GetFont(), fsz, ImVec2(area1.x - w - 16 * u, area1.y - 24 * u),  // 6px left, 2px up
                        IM_COL32(200, 200, 200, 220), fps);
        }

        ImGui::End();
        ImGui::PopStyleVar();

        // Controls popup appears just under the Controls button.
        DrawControlsWindow(ctrlBtnX, area0.y + 34 * u);

        ImGui::Render();
        ID3D11RenderTargetView* rtv = g_d3d.BackBufferRTV();
        const float clear[4] = { 0.01f, 0.01f, 0.01f, 1.0f };
        g_d3d.Context()->OMSetRenderTargets(1, &rtv, nullptr);
        g_d3d.Context()->ClearRenderTargetView(rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        // When the sleep-based limiter is active it owns pacing, so present
        // unsynced; otherwise vsync.
        g_d3d.Present(g_set.fpsLimit == 0);

        // FPS limiter.
        if (g_set.fpsLimit > 0) {
            double target = 1.0 / g_set.fpsLimit;
            for (;;) {
                LARGE_INTEGER c; QueryPerformanceCounter(&c);
                double elapsed = double(c.QuadPart - prev.QuadPart) / freq.QuadPart;
                if (elapsed >= target) { prev = c; break; }
                double remMs = (target - elapsed) * 1000.0;
                if (remMs > 2.0) Sleep((DWORD)(remMs - 1.0)); else Sleep(0);
            }
        } else {
            QueryPerformanceCounter(&prev);
        }
    }

    timeEndPeriod(1);

    // Save window placement + settings.
    WINDOWPLACEMENT wpl = { sizeof(wpl) };
    if (GetWindowPlacement(hwnd, &wpl)) {
        g_set.wndL = wpl.rcNormalPosition.left; g_set.wndT = wpl.rcNormalPosition.top;
        g_set.wndR = wpl.rcNormalPosition.right; g_set.wndB = wpl.rcNormalPosition.bottom;
        g_set.wndShow = wpl.showCmd;
    }
    g_set.Save();

    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    for (auto& p : g_panels) if (p.Scope()) p.Scope()->Shutdown();
    g_test.Shutdown(); g_probe.Shutdown(); g_peaks.Shutdown(); g_capture.Shutdown(); g_d3d.Shutdown();
    return 0;
}
