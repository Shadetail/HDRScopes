// Renders the uint bins texture into an offscreen RGBA16F target (the trace),
// applying gain/brightness and the pan/zoom/stretch transform render-side. The
// result is shown via ImGui::Image; the graticule + labels + reference lines are
// overlaid by the UI on top of that image.
#pragma once

#include "util/Common.h"
#include <d3d11.h>

class Waveform;

class GraphView {
public:
    struct Params {
        float gain = 0.05f;
        bool  extents = false;
        float uvScaleX = 1.0f, uvScaleY = 1.0f;   // pan/zoom
        float uvOffsetX = 0.0f, uvOffsetY = 0.0f;
    };

    bool Init(ID3D11Device* device);
    // (Re)create the offscreen target at the given pixel size (the panel size).
    bool SetTargetSize(UINT w, UINT h);
    // Render the waveform into the offscreen target.
    void Render(const Waveform& wf, const Params& p);

    ID3D11ShaderResourceView* ResultSRV() const { return rtSRV_.Get(); }
    UINT Width()  const { return width_; }
    UINT Height() const { return height_; }

    void Shutdown();

private:
    ComPtr<ID3D11Device>        device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VertexShader>  vs_;
    ComPtr<ID3D11PixelShader>   ps_;
    ComPtr<ID3D11Buffer>        cb_;

    ComPtr<ID3D11Texture2D>          rt_;
    ComPtr<ID3D11RenderTargetView>   rtv_;
    ComPtr<ID3D11ShaderResourceView> rtSRV_;
    UINT width_ = 0, height_ = 0;
};
