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
#include <timeapi.h>
#include <algorithm>

// Widen the narrow HDRSCOPES_VERSION macro from CMake for the window title.
#define HS_WIDEN2(x) L##x
#define HS_WIDEN(x) HS_WIDEN2(x)
#include <cmath>
#include <cstring>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// Scope-type picker (the four implemented scopes).
static bool ScopeCombo(const char* id, ScopeType& st) {
    static const ScopeType order[4] = { ScopeType::Waveform, ScopeType::Histogram, ScopeType::Vectorscope, ScopeType::CIE };
    const char* names[4] = { "Waveform", "Histogram", "Vectorscope", "CIE Chromaticity" };
    int cur = 0;
    for (int i = 0; i < 4; ++i) if (order[i] == st) cur = i;
    if (ImGui::Combo(id, &cur, names, 4)) { st = order[cur]; return true; }
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
}

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) { g_resize = true; g_resizeW = (UINT)LOWORD(lp); g_resizeH = (UINT)HIWORD(lp); }
        return 0;
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
    const float w = 380.0f;
    ImGui::SetNextWindowSize(ImVec2(w, 760), ImGuiCond_FirstUseEver);
    // Appear just under the Controls button (right-aligned to it) each time it opens.
    ImGui::SetNextWindowPos(ImVec2(btnX + 60.0f - w, btnY), ImGuiCond_Appearing);
    if (!ImGui::Begin("Controls", &g_showControls)) { ImGui::End(); return; }

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
        ImGui::SetNextItemWidth(220);
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
        ImGui::RadioButton("Window", &g_set.regionMode, 1); ImGui::SameLine();
        ImGui::RadioButton("Rect", &g_set.regionMode, 2);
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
            ImGui::SetNextItemWidth(220);
            ImGui::InputInt4("L,T,W,H", g_set.dragRect);
        }
    }

    if (ImGui::CollapsingHeader("Quality & display", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Continuous quality: 1 = per pixel (default), higher = downsample the
        // source sampling by that factor (for weaker GPUs).
        char qfmt[48];
        if (g_set.perPixelQuality()) snprintf(qfmt, sizeof(qfmt), "per pixel");
        else snprintf(qfmt, sizeof(qfmt), "1/%.2f resolution", g_set.qualityDownsample);
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderFloat("Quality", &g_set.qualityDownsample, 1.0f, 8.0f, qfmt, ImGuiSliderFlags_Logarithmic))
            g_set.qualityDownsample = std::clamp(g_set.qualityDownsample, 1.0f, 8.0f);
        UiReset(g_set.qualityDownsample, UiDefaults().qualityDownsample);
        if (!g_set.perPixelQuality() && g_lastCropW > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%dx%d", std::max(1, (int)std::lround(g_lastCropW / g_set.qualityDownsample)),
                                         std::max(1, (int)std::lround(g_lastCropH / g_set.qualityDownsample)));
        }
        if (!g_set.perPixelQuality()) { // sampling is 1:1 at per-pixel, filter is a no-op
            ImGui::Checkbox("Bilinear source downsample", &g_set.bilinearDownsample);
            UiReset(g_set.bilinearDownsample, UiDefaults().bilinearDownsample);
        }
        const char* ss[] = { "Off", "2x", "4x" };
        int ssi = g_set.renderSupersample <= 1 ? 0 : (g_set.renderSupersample >= 4 ? 2 : 1);
        ImGui::SetNextItemWidth(160);
        if (ImGui::Combo("Anti-alias (supersample)", &ssi, ss, 3)) g_set.renderSupersample = ssi == 0 ? 1 : (ssi == 1 ? 2 : 4);
        UiReset(g_set.renderSupersample, UiDefaults().renderSupersample);
        ImGui::SetNextItemWidth(160);
        ImGui::SliderFloat("Source blur", &g_set.sourceBlur, 0.0f, 8.0f, "%.1f px");
        UiReset(g_set.sourceBlur, UiDefaults().sourceBlur);
        if (g_set.sourceBlur > 0.05f) {
            ImGui::Checkbox("Blur affects waveform extents", &g_set.blurExtents);
            UiReset(g_set.blurExtents, UiDefaults().blurExtents);
        }
        ImGui::Checkbox("Show FPS", &g_set.showFps);
        UiReset(g_set.showFps, UiDefaults().showFps);
        ImGui::SetNextItemWidth(160);
        ImGui::SliderInt("FPS limit (0=vsync)", &g_set.fpsLimit, 0, 240);
        UiReset(g_set.fpsLimit, UiDefaults().fpsLimit);
        ImGui::Checkbox("UI follows Windows SDR white", &g_set.uiFollowSdrWhite);
        UiReset(g_set.uiFollowSdrWhite, UiDefaults().uiFollowSdrWhite);
        ImGui::Checkbox("Hover probe markers", &g_set.showHoverProbe);
        UiReset(g_set.showHoverProbe, UiDefaults().showHoverProbe);
        ImGui::SameLine(); ImGui::SetNextItemWidth(70);
        ImGui::SliderFloat("Size", &g_set.hoverCircleRadius, 3.0f, 24.0f, "%.0f");
        UiReset(g_set.hoverCircleRadius, UiDefaults().hoverCircleRadius);
        ImGui::Checkbox("Hover/peak readout (top)", &g_set.showHoverReadout);
        UiReset(g_set.showHoverReadout, UiDefaults().showHoverReadout);
        ImGui::SameLine(); ImGui::Checkbox("8-bit SDR", &g_set.showSdr8bit);
        UiReset(g_set.showSdr8bit, UiDefaults().showSdr8bit);
        if (g_set.showHoverReadout) {
            ImGui::Checkbox("Readout background", &g_set.readoutBg);
            UiReset(g_set.readoutBg, UiDefaults().readoutBg);
        }
        ImGui::Checkbox("Nit value at cursor", &g_set.showCursorNits);
        UiReset(g_set.showCursorNits, UiDefaults().showCursorNits);
    }

    if (ImGui::CollapsingHeader("Graticule", ImGuiTreeNodeFlags_DefaultOpen)) {
        // (no right-click reset on the swatch: ColorEdit owns that gesture)
        ImGui::ColorEdit3("Color", &g_set.graticuleColor.x, ImGuiColorEditFlags_NoInputs);
        ImGui::SliderFloat("Opacity", &g_set.graticuleOpacity, 0.0f, 1.0f);
        UiReset(g_set.graticuleOpacity, UiDefaults().graticuleOpacity);
    }

    int count = (int)g_set.layout;
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(1000 + i);
        char hdr[64]; snprintf(hdr, sizeof(hdr), "Panel %d - %s", i + 1, ScopeTypeName(g_set.panelScope[i]));
        if (ImGui::CollapsingHeader(hdr, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SetNextItemWidth(180);
            ScopeCombo("Scope", g_set.panelScope[i]);
            if (g_panels[i].Scope()) g_panels[i].Scope()->DrawControls(g_set);
        }
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("Debug")) {
        ImGui::Checkbox("Show test pattern source", &g_set.debugShowTestPattern);
        if (g_set.debugShowTestPattern) ImGui::Checkbox("Use test pattern", &g_set.useTestPattern);
        else g_set.useTestPattern = false;

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
                             const float peakLRGB[4], bool peaksValid, ImVec2 a0, ImVec2 a1) {
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
    const float y = a0.y + 11;  // moved 5px down so it clears the 10k line
    float w2 = line2[0] ? ImGui::CalcTextSize(line2).x : 0.0f;

    // Optional translucent black plate behind the readout for legibility.
    if (s.readoutBg) {
        float bw = std::max(total, w2);
        float bh = lineH + (line2[0] ? lineH + 2 : 0);
        ImVec2 p0(cx - bw * 0.5f - 6, y - 3), p1(cx + bw * 0.5f + 6, y + bh + 3);
        dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 140), 4.0f);
    }

    float x = cx - total * 0.5f;
    for (int i = 0; i < nseg; ++i) { dl->AddText(ImVec2(x, y), segs[i].col, segs[i].txt); x += widths[i]; }
    if (line2[0])
        dl->AddText(ImVec2(cx - w2 * 0.5f, y + lineH + 2), IM_COL32(180, 200, 235, 230), line2);
}

// Layout rects within the given content area.
static void LayoutRects(ImVec2 p0, ImVec2 p1, int count, ImVec2 out0[4], ImVec2 out1[4]) {
    float w = p1.x - p0.x, h = p1.y - p0.y;
    const float pad = 2.0f;
    if (count == 1) { out0[0] = p0; out1[0] = p1; }
    else if (count == 2) {
        out0[0] = p0; out1[0] = ImVec2(p0.x + w * 0.5f - pad, p1.y);
        out0[1] = ImVec2(p0.x + w * 0.5f + pad, p0.y); out1[1] = p1;
    } else {
        float mx = p0.x + w * 0.5f, my = p0.y + h * 0.5f;
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

    int wx = CW_USEDEFAULT, wy = CW_USEDEFAULT, ww = 1440, wh = 900;
    if (g_set.wndR > g_set.wndL && g_set.wndB > g_set.wndT) {
        wx = g_set.wndL; wy = g_set.wndT; ww = g_set.wndR - g_set.wndL; wh = g_set.wndB - g_set.wndT;
    }
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName,
        L"HDRScopes " HS_WIDEN(HDRSCOPES_VERSION), WS_OVERLAPPEDWINDOW,
        wx, wy, ww, wh, nullptr, nullptr, hInst, nullptr);
    g_hwnd = hwnd;

    if (!g_d3d.Init(hwnd)) { HDRLog("D3D init failed"); return 1; }

    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd); ImGui_ImplDX11_Init(g_d3d.Device(), g_d3d.Context());

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
        // Reserve a 5px strip at the very top so the "10k" nit label isn't clipped.
        ImVec2 scopeArea0 = ImVec2(area0.x, area0.y + 5.0f);
        ImVec2 r0[4], r1[4]; LayoutRects(scopeArea0, area1, count, r0, r1);
        for (int i = 0; i < count; ++i)
            g_panels[i].Draw(i, r0[i], r1[i], input, g_set, g_sdrWhiteNits, probe, uiB);

        // Panel borders.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < count; ++i)
            dl->AddRect(r0[i], r1[i], IM_COL32(60, 60, 60, 255));

        // Hover readout, centered over the left half (so it clears the divider/buttons
        // in 2/4-up layouts and stays over the left scope).
        ImVec2 readoutR1 = (count == 1) ? area1 : ImVec2(area0.x + (area1.x - area0.x) * 0.5f, area1.y);
        DrawHoverReadout(probe, g_set, g_sdrWhiteNits, peakLRGB, peaksValid, area0, readoutR1);

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
        const float comboW = 150.0f;
        float stripY = area0.y + 11;                                   // 5px down to clear the 10k line
        float stripX = area1.x - 240 - (count == 1 ? comboW + 8 : 0);  // 80px right, snug in the corner
        ImGui::SetCursorScreenPos(ImVec2(stripX, stripY));
        pushOpaqueWidgets();
        if (count == 1) {
            ImGui::PushID(3000); ImGui::SetNextItemWidth(comboW);
            ScopeCombo("##sc0", g_set.panelScope[0]); ImGui::PopID(); ImGui::SameLine();
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
                ImGui::SetCursorScreenPos(ImVec2(r0[i].x + 8, r0[i].y + 6));
                ImGui::PushID(2000 + i); ImGui::SetNextItemWidth(140);
                ScopeCombo("##sc", g_set.panelScope[i]);
                ImGui::PopID();
            }
            ImGui::PopStyleColor(6);
        }

        // Bottom-right FPS.
        if (g_set.showFps) {
            char fps[32]; snprintf(fps, sizeof(fps), "%.0f FPS", ImGui::GetIO().Framerate);
            float fsz = ImGui::GetFontSize() * 0.85f;            // slightly smaller font
            float w = ImGui::CalcTextSize(fps).x * 0.85f;
            dl->AddText(ImGui::GetFont(), fsz, ImVec2(area1.x - w - 16, area1.y - 24),  // 6px left, 2px up
                        IM_COL32(200, 200, 200, 220), fps);
        }

        ImGui::End();
        ImGui::PopStyleVar();

        // Controls popup appears just under the Controls button.
        DrawControlsWindow(ctrlBtnX, area0.y + 34);

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
