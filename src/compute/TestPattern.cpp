#include "compute/TestPattern.h"
#include "util/ShaderCompiler.h"

namespace { struct TestCB { UINT w, h, p0, p1; }; }

bool TestPattern::Init(ID3D11Device* device, UINT w, UINT h) {
    device_ = device;
    device_->GetImmediateContext(&context_);
    w_ = w; h_ = h;

    cs_ = shader::MakeCompute(device_.Get(), L"test_pattern_cs.hlsl", "CSMain");
    if (!cs_) return false;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w_; td.Height = h_;
    td.ArraySize = 1; td.MipLevels = 1;
    td.SampleDesc = { 1, 0 };
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; // scRGB FP16, like live capture
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    HR_RET(device_->CreateTexture2D(&td, nullptr, &tex_), "TestPattern tex");
    HR_RET(device_->CreateUnorderedAccessView(tex_.Get(), nullptr, &uav_), "TestPattern UAV");
    HR_RET(device_->CreateShaderResourceView(tex_.Get(), nullptr, &srv_), "TestPattern SRV");

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(TestCB);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HR_RET(device_->CreateBuffer(&bd, nullptr, &cb_), "TestPattern CB");
    return true;
}

void TestPattern::Generate() {
    if (generated_ || !cs_) return;
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(context_->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        TestCB* cb = (TestCB*)ms.pData;
        cb->w = w_; cb->h = h_; cb->p0 = cb->p1 = 0;
        context_->Unmap(cb_.Get(), 0);
    }
    ID3D11Buffer* cbs[] = { cb_.Get() };
    context_->CSSetConstantBuffers(0, 1, cbs);
    ID3D11UnorderedAccessView* uavs[] = { uav_.Get() };
    context_->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    context_->CSSetShader(cs_.Get(), nullptr, 0);
    context_->Dispatch((w_ + 7) / 8, (h_ + 7) / 8, 1);
    ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
    context_->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    generated_ = true;
}

void TestPattern::Shutdown() {
    cs_.Reset(); cb_.Reset(); tex_.Reset(); uav_.Reset(); srv_.Reset();
    context_.Reset(); device_.Reset();
}
