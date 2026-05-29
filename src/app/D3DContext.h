// App shell graphics: D3D11 device/context + an HDR (scRGB FP16) swapchain.
// The swapchain uses R16G16B16A16_FLOAT with the scRGB color space so captured
// HDR nits can eventually be presented faithfully; the synthetic graph renders
// fine on it regardless.
#pragma once

#include "util/Common.h"
#include <d3d11.h>
#include <dxgi1_6.h>

class D3DContext {
public:
    bool Init(HWND hwnd);
    void Resize(UINT w, UINT h);
    void Present(bool vsync);
    void Shutdown();

    ID3D11Device*         Device()  const { return device_.Get(); }
    ID3D11DeviceContext*  Context() const { return context_.Get(); }
    ID3D11RenderTargetView* BackBufferRTV() const { return rtv_.Get(); }
    IDXGISwapChain*       SwapChain() const { return swapchain_.Get(); }
    bool                  IsHDR() const { return hdrSwapchain_; }
    UINT Width()  const { return width_; }
    UINT Height() const { return height_; }

private:
    bool CreateRTV();
    void ReleaseRTV();

    ComPtr<ID3D11Device>          device_;
    ComPtr<ID3D11DeviceContext>   context_;
    ComPtr<IDXGISwapChain1>       swapchain_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    HWND hwnd_ = nullptr;
    UINT width_ = 0, height_ = 0;
    bool hdrSwapchain_ = false;
};
