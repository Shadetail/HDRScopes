// Reads back a single scRGB pixel from a GPU texture (the live capture) so the
// UI can mark where the hovered pixel lands on each scope.
//
// Double-buffered + non-blocking: each frame copies the current texel into one
// staging texture and maps the OTHER (copied a frame earlier, so the GPU is done)
// with DO_NOT_WAIT. This avoids the pipeline stall a synchronous readback causes.
// The returned value is at most ~1 frame stale, which is imperceptible for a probe.
#pragma once

#include "util/Common.h"
#include <d3d11.h>

class PixelProbe {
public:
    bool Init(ID3D11Device* device);
    bool Read(ID3D11ShaderResourceView* srv, UINT srcW, UINT srcH,
              int x, int y, float outRGB[3]);
    void Shutdown();

private:
    bool EnsureStaging(DXGI_FORMAT fmt);
    ComPtr<ID3D11Device>        device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D>     staging_[2];
    DXGI_FORMAT                 stagingFmt_ = DXGI_FORMAT_UNKNOWN;
    int   writeIdx_ = 0;
    bool  pending_[2] = { false, false };
    float lastRGB_[3] = { 0, 0, 0 };
    bool  haveLast_ = false;
};
