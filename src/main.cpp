// HDRScopes — real-time waveform scope for the live Windows HDR signal.
//
// App shell: Win32 window + D3D11 + scRGB FP16 (HDR) swapchain + ImGui, driving
// CaptureSource (DDA) -> Waveform (compute) -> GraphView (render) -> graticule.

#include "util/Common.h"
#include "app/D3DContext.h"
#include "capture/CaptureSource.h"
#include "capture/Region.h"
#include "compute/Waveform.h"
#include "compute/TestPattern.h"
#include "render/GraphView.h"
#include "render/Graticule.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <d3d11.h>
#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ---- Global app state --------------------------------------------------------
namespace {
D3DContext   g_d3d;
CaptureSource g_capture;
TestPattern  g_test;
Waveform     g_wave;
GraphView    g_graph;
bool         g_resize = false;
UINT         g_resizeW = 0, g_resizeH = 0;

enum class SourceMode { Live, TestPattern };

struct UIState {
    SourceMode source = SourceMode::Live;
    int   waveMode = 1;            // product default: RGB (0 = Luminance, 1 = RGB)
    float gain = 0.05f;
    float graticuleOpacity = 0.55f;
    bool  extents = true;          // product default: Extents on
    bool  refEnabled[2] = { false, false };
    double refNits[2] = { 100.0, 1000.0 };
    // pan/zoom
    float zoom = 1.0f;
    float panX = 0.0f, panY = 0.0f; // in uv units
    // region
    int   regionMode = 0;          // 0 full, 1 window, 2 drag-rect
    RECT  dragRect = { 0, 0, 1920, 1080 };
    HWND  pickedWindow = nullptr;
    unsigned long long pickArmUntil = 0;
    // output selection
    int   outputSel = -1;
};
UIState g_ui;
}

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return true;
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            g_resize = true;
            g_resizeW = (UINT)LOWORD(lp);
            g_resizeH = (UINT)HIWORD(lp);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// Pick a sensible default capture point: center of the first HDR output.
static POINT DefaultCapturePoint() {
    auto outs = CaptureSource::EnumerateOutputs();
    for (auto& o : outs) {
        if (o.hdr)
            return POINT{ (o.rect.left + o.rect.right) / 2, (o.rect.top + o.rect.bottom) / 2 };
    }
    if (!outs.empty())
        return POINT{ (outs[0].rect.left + outs[0].rect.right) / 2,
                      (outs[0].rect.top + outs[0].rect.bottom) / 2 };
    return POINT{ 0, 0 };
}

// ---- UI ----------------------------------------------------------------------
static void DrawControls() {
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 660), ImGuiCond_FirstUseEver);
    ImGui::Begin("Controls");

    ImGui::SeparatorText("Source");
    int src = (int)g_ui.source;
    if (ImGui::RadioButton("Live capture", &src, (int)SourceMode::Live)) g_ui.source = SourceMode::Live;
    ImGui::SameLine();
    if (ImGui::RadioButton("Test pattern", &src, (int)SourceMode::TestPattern)) g_ui.source = SourceMode::TestPattern;

    if (g_ui.source == SourceMode::Live) {
        // Output selector (live switching).
        auto outs = CaptureSource::EnumerateOutputs();
        int cur = g_capture.OutputIndex();
        std::string preview = "Output " + std::to_string(cur);
        for (auto& o : outs) if (o.index == cur) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%d: %dx%d %s", o.index,
                     o.rect.right - o.rect.left, o.rect.bottom - o.rect.top,
                     o.hdr ? "HDR" : "SDR");
            preview = buf;
        }
        if (ImGui::BeginCombo("Monitor", preview.c_str())) {
            for (auto& o : outs) {
                char buf[128];
                snprintf(buf, sizeof(buf), "%d: %dx%d %s", o.index,
                         o.rect.right - o.rect.left, o.rect.bottom - o.rect.top,
                         o.hdr ? "HDR" : "SDR");
                bool sel = (o.index == cur);
                if (ImGui::Selectable(buf, sel)) g_capture.RetargetToIndex(o.index);
            }
            ImGui::EndCombo();
        }
        ImGui::Text("Captured: %ux%u  %s", g_capture.Width(), g_capture.Height(),
                    g_capture.IsHDR() ? "HDR scRGB" : "SDR");
    }

    ImGui::SeparatorText("Region");
    ImGui::RadioButton("Full output", &g_ui.regionMode, 0); ImGui::SameLine();
    ImGui::RadioButton("Window", &g_ui.regionMode, 1);      ImGui::SameLine();
    ImGui::RadioButton("Rect", &g_ui.regionMode, 2);
    if (g_ui.regionMode == 1) {
        if (ImGui::Button("Pick window under cursor (2s)"))
            g_ui.pickArmUntil = GetTickCount64() + 2000;
        if (g_ui.pickArmUntil) {
            long long left = (long long)g_ui.pickArmUntil - (long long)GetTickCount64();
            if (left > 0) ImGui::Text("Move cursor over target window... %lld ms", left);
            else {
                POINT pt; GetCursorPos(&pt);
                HWND w = WindowFromPoint(pt);
                if (w) g_ui.pickedWindow = GetAncestor(w, GA_ROOT);
                g_ui.pickArmUntil = 0;
            }
        }
        if (g_ui.pickedWindow) {
            RECT r; GetWindowRect(g_ui.pickedWindow, &r);
            ImGui::Text("Window rect: %ld,%ld  %ldx%ld", r.left, r.top, r.right - r.left, r.bottom - r.top);
        }
    } else if (g_ui.regionMode == 2) {
        int v[4] = { g_ui.dragRect.left, g_ui.dragRect.top,
                     g_ui.dragRect.right - g_ui.dragRect.left,
                     g_ui.dragRect.bottom - g_ui.dragRect.top };
        if (ImGui::InputInt4("L,T,W,H (desktop)", v)) {
            g_ui.dragRect.left = v[0]; g_ui.dragRect.top = v[1];
            g_ui.dragRect.right = v[0] + v[2]; g_ui.dragRect.bottom = v[1] + v[3];
        }
    }

    ImGui::SeparatorText("Waveform");
    ImGui::RadioButton("Luminance", &g_ui.waveMode, 0); ImGui::SameLine();
    ImGui::RadioButton("RGB", &g_ui.waveMode, 1);
    ImGui::Checkbox("Extents (over/undershoot)", &g_ui.extents);
    ImGui::SliderFloat("Brightness / gain", &g_ui.gain, 0.001f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Graticule opacity", &g_ui.graticuleOpacity, 0.0f, 1.0f);

    ImGui::SeparatorText("Reference lines");
    for (int i = 0; i < 2; ++i) {
        ImGui::PushID(i);
        ImGui::Checkbox("##en", &g_ui.refEnabled[i]); ImGui::SameLine();
        float n = (float)g_ui.refNits[i];
        if (ImGui::SliderFloat("nits", &n, 0.0f, 10000.0f, "%.0f", ImGuiSliderFlags_Logarithmic))
            g_ui.refNits[i] = n;
        ImGui::PopID();
    }

    ImGui::SeparatorText("View");
    if (ImGui::Button("Reset pan/zoom")) { g_ui.zoom = 1.0f; g_ui.panX = g_ui.panY = 0.0f; }
    ImGui::Text("Zoom %.2fx", g_ui.zoom);

    ImGui::Separator();
    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
    ImGui::End();
}

// Draw the graph image + graticule + interaction in its own window.
static void DrawGraphWindow() {
    ImGui::SetNextWindowPos(ImVec2(400, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(1400, 820), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Waveform");
    ImVec2 avail = ImGui::GetContentRegionAvail();
    UINT pw = (UINT)(avail.x < 16 ? 16 : avail.x);
    UINT ph = (UINT)(avail.y < 16 ? 16 : avail.y);
    g_graph.SetTargetSize(pw, ph);

    // Render the trace into the offscreen RT for this frame.
    GraphView::Params gp;
    gp.gain = g_ui.gain;
    gp.extents = g_ui.extents;
    gp.uvScaleX = 1.0f / g_ui.zoom;
    gp.uvScaleY = 1.0f / g_ui.zoom;
    gp.uvOffsetX = g_ui.panX;
    gp.uvOffsetY = g_ui.panY;
    g_graph.Render(g_wave, gp);

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)g_graph.ResultSRV(), ImVec2((float)pw, (float)ph));
    ImVec2 p1 = ImVec2(p0.x + pw, p0.y + ph);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    graticule::DrawGraticule(dl, p0, p1, g_ui.graticuleOpacity);

    // Reference lines (draggable).
    const ImU32 refCols[2] = { IM_COL32(255, 220, 80, 220), IM_COL32(80, 200, 255, 220) };
    ImGuiIO& io = ImGui::GetIO();
    for (int i = 0; i < 2; ++i) {
        if (!g_ui.refEnabled[i]) continue;
        graticule::DrawRefLine(dl, p0, p1, g_ui.refNits[i], refCols[i]);
        float y = graticule::NitsToY(g_ui.refNits[i], p0.y, p1.y - p0.y);
        if (ImGui::IsWindowHovered() && io.MouseDown[0] &&
            fabsf(io.MousePos.y - y) < 6.0f && io.MousePos.x >= p0.x && io.MousePos.x <= p1.x) {
            g_ui.refNits[i] = graticule::YToNits(io.MousePos.y, p0.y, p1.y - p0.y);
        }
    }

    // Pan/zoom interaction over the image.
    if (ImGui::IsWindowHovered()) {
        if (io.MouseWheel != 0.0f) {
            float old = g_ui.zoom;
            g_ui.zoom = std::clamp(g_ui.zoom * (1.0f + io.MouseWheel * 0.1f), 1.0f, 20.0f);
            (void)old;
        }
        if (io.MouseDown[2]) { // middle-drag pan
            g_ui.panX -= io.MouseDelta.x / (float)pw / g_ui.zoom;
            g_ui.panY -= io.MouseDelta.y / (float)ph / g_ui.zoom;
        }
    }
    // Clamp pan so the (scaled) sample window stays in 0..1.
    float span = 1.0f / g_ui.zoom;
    g_ui.panX = std::clamp(g_ui.panX, 0.0f, 1.0f - span);
    g_ui.panY = std::clamp(g_ui.panY, 0.0f, 1.0f - span);

    ImGui::End();
    ImGui::PopStyleVar();
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    // IDXGIOutput5::DuplicateOutput1 fails with DXGI_ERROR_UNSUPPORTED unless the
    // process is per-monitor DPI aware. Must be set before any DDA call.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"HDRScopesWnd";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"HDRScopes - Waveform",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1440, 900,
        nullptr, nullptr, hInst, nullptr);

    if (!g_d3d.Init(hwnd)) { HDRLog("D3D init failed"); return 1; }

    // ImGui setup.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().IniFilename = nullptr; // use code-defined layout, ignore stale imgui.ini
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_d3d.Device(), g_d3d.Context());

    // Modules.
    if (!g_wave.Init(g_d3d.Device()))  { HDRLog("Waveform init failed"); return 1; }
    if (!g_graph.Init(g_d3d.Device())) { HDRLog("GraphView init failed"); return 1; }
    g_test.Init(g_d3d.Device());
    g_capture.Init(g_d3d.Device(), DefaultCapturePoint());

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        if (g_resize) {
            g_d3d.Resize(g_resizeW, g_resizeH);
            ImGui_ImplDX11_InvalidateDeviceObjects();
            ImGui_ImplDX11_CreateDeviceObjects();
            g_resize = false;
        }

        // ---- Acquire source ----
        ID3D11ShaderResourceView* srcSRV = nullptr;
        UINT srcW = 0, srcH = 0;
        RECT outRect = {};
        if (g_ui.source == SourceMode::TestPattern) {
            g_test.Generate();
            srcSRV = g_test.SRV(); srcW = g_test.Width(); srcH = g_test.Height();
            outRect = { 0, 0, (LONG)srcW, (LONG)srcH };
        } else {
            g_capture.AcquireFrame();
            srcSRV = g_capture.SRV(); srcW = g_capture.Width(); srcH = g_capture.Height();
            outRect = g_capture.DesktopRect();
        }

        // ---- Region crop ----
        Region region;
        region.mode = (RegionMode)g_ui.regionMode;
        if (g_ui.regionMode == 1) { region.targetWindow = g_ui.pickedWindow; }
        else if (g_ui.regionMode == 2) { region.desktopRect = g_ui.dragRect; }

        int cx = 0, cy = 0, cw = (int)srcW, ch = (int)srcH;
        if (srcSRV) {
            if (g_ui.source == SourceMode::TestPattern || g_ui.regionMode == 0) {
                cx = 0; cy = 0; cw = (int)srcW; ch = (int)srcH;
            } else {
                if (!region.ResolveToTexel(outRect, srcW, srcH, cx, cy, cw, ch)) {
                    cx = 0; cy = 0; cw = (int)srcW; ch = (int)srcH;
                }
            }
        }

        // ---- Compute waveform ----
        g_wave.SetMode(g_ui.waveMode == 1 ? Waveform::Mode::RGB : Waveform::Mode::Luminance);
        if (srcSRV && cw > 0 && ch > 0)
            g_wave.Dispatch(srcSRV, srcW, srcH, cx, cy, cw, ch);

        // ---- ImGui frame ----
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        DrawControls();
        DrawGraphWindow(); // also renders the offscreen trace for this frame

        ImGui::Render();

        ID3D11RenderTargetView* rtv = g_d3d.BackBufferRTV();
        const float clear[4] = { 0.02f, 0.02f, 0.02f, 1.0f };
        g_d3d.Context()->OMSetRenderTargets(1, &rtv, nullptr);
        g_d3d.Context()->ClearRenderTargetView(rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_d3d.Present(true);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_graph.Shutdown(); g_wave.Shutdown(); g_test.Shutdown();
    g_capture.Shutdown(); g_d3d.Shutdown();
    return 0;
}
