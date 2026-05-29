#include "compute/Waveform.h"
#include "util/ShaderCompiler.h"

namespace {
struct WaveformCB {
    UINT graphCols, bins, channels, mode;
    INT  cropX, cropY, cropW, cropH;
    UINT srcW, srcH, pad0, pad1;
};
constexpr UINT TG = 8; // thread-group size (matches [numthreads(8,8,1)])
inline UINT DivUp(UINT a, UINT b) { return (a + b - 1) / b; }
}

bool Waveform::Init(ID3D11Device* device, UINT graphCols, UINT bins) {
    device_ = device;
    device_->GetImmediateContext(&context_);
    graphCols_ = graphCols;
    bins_      = bins;

    csClear_     = shader::MakeCompute(device_.Get(), L"waveform_cs.hlsl", "CSClear");
    csHistogram_ = shader::MakeCompute(device_.Get(), L"waveform_cs.hlsl", "CSHistogram");
    if (!csClear_ || !csHistogram_) return false;

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(WaveformCB);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HR_RET(device_->CreateBuffer(&bd, nullptr, &cb_), "Waveform CB");

    return EnsureBins();
}

void Waveform::SetMode(Mode m) { mode_ = m; }

bool Waveform::EnsureBins() {
    const UINT channels = Channels();
    if (binsTex_ && builtChannels_ == channels) return true;

    binsTex_.Reset();   binsUAV_.Reset();   binsSRV_.Reset();
    extentsTex_.Reset(); extentsUAV_.Reset(); extentsSRV_.Reset();

    auto makeUintTex = [&](UINT w, UINT h, ComPtr<ID3D11Texture2D>& tex,
                           ComPtr<ID3D11UnorderedAccessView>& uav,
                           ComPtr<ID3D11ShaderResourceView>& srv, const char* tag) -> bool {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h;
        td.ArraySize = 1; td.MipLevels = 1;
        td.SampleDesc = { 1, 0 };
        td.Format = DXGI_FORMAT_R32_UINT;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        HR_RET(device_->CreateTexture2D(&td, nullptr, &tex), tag);
        HR_RET(device_->CreateUnorderedAccessView(tex.Get(), nullptr, &uav), tag);
        HR_RET(device_->CreateShaderResourceView(tex.Get(), nullptr, &srv), tag);
        return true;
    };

    if (!makeUintTex(graphCols_, bins_ * channels, binsTex_, binsUAV_, binsSRV_, "bins tex"))
        return false;
    if (!makeUintTex(graphCols_, 2 * channels, extentsTex_, extentsUAV_, extentsSRV_, "extents tex"))
        return false;

    builtChannels_ = channels;
    return true;
}

void Waveform::Dispatch(ID3D11ShaderResourceView* srcSRV, UINT srcW, UINT srcH,
                        int cropX, int cropY, int cropW, int cropH) {
    if (!srcSRV || cropW <= 0 || cropH <= 0) return;
    if (!EnsureBins()) return;

    const UINT channels = Channels();

    // Update constant buffer.
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(context_->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        WaveformCB* cb = (WaveformCB*)ms.pData;
        cb->graphCols = graphCols_;
        cb->bins = bins_;
        cb->channels = channels;
        cb->mode = (UINT)mode_;
        cb->cropX = cropX; cb->cropY = cropY; cb->cropW = cropW; cb->cropH = cropH;
        cb->srcW = srcW; cb->srcH = srcH; cb->pad0 = 0; cb->pad1 = 0;
        context_->Unmap(cb_.Get(), 0);
    }

    ID3D11Buffer* cbs[] = { cb_.Get() };
    context_->CSSetConstantBuffers(0, 1, cbs);

    ID3D11UnorderedAccessView* uavs[] = { binsUAV_.Get(), extentsUAV_.Get() };
    context_->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

    // Clear pass.
    context_->CSSetShader(csClear_.Get(), nullptr, 0);
    context_->Dispatch(DivUp(graphCols_, TG), DivUp(bins_ * channels, TG), 1);

    // Histogram pass (source bound as SRV t0).
    ID3D11ShaderResourceView* srvs[] = { srcSRV };
    context_->CSSetShaderResources(0, 1, srvs);
    context_->CSSetShader(csHistogram_.Get(), nullptr, 0);
    context_->Dispatch(DivUp((UINT)cropW, TG), DivUp((UINT)cropH, TG), 1);

    // Unbind UAVs/SRVs so the textures can be read by the render pass.
    ID3D11UnorderedAccessView* nullUAV[] = { nullptr, nullptr };
    context_->CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
    context_->CSSetShaderResources(0, 1, nullSRV);
}

void Waveform::Shutdown() {
    csClear_.Reset(); csHistogram_.Reset(); cb_.Reset();
    binsTex_.Reset(); binsUAV_.Reset(); binsSRV_.Reset();
    extentsTex_.Reset(); extentsUAV_.Reset(); extentsSRV_.Reset();
    context_.Reset(); device_.Reset();
}
