// Live HDR display capture via DXGI Desktop Duplication (DDA).
//
// Differs from SKIV's one-shot snip in one way: the duplicator is kept alive
// across frames (a persistent acquire/copy/release loop). When the desktop has
// not changed, the last captured texture is reused, so a static screen costs
// ~nothing. DXGI_ERROR_ACCESS_LOST (mode/HDR/fullscreen change) tears down and
// rebuilds the duplicator; the very same path is reused to retarget a different
// output at runtime.
//
// On an HDR output the captured surface is scRGB FP16 (linear, Rec.709
// primaries, 1.0 = 80 nits) — exactly what the color math downstream assumes.
#pragma once

#include "util/Common.h"
#include <d3d11.h>
#include <dxgi1_6.h>

class CaptureSource {
public:
    bool Init(ID3D11Device* device, POINT pickPoint);
    // Retarget to whichever output contains pickPoint (runtime monitor switch).
    bool RetargetToPoint(POINT pickPoint);
    // Retarget to a specific output index across all adapters (0-based).
    bool RetargetToIndex(int outputIndex);

    // Poll once. Returns true if a usable texture is available (fresh or reused).
    // Never blocks longer than ~timeoutMs.
    bool AcquireFrame(UINT timeoutMs = 8);

    void Shutdown();

    ID3D11ShaderResourceView* SRV() const { return srv_.Get(); }
    UINT Width()  const { return width_; }
    UINT Height() const { return height_; }
    RECT DesktopRect() const { return desktopRect_; }
    int  OutputIndex() const { return curOutputIndex_; }
    bool IsHDR() const { return isHDR_; }
    DXGI_FORMAT Format() const { return format_; }
    bool HasFrame() const { return hasFrame_; }

    // Enumerate outputs for the picker UI. Returns count; fills names/rects.
    struct OutputInfo { std::wstring name; RECT rect; bool hdr; int index; };
    static std::vector<OutputInfo> EnumerateOutputs();

private:
    bool SelectOutput(POINT pt, int explicitIndex);
    bool CreateDuplicator();
    void ReleaseDuplicator();
    bool EnsureTexture(UINT w, UINT h, DXGI_FORMAT fmt);

    ComPtr<ID3D11Device>            device_;
    ComPtr<ID3D11DeviceContext>     context_;
    ComPtr<IDXGIOutput5>            output5_;
    ComPtr<IDXGIOutput>             output_;     // base interface for the chosen output
    ComPtr<IDXGIOutputDuplication>  dup_;
    ComPtr<ID3D11Texture2D>         tex_;        // persistent GPU copy
    ComPtr<ID3D11ShaderResourceView> srv_;

    DXGI_OUTPUT_DESC outDesc_ = {};
    RECT  desktopRect_ = {};
    UINT  width_ = 0, height_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    bool  isHDR_ = false;
    bool  hasFrame_ = false;
    bool  frameHeld_ = false;
    int   curOutputIndex_ = 0;
    ULONGLONG lastDupAttemptMs_ = 0;
};
