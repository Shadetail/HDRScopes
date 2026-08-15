#include "compute/PeakMeter.h"
#include "util/ShaderCompiler.h"
#include <cstring>

namespace {
struct PCB { INT cropX, cropY, cropW, cropH; UINT srcW, srcH, p0, p1; };
inline UINT DivUp(UINT a, UINT b) { return (a + b - 1) / b; }
}

bool PeakMeter::Init(ID3D11Device* device) {
    device_ = device;
    device_->GetImmediateContext(&context_);
    cs_ = shader::MakeCompute(device, L"peak_cs.hlsl", "CSPeak");
    if (!cs_) return false;

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(PCB); bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&bd, nullptr, &cb_))) return false;

    // 4 uints, raw UAV so the shader can InterlockedMax into it.
    bd = {};
    bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    if (FAILED(device_->CreateBuffer(&bd, nullptr, &buf_))) return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
    ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = 4;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    if (FAILED(device_->CreateUnorderedAccessView(buf_.Get(), &ud, &uav_))) return false;

    bd = {};
    bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_STAGING;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (auto& s : staging_)
        if (FAILED(device_->CreateBuffer(&bd, nullptr, &s))) return false;
    return true;
}

bool PeakMeter::Measure(ID3D11ShaderResourceView* srv, UINT srcW, UINT srcH,
                        int cropX, int cropY, int cropW, int cropH, float outLRGB[4]) {
    if (!srv || !cs_ || cropW <= 0 || cropH <= 0) {
        if (haveLast_) { memcpy(outLRGB, last_, sizeof(last_)); return true; }
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(context_->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        PCB* cb = (PCB*)ms.pData;
        cb->cropX = cropX; cb->cropY = cropY; cb->cropW = cropW; cb->cropH = cropH;
        cb->srcW = srcW; cb->srcH = srcH; cb->p0 = cb->p1 = 0;
        context_->Unmap(cb_.Get(), 0);
    }

    const UINT zero[4] = { 0, 0, 0, 0 };
    context_->ClearUnorderedAccessViewUint(uav_.Get(), zero);
    ID3D11Buffer* cbs[] = { cb_.Get() };
    context_->CSSetConstantBuffers(0, 1, cbs);
    ID3D11UnorderedAccessView* uavs[] = { uav_.Get() };
    context_->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    ID3D11ShaderResourceView* srvs[] = { srv };
    context_->CSSetShaderResources(0, 1, srvs);
    context_->CSSetShader(cs_.Get(), nullptr, 0);
    context_->Dispatch(DivUp((UINT)cropW, 8), DivUp((UINT)cropH, 8), 1);
    ID3D11UnorderedAccessView* nUAV[] = { nullptr };
    context_->CSSetUnorderedAccessViews(0, 1, nUAV, nullptr);
    ID3D11ShaderResourceView* nSRV[] = { nullptr };
    context_->CSSetShaderResources(0, 1, nSRV);

    // Copy this frame's result; read the one copied a frame ago (GPU is done
    // with it, so this Map doesn't stall).
    int writeIdx = writeIdx_;
    int readIdx = 1 - writeIdx_;
    context_->CopyResource(staging_[writeIdx].Get(), buf_.Get());
    pending_[writeIdx] = true;
    writeIdx_ = readIdx;

    // DO_NOT_WAIT: in the rare case the GPU hasn't finished that copy yet, keep
    // showing the previous value instead of stalling the frame.
    if (pending_[readIdx] &&
        SUCCEEDED(context_->Map(staging_[readIdx].Get(), 0, D3D11_MAP_READ,
                                D3D11_MAP_FLAG_DO_NOT_WAIT, &ms))) {
        const uint32_t* bits = (const uint32_t*)ms.pData;
        for (int i = 0; i < 4; ++i) memcpy(&last_[i], &bits[i], sizeof(float));
        context_->Unmap(staging_[readIdx].Get(), 0);
        pending_[readIdx] = false;
        haveLast_ = true;
    }
    if (haveLast_) { memcpy(outLRGB, last_, sizeof(last_)); return true; }
    return false;
}

void PeakMeter::Shutdown() {
    cs_.Reset(); cb_.Reset(); buf_.Reset(); uav_.Reset();
    for (auto& s : staging_) s.Reset();
    context_.Reset(); device_.Reset();
}
