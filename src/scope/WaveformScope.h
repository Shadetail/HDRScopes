// Waveform scope: per-column PQ-nit histogram of the captured scRGB image, with
// luminance / RGB modes, channel toggles, colorize, supersampled extents (points
// or white envelope line), reference lines, SDR-white vertical zoom, and a hover
// probe. Owns its compute resources and an offscreen render target.
#pragma once

#include "scope/IScope.h"
#include <vector>

class WaveformScope : public IScope {
public:
    const char* Name() const override { return "Waveform"; }
    bool Init(ID3D11Device*) override;
    Margins GetMargins(const Settings&) const override;
    void Compute(const ScopeInput&, const Settings&) override;
    void Render(UINT outW, UINT outH, const ScopeFrame&, const Settings&) override;
    ID3D11ShaderResourceView* Result() const override { return rtSRV_.Get(); }
    void DrawOverlay(ImDrawList*, const ScopeFrame&, Settings&) override;
    void DrawControls(Settings&) override;
    void Shutdown() override;

private:
    void DimsFor(const Settings&, int cropW, int cropH,
                 UINT& graphCols, UINT& bins, UINT& sampleW, UINT& sampleH, UINT& extCols);
    bool EnsureBins(UINT graphCols, UINT bins, UINT channels, UINT extCols);
    bool EnsureRT(UINT w, UINT h);
    void ReadbackExtents();

    // Axis mapping (mirrors the shader exactly).
    float NitsToScreenY(double nits, const ScopeFrame&, const Settings&) const;
    double ScreenYToNits(float y, const ScopeFrame&, const Settings&) const;
    double YAxisTop01(const ScopeFrame&, const Settings&) const;

    ComPtr<ID3D11Device>        device_;
    ComPtr<ID3D11DeviceContext> context_;

    // Compute.
    ComPtr<ID3D11ComputeShader> csClearExt_, csHisto_;
    ComPtr<ID3D11Buffer>        computeCB_;
    ComPtr<ID3D11SamplerState>  linear_;
    ComPtr<ID3D11Texture2D>           binsTex_, extTex_, extStaging_;
    ComPtr<ID3D11UnorderedAccessView> binsUAV_, extUAV_;
    ComPtr<ID3D11ShaderResourceView>  binsSRV_, extSRV_;

    // Render.
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader>  ps_;
    ComPtr<ID3D11Buffer>       graphCB_;
    ComPtr<ID3D11Texture2D>          rt_;
    ComPtr<ID3D11RenderTargetView>   rtv_;
    ComPtr<ID3D11ShaderResourceView> rtSRV_;

    UINT graphCols_ = 0, bins_ = 0, channels_ = 0, extCols_ = 0;
    UINT rtW_ = 0, rtH_ = 0;
    int  curMode_ = 1;
    float perColSamples_ = 2025.0f; // for quality-invariant brightness

    // CPU copy of extents for the white-line style.
    std::vector<uint32_t> extData_; // extCols_ * 2 * channels_
    bool extReadbackValid_ = false;

    int  draggingRef_ = -1;
};
