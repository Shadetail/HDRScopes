#include "app/D3DContext.h"

bool D3DContext::Init(HWND hwnd) {
    hwnd_ = hwnd;
    RECT rc; GetClientRect(hwnd, &rc);
    width_  = (UINT)(rc.right  - rc.left);
    height_ = (UINT)(rc.bottom - rc.top);
    if (width_  == 0) width_  = 1280;
    if (height_ == 0) height_ = 720;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL got{};

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, _countof(levels), D3D11_SDK_VERSION,
        &device_, &got, &context_);
    if (FAILED(hr)) {
        // Retry without the debug layer (not installed on all machines).
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                               levels, _countof(levels), D3D11_SDK_VERSION,
                               &device_, &got, &context_);
    }
    HR_RET(hr, "D3D11CreateDevice");

    ComPtr<IDXGIDevice>  dxgiDevice;
    HR_RET(device_.As(&dxgiDevice), "QI IDXGIDevice");
    ComPtr<IDXGIAdapter> adapter;
    HR_RET(dxgiDevice->GetAdapter(&adapter), "GetAdapter");
    ComPtr<IDXGIFactory2> factory;
    HR_RET(adapter->GetParent(IID_PPV_ARGS(&factory)), "GetParent IDXGIFactory2");

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width            = width_;
    sd.Height           = height_;
    sd.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT; // scRGB FP16
    sd.SampleDesc.Count = 1;
    sd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount      = 2;
    sd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode        = DXGI_ALPHA_MODE_IGNORE;

    HR_RET(factory->CreateSwapChainForHwnd(device_.Get(), hwnd, &sd, nullptr, nullptr, &swapchain_),
           "CreateSwapChainForHwnd (FP16)");

    // Tell the compositor the buffer is scRGB (linear, Rec.709 primaries). On an
    // HDR output this lights up real HDR; on SDR it is tone-handled by the DWM.
    ComPtr<IDXGISwapChain3> sc3;
    if (SUCCEEDED(swapchain_.As(&sc3))) {
        UINT support = 0;
        const DXGI_COLOR_SPACE_TYPE cs = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        if (SUCCEEDED(sc3->CheckColorSpaceSupport(cs, &support)) &&
            (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
            if (SUCCEEDED(sc3->SetColorSpace1(cs))) {
                hdrSwapchain_ = true;
                HDRLog("[D3DContext] scRGB FP16 swapchain color space set.");
            }
        }
        if (!hdrSwapchain_)
            HDRLog("[D3DContext] scRGB color space not accepted; presenting as plain FP16.");
    }

    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    return CreateRTV();
}

bool D3DContext::CreateRTV() {
    ComPtr<ID3D11Texture2D> back;
    HR_RET(swapchain_->GetBuffer(0, IID_PPV_ARGS(&back)), "GetBuffer");
    HR_RET(device_->CreateRenderTargetView(back.Get(), nullptr, &rtv_), "CreateRenderTargetView");
    return true;
}

void D3DContext::ReleaseRTV() { rtv_.Reset(); }

void D3DContext::Resize(UINT w, UINT h) {
    if (!swapchain_ || (w == width_ && h == height_) || w == 0 || h == 0) return;
    width_ = w; height_ = h;
    ReleaseRTV();
    HRESULT hr = swapchain_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    if (!HDRCheck(hr, "ResizeBuffers")) return;
    CreateRTV();
}

void D3DContext::Present(bool vsync) {
    if (swapchain_) swapchain_->Present(vsync ? 1 : 0, 0);
}

void D3DContext::Shutdown() {
    ReleaseRTV();
    swapchain_.Reset();
    context_.Reset();
    device_.Reset();
}
