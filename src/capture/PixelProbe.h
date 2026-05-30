// Reads back a single scRGB pixel from a GPU texture (the live capture) so the
// UI can mark where the hovered pixel lands on each scope. One tiny 1x1 staging
// copy per frame — negligible cost.
#pragma once

#include "util/Common.h"
#include <d3d11.h>

class PixelProbe {
public:
    bool Init(ID3D11Device* device);
    // Copy texel (x,y) from the SRV's resource and decode to scRGB. Returns false
    // if out of range or the format is unsupported.
    bool Read(ID3D11ShaderResourceView* srv, UINT srcW, UINT srcH,
              int x, int y, float outRGB[3]);
    void Shutdown();

private:
    bool EnsureStaging(DXGI_FORMAT fmt);
    ComPtr<ID3D11Device>        device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D>     staging_;
    DXGI_FORMAT                 stagingFmt_ = DXGI_FORMAT_UNKNOWN;
};
