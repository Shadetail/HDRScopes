// Waveform compute pass: clear -> histogram over the captured scRGB texture,
// producing a uint bins texture (graphCols x bins*channels) plus a per-column
// min/max extents texture. Render-side normalization happens in GraphView.
#pragma once

#include "util/Common.h"
#include "capture/Region.h"
#include <d3d11.h>

class Waveform {
public:
    enum class Mode { Luminance = 0, RGB = 1 };

    bool Init(ID3D11Device* device, UINT graphCols = 1024, UINT bins = 1024);

    void SetMode(Mode m);
    Mode GetMode() const { return mode_; }

    // Run clear + histogram for one frame.
    void Dispatch(ID3D11ShaderResourceView* srcSRV, UINT srcW, UINT srcH,
                  int cropX, int cropY, int cropW, int cropH);

    ID3D11ShaderResourceView* BinsSRV()    const { return binsSRV_.Get(); }
    ID3D11ShaderResourceView* ExtentsSRV() const { return extentsSRV_.Get(); }
    UINT GraphCols() const { return graphCols_; }
    UINT Bins()      const { return bins_; }
    UINT Channels()  const { return mode_ == Mode::RGB ? 3u : 1u; }

    void Shutdown();

private:
    bool EnsureBins();

    ComPtr<ID3D11Device>        device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11ComputeShader> csClear_;
    ComPtr<ID3D11ComputeShader> csHistogram_;
    ComPtr<ID3D11Buffer>        cb_;

    ComPtr<ID3D11Texture2D>          binsTex_;
    ComPtr<ID3D11UnorderedAccessView> binsUAV_;
    ComPtr<ID3D11ShaderResourceView>  binsSRV_;
    ComPtr<ID3D11Texture2D>          extentsTex_;
    ComPtr<ID3D11UnorderedAccessView> extentsUAV_;
    ComPtr<ID3D11ShaderResourceView>  extentsSRV_;

    UINT graphCols_ = 1024;
    UINT bins_      = 1024;
    UINT builtChannels_ = 0;
    Mode mode_ = Mode::Luminance;
};
