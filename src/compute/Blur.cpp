#include "compute/Blur.h"
#include "util/ShaderCompiler.h"

namespace { struct BCB { UINT w, h; float sigma, dirX; float dirY, p0, p1, p2; }; }

bool Blur::Init(ID3D11Device* dev) {
    device_ = dev; device_->GetImmediateContext(&context_);
    cs_ = shader::MakeCompute(dev, L"blur_cs.hlsl", "CSMain");
    if (!cs_) return false;
    D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = sizeof(BCB); bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&bd, nullptr, &cb_))) return false;
    D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device_->CreateSamplerState(&sd, &linear_);
    return true;
}

bool Blur::EnsureTargets(UINT w, UINT h) {
    if (tex_[0] && w_ == w && h_ == h) return true;
    for (int i = 0; i < 2; ++i) { tex_[i].Reset(); uav_[i].Reset(); srv_[i].Reset(); }
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.ArraySize = 1; td.MipLevels = 1; td.SampleDesc = { 1, 0 };
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    for (int i = 0; i < 2; ++i) {
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &tex_[i]))) return false;
        if (FAILED(device_->CreateUnorderedAccessView(tex_[i].Get(), nullptr, &uav_[i]))) return false;
        if (FAILED(device_->CreateShaderResourceView(tex_[i].Get(), nullptr, &srv_[i]))) return false;
    }
    w_ = w; h_ = h; return true;
}

ID3D11ShaderResourceView* Blur::Apply(ID3D11ShaderResourceView* srcSRV, UINT w, UINT h, float radiusPx) {
    if (!srcSRV || radiusPx <= 0.05f || !cs_ || w == 0 || h == 0) return srcSRV;
    if (!EnsureTargets(w, h)) return srcSRV;

    ID3D11SamplerState* samp[] = { linear_.Get() };
    context_->CSSetSamplers(0, 1, samp);
    context_->CSSetShader(cs_.Get(), nullptr, 0);

    auto pass = [&](ID3D11ShaderResourceView* in, ID3D11UnorderedAccessView* out, float dx, float dy) {
        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(context_->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            BCB* cb = (BCB*)ms.pData; *cb = {};
            cb->w = w; cb->h = h; cb->sigma = radiusPx; cb->dirX = dx; cb->dirY = dy;
            context_->Unmap(cb_.Get(), 0);
        }
        ID3D11Buffer* cbs[] = { cb_.Get() }; context_->CSSetConstantBuffers(0, 1, cbs);
        ID3D11UnorderedAccessView* uavs[] = { out }; context_->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        ID3D11ShaderResourceView* srvs[] = { in }; context_->CSSetShaderResources(0, 1, srvs);
        context_->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
        ID3D11UnorderedAccessView* n[] = { nullptr }; context_->CSSetUnorderedAccessViews(0, 1, n, nullptr);
        ID3D11ShaderResourceView* ns[] = { nullptr }; context_->CSSetShaderResources(0, 1, ns);
    };

    pass(srcSRV, uav_[0].Get(), 1.0f, 0.0f);   // horizontal -> tex0
    pass(srv_[0].Get(), uav_[1].Get(), 0.0f, 1.0f); // vertical -> tex1
    return srv_[1].Get();
}

void Blur::Shutdown() {
    cs_.Reset(); cb_.Reset(); linear_.Reset();
    for (int i = 0; i < 2; ++i) { tex_[i].Reset(); uav_[i].Reset(); srv_[i].Reset(); }
    context_.Reset(); device_.Reset();
}
