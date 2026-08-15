#include "scope/HistogramScope.h"
#include "util/ShaderCompiler.h"
#include "util/PQ.h"
#include "util/Format.h"
#include "util/UiReset.h"
#include <algorithm>
#include <cmath>

namespace {
struct HCB { UINT bins, sampleW, sampleH, useBilinear; INT cropX, cropY, cropW, cropH; UINT srcW, srcH, p0, p1; };
struct GCB { UINT bins, mode, chanMask, colorize; float gain, uvScaleX, uvOffX, xAxisTop01; };
inline UINT DivUp(UINT a, UINT b) { return (a + b - 1) / b; }
void SampleDims(const Settings& s, int cw, int ch, UINT& sw, UINT& sh) {
    float f = std::clamp(s.qualityDownsample, 1.0f, 16.0f);
    auto dim = [f](int v, int m) { return (UINT)std::clamp((int)std::lround(v / f), 1, m); };
    sw = dim(cw, 7680); sh = dim(ch, 4320);
}
}

bool HistogramScope::Init(ID3D11Device* dev) {
    device_ = dev; device_->GetImmediateContext(&context_);
    cs_ = shader::MakeCompute(dev, L"histogram_cs.hlsl", "CSHistogram");
    vs_ = shader::MakeVertex(dev, L"histogram_ps.hlsl", "VSMain");
    ps_ = shader::MakePixel (dev, L"histogram_ps.hlsl", "PSMain");
    if (!cs_ || !vs_ || !ps_) return false;
    auto mkCB = [&](UINT sz, ComPtr<ID3D11Buffer>& b) {
        D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = sz; bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(device_->CreateBuffer(&bd, nullptr, &b));
    };
    if (!mkCB(sizeof(HCB), computeCB_) || !mkCB(sizeof(GCB), graphCB_)) return false;
    D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device_->CreateSamplerState(&sd, &linear_);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = bins_; td.Height = 4; td.ArraySize = 1; td.MipLevels = 1; td.SampleDesc = { 1, 0 };
    td.Format = DXGI_FORMAT_R32_UINT; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &histTex_))) return false;
    if (FAILED(device_->CreateUnorderedAccessView(histTex_.Get(), nullptr, &histUAV_))) return false;
    if (FAILED(device_->CreateShaderResourceView(histTex_.Get(), nullptr, &histSRV_))) return false;
    return true;
}

void HistogramScope::Compute(const ScopeInput& in, const Settings& s) {
    if (!in.srcSRV || in.cropW <= 0 || in.cropH <= 0) return;
    UINT sw, sh; SampleDims(s, in.cropW, in.cropH, sw, sh);
    totalSamples_ = std::max(1.0f, (float)sw * (float)sh);
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(context_->Map(computeCB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        HCB* cb = (HCB*)ms.pData;
        cb->bins = bins_; cb->sampleW = sw; cb->sampleH = sh; cb->useBilinear = s.bilinearDownsample ? 1u : 0u;
        cb->cropX = in.cropX; cb->cropY = in.cropY; cb->cropW = in.cropW; cb->cropH = in.cropH;
        cb->srcW = in.srcW; cb->srcH = in.srcH; cb->p0 = cb->p1 = 0;
        context_->Unmap(computeCB_.Get(), 0);
    }
    const UINT zero[4] = { 0, 0, 0, 0 };
    context_->ClearUnorderedAccessViewUint(histUAV_.Get(), zero);
    ID3D11Buffer* cbs[] = { computeCB_.Get() }; context_->CSSetConstantBuffers(0, 1, cbs);
    ID3D11UnorderedAccessView* uavs[] = { histUAV_.Get() }; context_->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    ID3D11ShaderResourceView* srvs[] = { in.srcSRV }; context_->CSSetShaderResources(0, 1, srvs);
    ID3D11SamplerState* samp[] = { linear_.Get() }; context_->CSSetSamplers(0, 1, samp);
    context_->CSSetShader(cs_.Get(), nullptr, 0);
    context_->Dispatch(DivUp(sw, 8), DivUp(sh, 8), 1);
    ID3D11UnorderedAccessView* n[] = { nullptr }; context_->CSSetUnorderedAccessViews(0, 1, n, nullptr);
    ID3D11ShaderResourceView* ns[] = { nullptr }; context_->CSSetShaderResources(0, 1, ns);
}

bool HistogramScope::EnsureRT(UINT w, UINT h) {
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

void HistogramScope::Render(UINT outW, UINT outH, const ScopeFrame& f, const Settings& s) {
    if (!EnsureRT(outW, outH)) return;
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(context_->Map(graphCB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        GCB* cb = (GCB*)ms.pData;
        cb->bins = bins_; cb->mode = (UINT)s.histoMode;
        cb->chanMask = (s.histoChannelEnabled[0] ? 1u : 0u) | (s.histoChannelEnabled[1] ? 2u : 0u) | (s.histoChannelEnabled[2] ? 4u : 0u);
        cb->colorize = s.histoColorize ? 1u : 0u;
        cb->gain = s.histoGain * (2073600.0f / totalSamples_);
        cb->uvScaleX = 1.0f / f.zoom; cb->uvOffX = f.panX;
        cb->xAxisTop01 = (float)XAxisTop01(f, s);
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
    ID3D11ShaderResourceView* srvs[] = { histSRV_.Get() }; context_->PSSetShaderResources(0, 1, srvs);
    context_->Draw(3, 0);
    ID3D11ShaderResourceView* n[] = { nullptr }; context_->PSSetShaderResources(0, 1, n);
    ID3D11RenderTargetView* nr[] = { nullptr }; context_->OMSetRenderTargets(1, nr, nullptr);
}

double HistogramScope::XAxisTop01(const ScopeFrame& f, const Settings& s) const {
    return s.histoSdrWhiteZoom ? std::max(pq::NitsToPos01(f.sdrWhiteNits), 1e-4) : 1.0;
}
float HistogramScope::NitsToScreenX(double nits, const ScopeFrame& f, const Settings& s) const {
    double pos01 = pq::NitsToPos01(nits) / XAxisTop01(f, s);
    double screenUVx = (pos01 - f.panX) * f.zoom;
    return f.graphP0.x + (float)(screenUVx * (f.graphP1.x - f.graphP0.x));
}
double HistogramScope::ScreenXToNits(float x, const ScopeFrame& f, const Settings& s) const {
    double screenUVx = (double)(x - f.graphP0.x) / (double)(f.graphP1.x - f.graphP0.x);
    double pos01 = (screenUVx / f.zoom + f.panX) * XAxisTop01(f, s);
    return pq::Pos01ToNits(std::clamp(pos01, 0.0, 1.0));
}

void HistogramScope::DrawOverlay(ImDrawList* dl, const ScopeFrame& f, Settings& s) {
    const float top = f.graphP0.y, bot = f.graphP1.y, left = f.graphP0.x, right = f.graphP1.x;
    const ImVec4 gc(s.graticuleColor.x, s.graticuleColor.y, s.graticuleColor.z, s.graticuleOpacity);
    const ImU32 colMajor = ImGui::GetColorU32(gc);
    const ImU32 colText = ImGui::GetColorU32(ImVec4(0.82f, 0.82f, 0.82f, std::max(0.5f, s.graticuleOpacity)));
    auto inX = [&](float x) { return x >= left - 0.5f && x <= right + 0.5f; };

    dl->PushClipRect(f.graphP0, f.graphP1, true);
    const double decades[] = { 1, 10, 100, 1000, 10000 };
    for (double dec : decades) {
        float x = NitsToScreenX(dec, f, s);
        if (inX(x)) dl->AddLine(ImVec2(x, top), ImVec2(x, bot), colMajor, 1.0f);
        if (dec < 10000.0) for (int m = 2; m <= 9; ++m) {
            double n = dec * m; if (n > 10000.0) break;
            float mx = NitsToScreenX(n, f, s);
            if (inX(mx)) dl->AddLine(ImVec2(mx, bot - 12.0f), ImVec2(mx, bot), colMajor, 1.0f);
        }
    }
    dl->PopClipRect();
    for (double dec : decades) {
        float x = NitsToScreenX(dec, f, s); if (!inX(x)) continue;
        char lab[16];
        if (dec >= 1000.0) snprintf(lab, sizeof(lab), "%gk", dec / 1000.0); else snprintf(lab, sizeof(lab), "%g", dec);
        ImVec2 ts = ImGui::CalcTextSize(lab);
        dl->AddText(ImVec2(x - ts.x * 0.5f, bot + 4.0f), colText, lab);
    }

    // Nit value of the axis position under the cursor, attached to the cursor.
    ImGuiIO& io = ImGui::GetIO();
    if (s.showCursorNits && ImGui::IsMouseHoveringRect(f.graphP0, f.graphP1) && ImGui::IsWindowHovered()) {
        char lab[32];
        FormatNits(std::clamp(ScreenXToNits(io.MousePos.x, f, s), 0.0, 10000.0), lab, sizeof(lab));
        ImVec2 ts = ImGui::CalcTextSize(lab);
        ImVec2 tp(io.MousePos.x + 14.0f, io.MousePos.y - ts.y * 0.5f);
        if (tp.x + ts.x > right) tp.x = io.MousePos.x - ts.x - 10.0f;
        tp.y = std::clamp(tp.y, top, bot - ts.y);
        dl->AddText(ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, 200), lab);
        dl->AddText(tp, IM_COL32(235, 235, 235, 230), lab);
    }

    // Hover probe: vertical markers at the hovered pixel's nit value(s).
    if (s.showHoverProbe && f.probeValid) {
        dl->PushClipRect(f.graphP0, f.graphP1, true);
        if (s.histoMode == 2) {
            double lum = 0.2126390 * f.probeRGB[0] + 0.7151686 * f.probeRGB[1] + 0.0721923 * f.probeRGB[2];
            float x = NitsToScreenX(std::max(0.0, lum) * 80.0, f, s);
            if (inX(x)) dl->AddLine(ImVec2(x, top), ImVec2(x, bot), IM_COL32(255, 255, 255, 220), 1.2f);
        } else {
            const ImU32 cc[3] = { IM_COL32(255, 80, 80, 230), IM_COL32(80, 255, 80, 230), IM_COL32(110, 150, 255, 230) };
            for (int ch = 0; ch < 3; ++ch) {
                float x = NitsToScreenX(std::max(0.0f, f.probeRGB[ch]) * 80.0, f, s);
                if (inX(x)) dl->AddLine(ImVec2(x, top), ImVec2(x, bot), cc[ch], 1.2f);
            }
        }
        dl->PopClipRect();
    }
}

void HistogramScope::DrawControls(Settings& s) {
    const char* modes[] = { "LRGB rows", "Overlay RGB", "Luma" };
    ImGui::SetNextItemWidth(160); ImGui::Combo("Mode", &s.histoMode, modes, 3);
    UiReset(s.histoMode, UiDefaults().histoMode);
    if (s.histoMode == 1) {
        const char* names[3] = { "R", "G", "B" };
        const ImVec4 onCol[3] = { {0.9f,0.2f,0.2f,1}, {0.2f,0.85f,0.2f,1}, {0.3f,0.5f,1.0f,1} };
        for (int i = 0; i < 3; ++i) {
            ImGui::PushID(i + 100);
            ImVec4 c = s.histoChannelEnabled[i] ? onCol[i] : ImVec4(0.2f, 0.2f, 0.2f, 1);
            ImGui::PushStyleColor(ImGuiCol_Button, c); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c); ImGui::PushStyleColor(ImGuiCol_ButtonActive, c);
            if (ImGui::Button(names[i], ImVec2(26, 26))) s.histoChannelEnabled[i] = !s.histoChannelEnabled[i];
            UiReset(s.histoChannelEnabled[i], true);
            ImGui::PopStyleColor(3); ImGui::PopID(); if (i < 2) ImGui::SameLine();
        }
    }
    // Colorize only affects the overlay renderer; hide it in the other modes.
    if (s.histoMode == 1) {
        ImGui::Checkbox("Colorize", &s.histoColorize);
        UiReset(s.histoColorize, UiDefaults().histoColorize);
    }
    // Friendly 0..100 brightness, log-mapped to gain (wide range, low default).
    const float gmin = 2e-6f, gmax = 1e-3f;
    float lg = std::log10(gmin), hg = std::log10(gmax);
    float t = (std::log10(std::clamp(s.histoGain, gmin, gmax)) - lg) / (hg - lg);
    int bri = (int)(t * 100.0f + 0.5f);
    ImGui::SetNextItemWidth(180);
    if (ImGui::SliderInt("Brightness", &bri, 0, 100))
        s.histoGain = std::pow(10.0f, lg + (bri / 100.0f) * (hg - lg));
    UiReset(s.histoGain, UiDefaults().histoGain);
    ImGui::Checkbox("Zoom to SDR white", &s.histoSdrWhiteZoom);
    UiReset(s.histoSdrWhiteZoom, UiDefaults().histoSdrWhiteZoom);
}

void HistogramScope::Shutdown() {
    cs_.Reset(); vs_.Reset(); ps_.Reset(); computeCB_.Reset(); graphCB_.Reset(); linear_.Reset();
    histTex_.Reset(); histUAV_.Reset(); histSRV_.Reset();
    rt_.Reset(); rtv_.Reset(); rtSRV_.Reset();
    context_.Reset(); device_.Reset();
}
