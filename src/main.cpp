// HDRScopes — real-time scopes for the live Windows HDR signal.
//
// Single-window UI: the scope(s) fill the client area; a gear button opens the
// controls popup; layout presets (1/2/4-up) live top-right; FPS and zoom sit in
// the bottom corners. Capture -> per-panel scope (compute+render) -> overlay.

#include "util/Common.h"
#include "util/Settings.h"
#include "util/SdrWhite.h"
#include "app/D3DContext.h"
#include "capture/CaptureSource.h"
#include "capture/Region.h"
#include "capture/RegionPicker.h"
#include "capture/PixelProbe.h"
#include "compute/TestPattern.h"
#include "scope/ScopePanel.h"
#include "scope/ScopeFactory.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <d3d11.h>
#include <timeapi.h>
#include <algorithm>

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
PixelProbe    g_probe;
Settings      g_set;
ScopePanel    g_panels[4];
HWND          g_hwnd = nullptr;
bool          g_resize = false; UINT g_resizeW = 0, g_resizeH = 0;
bool          g_showControls = false;
float         g_sdrWhiteNits = 200.0f;
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
static void DrawControlsWindow() {
    if (!g_showControls) return;
    ImGui::SetNextWindowSize(ImVec2(380, 720), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Controls", &g_showControls)) { ImGui::End(); return; }

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
        const char* q[] = { "Low", "Medium", "High", "Ultra", "Per-pixel" };
        int qi = (int)g_set.quality; ImGui::SetNextItemWidth(160);
        if (ImGui::Combo("Quality", &qi, q, 5)) g_set.quality = (Quality)qi;
        ImGui::Checkbox("Bilinear source downsample", &g_set.bilinearDownsample);
        ImGui::Checkbox("Show FPS", &g_set.showFps);
        ImGui::SetNextItemWidth(160);
        ImGui::SliderInt("FPS limit (0=off)", &g_set.fpsLimit, 0, 240);
        ImGui::Checkbox("UI follows Windows SDR white", &g_set.uiFollowSdrWhite);
        ImGui::Checkbox("Hover probe", &g_set.showHoverProbe);
    }

    if (ImGui::CollapsingHeader("Graticule", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Color", &g_set.graticuleColor.x, ImGuiColorEditFlags_NoInputs);
        ImGui::SliderFloat("Opacity", &g_set.graticuleOpacity, 0.0f, 1.0f);
    }

    int count = (int)g_set.layout;
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(1000 + i);
        char hdr[64]; snprintf(hdr, sizeof(hdr), "Panel %d — %s", i + 1, ScopeTypeName(g_set.panelScope[i]));
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
    }
    ImGui::End();
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
    RegisterClassExW(&wc);

    int wx = CW_USEDEFAULT, wy = CW_USEDEFAULT, ww = 1440, wh = 900;
    if (g_set.wndR > g_set.wndL && g_set.wndB > g_set.wndT) {
        wx = g_set.wndL; wy = g_set.wndT; ww = g_set.wndR - g_set.wndL; wh = g_set.wndB - g_set.wndT;
    }
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"HDRScopes", WS_OVERLAPPEDWINDOW,
        wx, wy, ww, wh, nullptr, nullptr, hInst, nullptr);
    g_hwnd = hwnd;

    if (!g_d3d.Init(hwnd)) { HDRLog("D3D init failed"); return 1; }

    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd); ImGui_ImplDX11_Init(g_d3d.Device(), g_d3d.Context());

    g_test.Init(g_d3d.Device());
    g_probe.Init(g_d3d.Device());
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
        ULONGLONG now = GetTickCount64();
        if (now - g_lastSdrQuery > 1000) {
            g_sdrWhiteNits = sdrwhite::QueryPrimaryNits(200.0f);
            g_lastSdrQuery = now;
        }
        float uiB = (g_d3d.IsHDR() && g_set.uiFollowSdrWhite) ? (g_sdrWhiteNits / 80.0f) : 1.0f;
        ImGui_ImplDX11_SetUIBrightness(uiB);

        // ---- Source ----
        ID3D11ShaderResourceView* srcSRV = nullptr; UINT srcW = 0, srcH = 0; RECT outRect = {};
        bool usingTest = g_set.debugShowTestPattern && g_set.useTestPattern;
        if (usingTest) { g_test.Generate(); srcSRV = g_test.SRV(); srcW = g_test.Width(); srcH = g_test.Height(); outRect = { 0,0,(LONG)srcW,(LONG)srcH }; }
        else { g_capture.AcquireFrame(); srcSRV = g_capture.SRV(); srcW = g_capture.Width(); srcH = g_capture.Height(); outRect = g_capture.DesktopRect(); }

        // ---- Region crop ----
        Region region; region.mode = (RegionMode)g_set.regionMode;
        if (g_set.regionMode == 1) region.targetWindow = g_pickedWindow;
        else if (g_set.regionMode == 2) region.desktopRect = { g_set.dragRect[0], g_set.dragRect[1], g_set.dragRect[0] + g_set.dragRect[2], g_set.dragRect[1] + g_set.dragRect[3] };
        int cx = 0, cy = 0, cw = (int)srcW, ch = (int)srcH;
        if (!(usingTest || g_set.regionMode == 0))
            if (!region.ResolveToTexel(outRect, srcW, srcH, cx, cy, cw, ch)) { cx = cy = 0; cw = srcW; ch = srcH; }

        ScopeInput input; input.srcSRV = srcSRV; input.srcW = srcW; input.srcH = srcH;
        input.cropX = cx; input.cropY = cy; input.cropW = cw; input.cropH = ch;

        // ---- Hover probe (source pixel under cursor) ----
        ScopeFrame probe;
        if (!usingTest && srcSRV) {
            POINT cur; GetCursorPos(&cur);
            if (PtInRect(&outRect, cur)) {
                int tx = cur.x - outRect.left, ty = cur.y - outRect.top;
                if (tx >= cx && tx < cx + cw && ty >= cy && ty < cy + ch) {
                    float rgb[3];
                    if (g_probe.Read(srcSRV, srcW, srcH, tx, ty, rgb)) {
                        probe.probeValid = true;
                        probe.probeRGB[0] = rgb[0]; probe.probeRGB[1] = rgb[1]; probe.probeRGB[2] = rgb[2];
                        probe.probeU = (float)(tx - cx) / std::max(1, cw);
                        probe.probeV = (float)(ty - cy) / std::max(1, ch);
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
        ImVec2 r0[4], r1[4]; LayoutRects(area0, area1, count, r0, r1);
        for (int i = 0; i < count; ++i)
            g_panels[i].Draw(i, r0[i], r1[i], input, g_set, g_sdrWhiteNits, probe, uiB);

        // Panel borders.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < count; ++i)
            dl->AddRect(r0[i], r1[i], IM_COL32(60, 60, 60, 255));

        // Top-right: layout presets + gear.
        ImGui::SetCursorScreenPos(ImVec2(area1.x - 168, area0.y + 6));
        if (ImGui::Button("1")) g_set.layout = LayoutMode::Single; ImGui::SameLine();
        if (ImGui::Button("2")) g_set.layout = LayoutMode::SideBySide; ImGui::SameLine();
        if (ImGui::Button("4")) g_set.layout = LayoutMode::Quad; ImGui::SameLine();
        if (ImGui::Button("Controls")) g_showControls = !g_showControls;

        // Per-panel scope-type quick combo (top-left of each panel).
        for (int i = 0; i < count; ++i) {
            ImGui::SetCursorScreenPos(ImVec2(r0[i].x + 6, r0[i].y + 6));
            ImGui::PushID(2000 + i); ImGui::SetNextItemWidth(140);
            ScopeCombo("##sc", g_set.panelScope[i]);
            ImGui::PopID();
        }

        // Bottom-left zoom (click to reset), bottom-right FPS.
        ImGui::SetCursorScreenPos(ImVec2(area0.x + 8, area1.y - 26));
        char zlbl[32]; snprintf(zlbl, sizeof(zlbl), "Zoom %.2fx", g_set.zoom[0]);
        if (ImGui::Button(zlbl)) { for (int i = 0; i < 4; ++i) { g_set.zoom[i] = 1; g_set.panX[i] = g_set.panY[i] = 0; } }
        if (g_set.showFps) {
            char fps[32]; snprintf(fps, sizeof(fps), "%.0f FPS", ImGui::GetIO().Framerate);
            ImVec2 ts = ImGui::CalcTextSize(fps);
            dl->AddText(ImVec2(area1.x - ts.x - 10, area1.y - 22), IM_COL32(200, 200, 200, 220), fps);
        }

        ImGui::End();
        ImGui::PopStyleVar();

        DrawControlsWindow();

        ImGui::Render();
        ID3D11RenderTargetView* rtv = g_d3d.BackBufferRTV();
        const float clear[4] = { 0.01f, 0.01f, 0.01f, 1.0f };
        g_d3d.Context()->OMSetRenderTargets(1, &rtv, nullptr);
        g_d3d.Context()->ClearRenderTargetView(rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_d3d.Present(true);

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
    g_test.Shutdown(); g_probe.Shutdown(); g_capture.Shutdown(); g_d3d.Shutdown();
    return 0;
}
