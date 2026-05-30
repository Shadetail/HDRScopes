#include "scope/ChromaScope.h"
#include "util/ShaderCompiler.h"
#include <algorithm>

namespace {
struct CCB { UINT size, mode, sampleW, sampleH; UINT useBilinear, pa, pb, pc;
             INT cropX, cropY, cropW, cropH; UINT srcW, srcH, pd, pe;
             float minX, maxX, minY, maxY; float scale, sdrNorm, pg, ph; };
struct GCB { UINT size, mode, colorize, extents; float gain, minX, maxX, minY;
             float maxY, scale, uvScaleX, uvScaleY; float uvOffX, uvOffY, dotRadius, extentsOpacity; };
inline UINT DivUp(UINT a, UINT b) { return (a + b - 1) / b; }
void SampleDims(Quality q, int cw, int ch, UINT& sw, UINT& sh) {
    auto cap = [](int v, int m) { return (UINT)std::min(std::max(v, 1), m); };
    switch (q) {
    case Quality::Low:    sw = cap(cw, 960);  sh = cap(ch, 540);  break;
    case Quality::Medium: sw = cap(cw, 1440); sh = cap(ch, 810);  break;
    case Quality::High:   sw = cap(cw, 1920); sh = cap(ch, 1080); break;
    default:              sw = cap(cw, 7680); sh = cap(ch, 4320); break;
    }
}
}

bool ChromaScope::Init(ID3D11Device* dev) {
    device_ = dev; device_->GetImmediateContext(&context_);
    cs_ = shader::MakeCompute(dev, L"chroma_cs.hlsl", "CSMain");
    vs_ = shader::MakeVertex(dev, L"chroma_ps.hlsl", "VSMain");
    ps_ = shader::MakePixel (dev, L"chroma_ps.hlsl", "PSMain");
    if (!cs_ || !vs_ || !ps_) return false;
    auto mkCB = [&](UINT sz, ComPtr<ID3D11Buffer>& b) {
        D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = sz; bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(device_->CreateBuffer(&bd, nullptr, &b));
    };
    if (!mkCB(sizeof(CCB), computeCB_) || !mkCB(sizeof(GCB), graphCB_)) return false;
    D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device_->CreateSamplerState(&sd, &linear_);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = size_; td.Height = size_; td.ArraySize = 1; td.MipLevels = 1; td.SampleDesc = { 1, 0 };
    td.Format = DXGI_FORMAT_R32_UINT; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &accumTex_))) return false;
    if (FAILED(device_->CreateUnorderedAccessView(accumTex_.Get(), nullptr, &accumUAV_))) return false;
    if (FAILED(device_->CreateShaderResourceView(accumTex_.Get(), nullptr, &accumSRV_))) return false;
    return true;
}

void ChromaScope::Compute(const ScopeInput& in, const Settings& s) {
    if (!in.srcSRV || in.cropW <= 0 || in.cropH <= 0) return;
    PlotRange r = Range(s);
    UINT sw, sh; SampleDims(s.quality, in.cropW, in.cropH, sw, sh);
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(context_->Map(computeCB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        CCB* cb = (CCB*)ms.pData;
        *cb = {};
        cb->size = size_; cb->mode = (UINT)r.mode; cb->sampleW = sw; cb->sampleH = sh;
        cb->useBilinear = s.bilinearDownsample ? 1u : 0u;
        cb->cropX = in.cropX; cb->cropY = in.cropY; cb->cropW = in.cropW; cb->cropH = in.cropH;
        cb->srcW = in.srcW; cb->srcH = in.srcH;
        cb->minX = r.minX; cb->maxX = r.maxX; cb->minY = r.minY; cb->maxY = r.maxY;
        cb->scale = (r.mode == 0) ? s.vectorScale : r.scale;
        cb->sdrNorm = in.sdrWhiteNits / 80.0f;
        context_->Unmap(computeCB_.Get(), 0);
    }
    const UINT zero[4] = { 0, 0, 0, 0 };
    context_->ClearUnorderedAccessViewUint(accumUAV_.Get(), zero);
    ID3D11Buffer* cbs[] = { computeCB_.Get() }; context_->CSSetConstantBuffers(0, 1, cbs);
    ID3D11UnorderedAccessView* uavs[] = { accumUAV_.Get() }; context_->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    ID3D11ShaderResourceView* srvs[] = { in.srcSRV }; context_->CSSetShaderResources(0, 1, srvs);
    ID3D11SamplerState* samp[] = { linear_.Get() }; context_->CSSetSamplers(0, 1, samp);
    context_->CSSetShader(cs_.Get(), nullptr, 0);
    context_->Dispatch(DivUp(sw, 8), DivUp(sh, 8), 1);
    ID3D11UnorderedAccessView* n[] = { nullptr }; context_->CSSetUnorderedAccessViews(0, 1, n, nullptr);
    ID3D11ShaderResourceView* ns[] = { nullptr }; context_->CSSetShaderResources(0, 1, ns);
}

bool ChromaScope::EnsureRT(UINT w, UINT h) {
    if (rt_ && rtW_ == w && rtH_ == h) return true;
    rt_.Reset(); rtv_.Reset(); rtSRV_.Reset();
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.ArraySize = 1; td.MipLevels = 1; td.SampleDesc = { 1, 0 };
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &rt_))) return false;
    if (FAILED(device_->CreateRenderTargetView(rt_.Get(), nullptr, &rtv_))) return false;
    if (FAILED(device_->CreateShaderResourceView(rt_.Get(), nullptr, &rtSRV_))) return false;
    rtW_ = w; rtH_ = h; return true;
}

void ChromaScope::Render(UINT outW, UINT outH, const ScopeFrame& f, const Settings& s) {
    if (!EnsureRT(outW, outH)) return;
    PlotRange r = Range(s);
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(context_->Map(graphCB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        GCB* cb = (GCB*)ms.pData;
        *cb = {};
        cb->size = size_; cb->mode = (UINT)r.mode; cb->colorize = s.colorize ? 1u : 0u;
        cb->gain = Gain(s); cb->minX = r.minX; cb->maxX = r.maxX; cb->minY = r.minY; cb->maxY = r.maxY;
        cb->scale = (r.mode == 0) ? s.vectorScale : r.scale;
        cb->uvScaleX = 1.0f / f.zoom; cb->uvScaleY = 1.0f / f.zoom;
        cb->uvOffX = f.panX; cb->uvOffY = f.panY;
        cb->dotRadius = (float)s.chromaDotRadius;
        cb->extents = s.extents ? 1u : 0u;
        cb->extentsOpacity = std::clamp(s.extentsOpacity, 0.0f, 1.0f);
        context_->Unmap(graphCB_.Get(), 0);
    }
    const float clear[4] = { 0, 0, 0, 1 };
    context_->ClearRenderTargetView(rtv_.Get(), clear);
    D3D11_VIEWPORT vp = {}; vp.Width = (float)outW; vp.Height = (float)outH; vp.MaxDepth = 1;
    context_->RSSetViewports(1, &vp);
    ID3D11RenderTargetView* rtvs[] = { rtv_.Get() }; context_->OMSetRenderTargets(1, rtvs, nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->IASetInputLayout(nullptr);
    context_->VSSetShader(vs_.Get(), nullptr, 0); context_->PSSetShader(ps_.Get(), nullptr, 0);
    ID3D11Buffer* cbs[] = { graphCB_.Get() }; context_->PSSetConstantBuffers(0, 1, cbs);
    ID3D11ShaderResourceView* srvs[] = { accumSRV_.Get() }; context_->PSSetShaderResources(0, 1, srvs);
    context_->Draw(3, 0);
    ID3D11ShaderResourceView* n[] = { nullptr }; context_->PSSetShaderResources(0, 1, n);
    ID3D11RenderTargetView* nr[] = { nullptr }; context_->OMSetRenderTargets(1, nr, nullptr);
}

void ChromaScope::Shutdown() {
    cs_.Reset(); vs_.Reset(); ps_.Reset(); computeCB_.Reset(); graphCB_.Reset(); linear_.Reset();
    accumTex_.Reset(); accumUAV_.Reset(); accumSRV_.Reset();
    rt_.Reset(); rtv_.Reset(); rtSRV_.Reset();
    context_.Reset(); device_.Reset();
}
