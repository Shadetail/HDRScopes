// Optional separable Gaussian blur of the captured scRGB texture, applied before
// scoping (the "source blur" option). Returns a blurred SRV; ping-pongs two
// RGBA16F targets sized to the source.
#pragma once

#include "util/Common.h"
#include <d3d11.h>

class Blur {
public:
    bool Init(ID3D11Device* device);
    // Returns a blurred SRV (or srcSRV unchanged if radius <= 0).
    ID3D11ShaderResourceView* Apply(ID3D11ShaderResourceView* srcSRV, UINT w, UINT h, float radiusPx);
    void Shutdown();

private:
    bool EnsureTargets(UINT w, UINT h);
    ComPtr<ID3D11Device> device_; ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11ComputeShader> cs_;
    ComPtr<ID3D11Buffer> cb_;
    ComPtr<ID3D11SamplerState> linear_;
    ComPtr<ID3D11Texture2D> tex_[2];
    ComPtr<ID3D11UnorderedAccessView> uav_[2];
    ComPtr<ID3D11ShaderResourceView> srv_[2];
    UINT w_ = 0, h_ = 0;
};
