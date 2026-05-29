#include "render/GraphView.h"
#include "compute/Waveform.h"
#include "util/ShaderCompiler.h"

namespace {
struct GraphCB {
    UINT  graphCols, bins, channels, mode;
    float gain; UINT extents; float uvScaleX, uvScaleY;
    float uvOffsetX, uvOffsetY, pad0, pad1;
};
}

bool GraphView::Init(ID3D11Device* device) {
    device_ = device;
    device_->GetImmediateContext(&context_);

    vs_ = shader::MakeVertex(device_.Get(), L"graph.hlsl", "VSMain");
    ps_ = shader::MakePixel (device_.Get(), L"graph.hlsl", "PSMain");
    if (!vs_ || !ps_) return false;

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(GraphCB);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HR_RET(device_->CreateBuffer(&bd, nullptr, &cb_), "Graph CB");
    return true;
}

bool GraphView::SetTargetSize(UINT w, UINT h) {
    if (w == 0 || h == 0) return false;
    if (rt_ && w == width_ && h == height_) return true;
    rt_.Reset(); rtv_.Reset(); rtSRV_.Reset();

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h;
    td.ArraySize = 1; td.MipLevels = 1;
    td.SampleDesc = { 1, 0 };
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    HR_RET(device_->CreateTexture2D(&td, nullptr, &rt_), "Graph RT");
    HR_RET(device_->CreateRenderTargetView(rt_.Get(), nullptr, &rtv_), "Graph RTV");
    HR_RET(device_->CreateShaderResourceView(rt_.Get(), nullptr, &rtSRV_), "Graph RT SRV");
    width_ = w; height_ = h;
    return true;
}

void GraphView::Render(const Waveform& wf, const Params& p) {
    if (!rtv_) return;

    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(context_->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        GraphCB* cb = (GraphCB*)ms.pData;
        cb->graphCols = wf.GraphCols();
        cb->bins      = wf.Bins();
        cb->channels  = wf.Channels();
        cb->mode      = (UINT)wf.GetMode();
        cb->gain      = p.gain;
        cb->extents   = p.extents ? 1u : 0u;
        cb->uvScaleX  = p.uvScaleX; cb->uvScaleY = p.uvScaleY;
        cb->uvOffsetX = p.uvOffsetX; cb->uvOffsetY = p.uvOffsetY;
        cb->pad0 = cb->pad1 = 0;
        context_->Unmap(cb_.Get(), 0);
    }

    const float clear[4] = { 0, 0, 0, 1 };
    context_->ClearRenderTargetView(rtv_.Get(), clear);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)width_; vp.Height = (float)height_; vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);

    ID3D11RenderTargetView* rtvs[] = { rtv_.Get() };
    context_->OMSetRenderTargets(1, rtvs, nullptr);

    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->IASetInputLayout(nullptr);
    context_->VSSetShader(vs_.Get(), nullptr, 0);
    context_->PSSetShader(ps_.Get(), nullptr, 0);

    ID3D11Buffer* cbs[] = { cb_.Get() };
    context_->PSSetConstantBuffers(0, 1, cbs);
    ID3D11ShaderResourceView* srvs[] = { wf.BinsSRV(), wf.ExtentsSRV() };
    context_->PSSetShaderResources(0, 2, srvs);

    context_->Draw(3, 0);

    // Unbind so the SRVs are free next frame.
    ID3D11ShaderResourceView* nullSRV[] = { nullptr, nullptr };
    context_->PSSetShaderResources(0, 2, nullSRV);
    ID3D11RenderTargetView* nullRTV[] = { nullptr };
    context_->OMSetRenderTargets(1, nullRTV, nullptr);
}

void GraphView::Shutdown() {
    vs_.Reset(); ps_.Reset(); cb_.Reset();
    rt_.Reset(); rtv_.Reset(); rtSRV_.Reset();
    context_.Reset(); device_.Reset();
}
