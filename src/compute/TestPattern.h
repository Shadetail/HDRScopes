// Synthetic known-nit scRGB FP16 texture: vertical bands at 1/10/100/1000/10000
// nits. Used to validate the PQ axis placement (Step 3) before trusting live
// capture, and as a selectable source in the UI.
#pragma once

#include "util/Common.h"
#include <d3d11.h>

class TestPattern {
public:
    bool Init(ID3D11Device* device, UINT w = 1280, UINT h = 720);
    void Generate(); // run the compute shader once
    ID3D11ShaderResourceView* SRV() const { return srv_.Get(); }
    UINT Width()  const { return w_; }
    UINT Height() const { return h_; }
    void Shutdown();

private:
    ComPtr<ID3D11Device>             device_;
    ComPtr<ID3D11DeviceContext>      context_;
    ComPtr<ID3D11ComputeShader>      cs_;
    ComPtr<ID3D11Buffer>             cb_;
    ComPtr<ID3D11Texture2D>          tex_;
    ComPtr<ID3D11UnorderedAccessView> uav_;
    ComPtr<ID3D11ShaderResourceView>  srv_;
    UINT w_ = 0, h_ = 0;
    bool generated_ = false;
};
