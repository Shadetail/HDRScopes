// Base for chroma scopes (vectorscope + CIE): a square 2D accumulation grid
// rendered with gain/colorize. Subclasses supply the mapping mode, plot range,
// gain, graticule overlay, and controls.
#pragma once
#include "scope/IScope.h"

class ChromaScope : public IScope {
public:
    bool Init(ID3D11Device*) override;
    float AspectRatio() const override { return 1.0f; }
    void Compute(const ScopeInput&, const Settings&) override;
    void Render(UINT outW, UINT outH, const ScopeFrame&, const Settings&) override;
    ID3D11ShaderResourceView* Result() const override { return rtSRV_.Get(); }
    void Shutdown() override;

protected:
    struct PlotRange { int mode; float minX, maxX, minY, maxY, scale; };
    virtual PlotRange Range(const Settings&) const = 0;
    virtual float Gain(const Settings&) const = 0;

    bool EnsureRT(UINT w, UINT h);

    ComPtr<ID3D11Device> device_; ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11ComputeShader> cs_;
    ComPtr<ID3D11VertexShader> vs_; ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11Buffer> computeCB_, graphCB_;
    ComPtr<ID3D11SamplerState> linear_;
    ComPtr<ID3D11Texture2D> accumTex_; ComPtr<ID3D11UnorderedAccessView> accumUAV_; ComPtr<ID3D11ShaderResourceView> accumSRV_;
    ComPtr<ID3D11Texture2D> rt_; ComPtr<ID3D11RenderTargetView> rtv_; ComPtr<ID3D11ShaderResourceView> rtSRV_;
    UINT size_ = 512, rtW_ = 0, rtH_ = 0;
};
