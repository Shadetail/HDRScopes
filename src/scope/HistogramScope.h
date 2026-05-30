// Histogram scope: full-frame pixel count per PQ-nit bin for L/R/G/B. Modes:
// LRGB (4 stacked bands), overlaid RGB, luma. X axis = nits, Y = count.
#pragma once
#include "scope/IScope.h"

class HistogramScope : public IScope {
public:
    const char* Name() const override { return "Histogram"; }
    bool Init(ID3D11Device*) override;
    Margins GetMargins(const Settings&) const override { return { 12.0f, 14.0f, 10.0f, 22.0f }; }
    void Compute(const ScopeInput&, const Settings&) override;
    void Render(UINT outW, UINT outH, const ScopeFrame&, const Settings&) override;
    ID3D11ShaderResourceView* Result() const override { return rtSRV_.Get(); }
    void DrawOverlay(ImDrawList*, const ScopeFrame&, Settings&) override;
    void DrawControls(Settings&) override;
    void Shutdown() override;

private:
    bool EnsureRT(UINT w, UINT h);
    float NitsToScreenX(double nits, const ScopeFrame&, const Settings&) const;
    double XAxisTop01(const ScopeFrame&, const Settings&) const;

    ComPtr<ID3D11Device> device_; ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11ComputeShader> cs_;
    ComPtr<ID3D11VertexShader> vs_; ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11Buffer> computeCB_, graphCB_;
    ComPtr<ID3D11SamplerState> linear_;
    ComPtr<ID3D11Texture2D> histTex_; ComPtr<ID3D11UnorderedAccessView> histUAV_; ComPtr<ID3D11ShaderResourceView> histSRV_;
    ComPtr<ID3D11Texture2D> rt_; ComPtr<ID3D11RenderTargetView> rtv_; ComPtr<ID3D11ShaderResourceView> rtSRV_;
    UINT bins_ = 1024, rtW_ = 0, rtH_ = 0;
    float totalSamples_ = 2073600.0f;
};
