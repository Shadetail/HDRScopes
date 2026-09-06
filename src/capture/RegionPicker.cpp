#include "capture/RegionPicker.h"
#include "util/UiFonts.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <dxgi1_6.h>
#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace regionpicker {

static const wchar_t* kClass = L"HDRScopesRegionPicker";

// Linear-light multiplier for the unselected area. The old GDI picker blended
// 55% black in gamma space (0.45 encoded is ~0.17 linear); a touch brighter
// reads the same on an HDR desktop, where the image has more room above SDR.
static const float kDimLinear = 0.22f;

struct PickState {
    RECT          output{};
    int           w = 0, h = 0;
    ImGuiContext* ctx = nullptr;   // set only while the ImGui backends are live
    bool          dragging = false, done = false, ok = false;
    POINT         start{}, cur{};
};
static PickState* g_ps = nullptr;

static RECT NormRect(POINT a, POINT b) {
    RECT r;
    r.left   = std::min(a.x, b.x); r.top    = std::min(a.y, b.y);
    r.right  = std::max(a.x, b.x); r.bottom = std::max(a.y, b.y);
    return r;
}

static bool UsableRect(const RECT& r) { return r.right - r.left >= 4 && r.bottom - r.top >= 4; }

// Mouse position from lParam, clamped to the window: under capture the drag
// can leave the output, and the readout/result must stay inside it.
static POINT ClampedPoint(LPARAM l, const PickState& s) {
    POINT p = { (int)(short)LOWORD(l), (int)(short)HIWORD(l) };
    p.x = std::clamp(p.x, 0L, (LONG)s.w);
    p.y = std::clamp(p.y, 0L, (LONG)s.h);
    return p;
}

static LRESULT CALLBACK PickProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    PickState* s = g_ps;
    if (!s) return DefWindowProc(h, m, w, l);
    // Keep the ImGui platform backend informed (mouse/focus state). It acts on
    // whichever context is current — normally the app's, see the modal loop —
    // so pin ours for the call. Gated on ctx: this proc also runs before the
    // backends exist (window creation) and after they are gone (destruction).
    if (s->ctx) {
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(s->ctx);
        LRESULT r = ImGui_ImplWin32_WndProcHandler(h, m, w, l);
        ImGui::SetCurrentContext(prev);
        if (r) return r;
    }
    switch (m) {
    case WM_LBUTTONDOWN:
        s->dragging = true;
        s->start = ClampedPoint(l, *s);
        s->cur = s->start;
        SetCapture(h);
        return 0;
    case WM_MOUSEMOVE:
        if (s->dragging) s->cur = ClampedPoint(l, *s);
        return 0;
    case WM_LBUTTONUP:
        if (s->dragging) {
            s->dragging = false;
            ReleaseCapture();
            s->ok = UsableRect(NormRect(s->start, s->cur));
            s->done = true;
        }
        return 0;
    case WM_RBUTTONDOWN:
        s->ok = false; s->done = true;
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { s->ok = false; s->done = true; }
        else if (w == VK_RETURN) { s->ok = UsableRect(NormRect(s->start, s->cur)); s->done = true; }
        return 0;
    case WM_DPICHANGED:
        return 0; // pinned to one output at its native size; nothing to rescale
    case WM_ERASEBKGND:
        return 1; // the swapchain paints everything
    }
    return DefWindowProc(h, m, w, l);
}

// GPU objects for one picker session. Declared views-first so even implicit
// destruction releases them before the chain they view.
struct Gfx {
    ComPtr<ID3D11RenderTargetView>   rtv;
    ComPtr<ID3D11ShaderResourceView> frameSRV;
    ComPtr<ID3D11Texture2D>          frame;
    ComPtr<IDXGISwapChain1>          swap;
    ComPtr<IDXGIFactory2>            factory;
    ComPtr<ID3D11DeviceContext>      ctx;
    ComPtr<ID3D11Device>             device;
};

// Views before the chain (like D3DContext::Shutdown), with the back buffer
// unbound first. Creating the picker chain also moved DXGI's window
// association (Alt+Enter monitoring) off the app window, so hand it back with
// the app's setting.
static void ReleaseGfx(Gfx& g, HWND appWindow) {
    if (g.ctx) g.ctx->OMSetRenderTargets(0, nullptr, nullptr);
    g.rtv.Reset(); g.frameSRV.Reset(); g.frame.Reset();
    g.swap.Reset();
    if (g.factory && appWindow) g.factory->MakeWindowAssociation(appWindow, DXGI_MWA_NO_ALT_ENTER);
    g.factory.Reset(); g.ctx.Reset(); g.device.Reset();
}

// Same recipe as the app window (D3DContext): FP16 flip-model chain tagged
// scRGB, so on an HDR output the frozen frame's nit values pass straight
// through to the display.
static bool CreateSwapchain(Gfx& g, HWND hwnd, int w, int h) {
    ComPtr<IDXGIDevice> dxgiDevice;
    HR_RET(g.device.As(&dxgiDevice), "picker QI IDXGIDevice");
    ComPtr<IDXGIAdapter> adapter;
    HR_RET(dxgiDevice->GetAdapter(&adapter), "picker GetAdapter");
    HR_RET(adapter->GetParent(IID_PPV_ARGS(&g.factory)), "picker GetParent IDXGIFactory2");

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width            = (UINT)w;
    sd.Height           = (UINT)h;
    sd.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount      = 2;
    sd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode        = DXGI_ALPHA_MODE_IGNORE;
    HR_RET(g.factory->CreateSwapChainForHwnd(g.device.Get(), hwnd, &sd, nullptr, nullptr, &g.swap),
           "picker CreateSwapChainForHwnd (FP16)");

    bool scrgb = false;
    ComPtr<IDXGISwapChain3> sc3;
    if (SUCCEEDED(g.swap.As(&sc3))) {
        UINT support = 0;
        const DXGI_COLOR_SPACE_TYPE cs = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        if (SUCCEEDED(sc3->CheckColorSpaceSupport(cs, &support)) &&
            (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
            scrgb = SUCCEEDED(sc3->SetColorSpace1(cs));
    }
    if (!scrgb) HDRLog("[RegionPicker] scRGB color space not accepted; presenting as plain FP16.");
    g.factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

    ComPtr<ID3D11Texture2D> back;
    HR_RET(g.swap->GetBuffer(0, IID_PPV_ARGS(&back)), "picker GetBuffer");
    HR_RET(g.device->CreateRenderTargetView(back.Get(), nullptr, &g.rtv), "picker CreateRenderTargetView");
    return true;
}

// Copies the capture texture so the preview stays frozen while the live
// capture keeps running. 8-bit captures are sRGB-encoded, and the ImGui shader
// would feed them to the linear scRGB chain as-is (washed out), so the copy is
// typeless (CopyResource stays legal from the UNORM source) and viewed as
// *_SRGB so sampling linearizes. Float captures are already scRGB.
static bool FreezeFrame(Gfx& g, ID3D11ShaderResourceView* srv, int w, int h) {
    ComPtr<ID3D11Resource> res;
    srv->GetResource(&res);
    ComPtr<ID3D11Texture2D> src;
    HR_RET(res.As(&src), "picker capture resource is not a Texture2D");
    D3D11_TEXTURE2D_DESC td = {};
    src->GetDesc(&td);
    if (td.Width != (UINT)w || td.Height != (UINT)h) {
        // Another output's frame (e.g. kept after a failed retarget) must not
        // be stretched over this one.
        HDRLog("[RegionPicker] capture %ux%u doesn't match output %dx%d; no preview",
               td.Width, td.Height, w, h);
        return false;
    }

    DXGI_FORMAT texFmt = td.Format, viewFmt = td.Format;
    switch (td.Format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        texFmt = DXGI_FORMAT_B8G8R8A8_TYPELESS; viewFmt = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; break;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        texFmt = DXGI_FORMAT_R8G8B8A8_TYPELESS; viewFmt = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; break;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        break;
    default:
        // R10G10B10A2 would be HDR10 PQ/Rec.2020 and needs a decode this
        // passthrough path doesn't have; the duplicator only hands it out on
        // exotic setups, so the picker just works over black there.
        HDRLog("[RegionPicker] no preview for capture format %d", (int)td.Format);
        return false;
    }

    td.Format = texFmt;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = 0;
    td.MiscFlags = 0;
    HR_RET(g.device->CreateTexture2D(&td, nullptr, &g.frame), "picker frozen-frame texture");

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = viewFmt;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    HR_RET(g.device->CreateShaderResourceView(g.frame.Get(), &sd, &g.frameSRV), "picker frozen-frame SRV");

    g.ctx->CopyResource(g.frame.Get(), src.Get());
    return true;
}

static ImU32 Gray(float v) { return ImGui::GetColorU32(ImVec4(v, v, v, 1.0f)); }

// Text on an opaque black plate (0 nits on HDR, so it reads over anything).
static void PlateText(ImDrawList* dl, ImVec2 p, float pad, ImU32 col, const char* txt) {
    ImVec2 ts = ImGui::CalcTextSize(txt);
    dl->AddRectFilled(ImVec2(p.x - pad, p.y - pad), ImVec2(p.x + ts.x + pad, p.y + ts.y + pad), IM_COL32(0, 0, 0, 255));
    dl->AddText(p, col, txt);
}

// ImGui blends SRC_ALPHA / INV_SRC_ALPHA, but a duplicated desktop frame does
// not guarantee alpha = 1 (a 0 would render black), so the frame is drawn
// with blending off; that also makes the dim exact instead of alpha-weighted.
static void OpaqueBlend(const ImDrawList*, const ImDrawCmd* cmd) {
    ((ID3D11DeviceContext*)cmd->UserCallbackData)->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
}

// brightness is the backend's global UI multiplier (SDR white / 80 on HDR):
// text and border get it so they sit at SDR white; the image tints cancel it
// so the frozen frame reaches the swapchain at its captured scRGB values.
static void DrawFrame(const PickState& s, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* frame,
                      float brightness, float scale) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float inv = 1.0f / brightness;
    const RECT sel = NormRect(s.start, s.cur);
    const int sw = sel.right - sel.left, sh = sel.bottom - sel.top;
    const bool haveSel = sw > 0 && sh > 0;
    const ImVec2 a((float)sel.left, (float)sel.top), b((float)sel.right, (float)sel.bottom);

    if (frame) {
        dl->AddCallback(OpaqueBlend, ctx);
        dl->AddImage((ImTextureID)frame, ImVec2(0, 0), ImVec2((float)s.w, (float)s.h),
                     ImVec2(0, 0), ImVec2(1, 1), Gray(inv * kDimLinear));
        if (haveSel)
            dl->AddImage((ImTextureID)frame, a, b, ImVec2(a.x / s.w, a.y / s.h), ImVec2(b.x / s.w, b.y / s.h), Gray(inv));
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }

    if (haveSel) {
        dl->AddRect(a, b, IM_COL32(80, 200, 255, 255), 0.0f, 0, 2.0f * scale);
        char txt[80];
        snprintf(txt, sizeof(txt), "%d x %d  @ (%ld, %ld)", sw, sh, sel.left + s.output.left, sel.top + s.output.top);
        const float pad = 3.0f * scale, lineH = ImGui::GetTextLineHeight();
        // Above the selection when there's room, else just below it.
        float ty = (a.y > lineH + 8.0f * scale) ? a.y - lineH - 6.0f * scale : b.y + 4.0f * scale + pad;
        PlateText(dl, ImVec2(a.x + 2.0f * scale + pad, ty), pad, IM_COL32(255, 255, 255, 255), txt);
    }

    PlateText(dl, ImVec2(16.0f * scale, 16.0f * scale), 3.0f * scale, IM_COL32(230, 230, 230, 255),
              "Drag to select region   |   release / Enter = confirm   |   Esc / right-click = cancel");
}

bool PickScreenRegion(const Source& src, const RECT& outputRect, RECT& out) {
    PickState s;
    s.output = outputRect;
    s.w = outputRect.right - outputRect.left;
    s.h = outputRect.bottom - outputRect.top;
    if (s.w <= 0 || s.h <= 0 || !src.device) return false;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = PickProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);  // fails harmlessly once registered

    HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClass, L"",
        WS_POPUP, outputRect.left, outputRect.top, s.w, s.h,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!h) return false;

    Gfx g;
    g.device = src.device;
    g.device->GetImmediateContext(&g.ctx);
    if (!CreateSwapchain(g, h, s.w, s.h)) {
        HDRLog("[RegionPicker] swapchain unavailable; picker cancelled.");
        ReleaseGfx(g, src.appWindow);
        DestroyWindow(h);
        return false;
    }
    if (src.srv && !FreezeFrame(g, src.srv, s.w, s.h)) { g.frameSRV.Reset(); g.frame.Reset(); }

    // The backend's brightness multiplier is global: set it for the picker and
    // put the app's back afterwards. SDR white is never below 80 nits, so the
    // value is >= 1 already; the clamp keeps the image's 1/brightness tint
    // representable in ImGui's 8-bit vertex colors.
    const float brightness = std::max(1.0f, src.uiBrightness);
    const float appBrightness = ImGui_ImplDX11_GetUIBrightness();

    // Second ImGui context on the same device. It is current only while the
    // picker builds/renders a frame (and for backend init/shutdown). While
    // messages are pumped the app's context stays current: the hidden main
    // window still receives messages (WM_MOUSELEAVE, focus) and its WndProc
    // must feed the app's backend state, not ours — PickProc pins ours for
    // the picker window's own messages.
    ImGuiContext* appCtx = ImGui::GetCurrentContext();
    ImGuiContext* ctx = ImGui::CreateContext();
    // CreateContext leaves the app's context current when one exists, so the
    // backends below would otherwise be (re)initialized onto the app's context.
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;  // keep the class cross cursor
    const float scale = (float)GetDpiForWindow(h) / 96.0f;
    BuildUiFonts(io, scale);
    ImGui_ImplWin32_Init(h);
    ImGui_ImplDX11_Init(g.device.Get(), g.ctx.Get());
    ImGui_ImplDX11_SetUIBrightness(brightness);
    s.ctx = ctx;
    ImGui::SetCurrentContext(appCtx);

    g_ps = &s;
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    SetFocus(h);

    bool presentFailed = false;
    while (!s.done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage((int)msg.wParam);  // re-post for the app's loop
                s.ok = false; s.done = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (s.done) break;

        ImGui::SetCurrentContext(ctx);
        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        DrawFrame(s, g.ctx.Get(), g.frameSRV.Get(), brightness, scale);
        ImGui::Render();
        const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        ID3D11RenderTargetView* rtv = g.rtv.Get();
        g.ctx->OMSetRenderTargets(1, &rtv, nullptr);
        g.ctx->ClearRenderTargetView(rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        HRESULT hr = g.swap->Present(1, 0);
        ImGui::SetCurrentContext(appCtx);
        if (FAILED(hr)) {
            // Keep taking input (Esc still cancels) without spinning a core
            // on a removed device — vsync no longer paces the loop.
            if (!presentFailed) HDRLog("[RegionPicker] Present failed hr=0x%08lx", (unsigned long)hr);
            presentFailed = true;
            Sleep(16);
        }
    }

    if (GetCapture() == h) ReleaseCapture();
    s.ctx = nullptr;  // stop forwarding before the backends go away
    ImGui::SetCurrentContext(ctx);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(appCtx);
    ImGui_ImplDX11_SetUIBrightness(appBrightness);
    g_ps = nullptr;

    ReleaseGfx(g, src.appWindow);
    DestroyWindow(h);

    if (s.ok) {
        RECT sel = NormRect(s.start, s.cur);
        out.left   = sel.left   + outputRect.left;
        out.top    = sel.top    + outputRect.top;
        out.right  = sel.right  + outputRect.left;
        out.bottom = sel.bottom + outputRect.top;
    }
    return s.ok;
}

} // namespace regionpicker
