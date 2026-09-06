// GPU reduction over the crop region: peak luminance and peak R/G/B (four
// independent per-channel maxima) plus the mean luminance, all in scRGB units.
//
// Double-buffered + non-blocking like PixelProbe: each frame dispatches the
// reduction and copies the result into one staging buffer, then maps the OTHER
// (copied a frame earlier, so the GPU is done). Values are ~1 frame stale.
#pragma once

#include "util/Common.h"
#include <cstdint>
#include <d3d11.h>

class PeakMeter {
public:
    bool Init(ID3D11Device* device);
    // outLRGB = { max luminance, max R, max G, max B } over the crop (scRGB);
    // outAvgLum = mean luminance of every crop pixel (scRGB).
    bool Measure(ID3D11ShaderResourceView* srv, UINT srcW, UINT srcH,
                 int cropX, int cropY, int cropW, int cropH,
                 float outLRGB[4], float& outAvgLum);
    void Shutdown();

private:
    ComPtr<ID3D11Device>              device_;
    ComPtr<ID3D11DeviceContext>       context_;
    ComPtr<ID3D11ComputeShader>       cs_;
    ComPtr<ID3D11Buffer>              cb_, buf_, staging_[2];
    ComPtr<ID3D11UnorderedAccessView> uav_;
    int      writeIdx_ = 0;
    bool     pending_[2] = { false, false };
    uint64_t pendingPx_[2] = { 0, 0 };  // crop pixel count each in-flight copy summed over
    float    last_[4] = { 0, 0, 0, 0 };
    float    lastAvg_ = 0.0f;
    bool     haveLast_ = false;
};
