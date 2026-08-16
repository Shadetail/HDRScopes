#include "scope/WaveformScope.h"
#include "util/ShaderCompiler.h"
#include "util/PQ.h"
#include "util/Format.h"
#include "util/UiReset.h"
#include <algorithm>
#include <cmath>

namespace {
struct WaveCB {
    UINT graphCols, bins, channels, mode;
    UINT extentCols, sampleW, sampleH, useBilinear;
    INT  cropX, cropY, cropW, cropH;
    UINT srcW, srcH, lowPassRadius, blurExtents;
};
struct GraphCB {
    UINT  graphCols, bins, channels, mode;
    float gain; UINT colorize, extentsPoints, chanMask;
    float uvScaleX, uvScaleY, uvOffX, uvOffY;
    float yAxisTop01; UINT extentCols; float extentsOpacity, p1;
};
inline UINT DivUp(UINT a, UINT b) { return (a + b - 1) / b; }
}

bool WaveformScope::Init(ID3D11Device* dev) {
    device_ = dev;
    device_->GetImmediateContext(&context_);
    csClearExt_ = shader::MakeCompute(dev, L"waveform_cs.hlsl", "CSClearExtents");
    csHisto_    = shader::MakeCompute(dev, L"waveform_cs.hlsl", "CSHistogram");
    csExtents_  = shader::MakeCompute(dev, L"waveform_cs.hlsl", "CSExtents");
    vs_ = shader::MakeVertex(dev, L"waveform_ps.hlsl", "VSMain");
    ps_ = shader::MakePixel (dev, L"waveform_ps.hlsl", "PSMain");
    if (!csClearExt_ || !csHisto_ || !csExtents_ || !vs_ || !ps_) return false;

    auto mkCB = [&](UINT sz, ComPtr<ID3D11Buffer>& b) {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sz; bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(device_->CreateBuffer(&bd, nullptr, &b));
    };
    if (!mkCB(sizeof(WaveCB), computeCB_) || !mkCB(sizeof(GraphCB), graphCB_)) return false;

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device_->CreateSamplerState(&sd, &linear_);
    return true;
}

Margins WaveformScope::GetMargins(const Settings&) const {
    // Only a left margin (room for nit labels); graph is flush top/right/bottom.
    return { 30.0f, 0.0f, 0.0f, 0.0f };
}

void WaveformScope::DimsFor(const Settings& s, int cropW, int cropH,
                            UINT& graphCols, UINT& bins, UINT& sampleW, UINT& sampleH, UINT& extCols) {
    cropW = std::max(cropW, 1); cropH = std::max(cropH, 1);
    // Continuous quality: sample the source at 1/f resolution (f = 1 → per pixel).
    float f = std::clamp(s.qualityDownsample, 1.0f, 16.0f);
    sampleW = (UINT)std::max(1l, std::lround(cropW / f));
    sampleH = (UINT)std::max(1l, std::lround(cropH / f));
    bins = 1080;
    graphCols = std::min(sampleW, 3840u);
    extCols = s.waveExtentsSupersample ? (UINT)std::min(cropW, 3840) : graphCols;
}

bool WaveformScope::EnsureBins(UINT graphCols, UINT bins, UINT channels, UINT extCols) {
    if (binsTex_ && graphCols_ == graphCols && bins_ == bins && channels_ == channels && extCols_ == extCols)
        return true;
    binsTex_.Reset(); binsUAV_.Reset(); binsSRV_.Reset();
    extTex_.Reset(); extUAV_.Reset(); extSRV_.Reset(); extStaging_.Reset();
    colorTex_.Reset(); colorUAV_.Reset(); colorSRV_.Reset();

    auto mk = [&](UINT w, UINT h, ComPtr<ID3D11Texture2D>& t, ComPtr<ID3D11UnorderedAccessView>& u,
                  ComPtr<ID3D11ShaderResourceView>& sr) -> bool {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h; td.ArraySize = 1; td.MipLevels = 1;
        td.SampleDesc = { 1, 0 }; td.Format = DXGI_FORMAT_R32_UINT;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &t))) return false;
        if (FAILED(device_->CreateUnorderedAccessView(t.Get(), nullptr, &u))) return false;
        if (FAILED(device_->CreateShaderResourceView(t.Get(), nullptr, &sr))) return false;
        return true;
    };
    if (!mk(graphCols, bins * channels, binsTex_, binsUAV_, binsSRV_)) return false;
    if (!mk(extCols, 2 * channels, extTex_, extUAV_, extSRV_)) return false;
    // Per-cell source colour sums (R,G,B stacked) for luminance colorize.
    if (!mk(graphCols, bins * 3, colorTex_, colorUAV_, colorSRV_)) return false;

    graphCols_ = graphCols; bins_ = bins; channels_ = channels; extCols_ = extCols;
    return true;
}

void WaveformScope::Compute(const ScopeInput& in, const Settings& s) {
    if (!in.srcSRV || in.cropW <= 0 || in.cropH <= 0) return;
    curMode_ = s.waveMode;
    UINT channels = (s.waveMode == 1) ? 3u : 1u;
    UINT graphCols, bins, sampleW, sampleH, extCols;
    DimsFor(s, in.cropW, in.cropH, graphCols, bins, sampleW, sampleH, extCols);
    if (!EnsureBins(graphCols, bins, channels, extCols)) return;
    perColSamples_ = std::max(1.0f, (float)sampleW * (float)sampleH / (float)graphCols);

    // Low-pass radius (in source samples), scaled by the per-column footprint so
    // wider sources get more pre-filtering. Only when the user enables low-pass.
    UINT lowPassRadius = 0;
    if (s.lowPass) {
        float footprint = std::max(1.0f, (float)sampleW / (float)std::max(1u, graphCols));
        lowPassRadius = (UINT)std::lround(std::clamp(s.lowPassAmount, 0.0f, 1.0f) * 6.0f * footprint);
        lowPassRadius = std::min(lowPassRadius, 48u);
    }
    // Whether source blur should also affect the extents trace.
    bool blurActive = (in.rawSRV != in.srcSRV) && in.rawSRV != nullptr;
    UINT blurExtents = (!blurActive || s.blurExtents) ? 1u : 0u;

    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(context_->Map(computeCB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        WaveCB* cb = (WaveCB*)ms.pData;
        cb->graphCols = graphCols; cb->bins = bins; cb->channels = channels; cb->mode = s.waveMode;
        cb->extentCols = extCols; cb->sampleW = sampleW; cb->sampleH = sampleH;
        cb->useBilinear = s.bilinearDownsample ? 1u : 0u;
        cb->cropX = in.cropX; cb->cropY = in.cropY; cb->cropW = in.cropW; cb->cropH = in.cropH;
        cb->srcW = in.srcW; cb->srcH = in.srcH; cb->lowPassRadius = lowPassRadius; cb->blurExtents = blurExtents;
        context_->Unmap(computeCB_.Get(), 0);
    }

    // Clear counts + colour sums; clear extents via shader (min=bins, max=0).
    const UINT zero[4] = { 0, 0, 0, 0 };
    context_->ClearUnorderedAccessViewUint(binsUAV_.Get(), zero);
    context_->ClearUnorderedAccessViewUint(colorUAV_.Get(), zero);

    ID3D11Buffer* cbs[] = { computeCB_.Get() };
    context_->CSSetConstantBuffers(0, 1, cbs);
    ID3D11UnorderedAccessView* uavs[] = { binsUAV_.Get(), extUAV_.Get(), colorUAV_.Get() };
    context_->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);

    context_->CSSetShader(csClearExt_.Get(), nullptr, 0);
    context_->Dispatch(DivUp(extCols, 8), DivUp(channels, 8), 1);

    ID3D11ShaderResourceView* srvs[] = { in.srcSRV, in.rawSRV ? in.rawSRV : in.srcSRV };
    context_->CSSetShaderResources(0, 2, srvs);
    ID3D11SamplerState* samp[] = { linear_.Get() };
    context_->CSSetSamplers(0, 1, samp);
    context_->CSSetShader(csHisto_.Get(), nullptr, 0);
    context_->Dispatch(DivUp(sampleW, 8), DivUp(sampleH, 8), 1);

    // CSHistogram can only populate sampleW distinct extent columns; when the
    // supersampled extents grid is finer than that, resample per extent column.
    if (s.waveExtents && extCols > sampleW) {
        context_->CSSetShader(csExtents_.Get(), nullptr, 0);
        context_->Dispatch(DivUp(extCols, 8), DivUp(sampleH, 8), 1);
    }

    ID3D11UnorderedAccessView* nUAV[] = { nullptr, nullptr, nullptr };
    context_->CSSetUnorderedAccessViews(0, 3, nUAV, nullptr);
    ID3D11ShaderResourceView* nSRV[] = { nullptr, nullptr };
    context_->CSSetShaderResources(0, 2, nSRV);

    extReadbackValid_ = false;
    if (s.waveExtents && s.waveExtentsStyle == 1) ReadbackExtents();
}

void WaveformScope::ReadbackExtents() {
    UINT rows = 2 * channels_;
    if (!extStaging_) {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = extCols_; td.Height = rows; td.ArraySize = 1; td.MipLevels = 1;
        td.SampleDesc = { 1, 0 }; td.Format = DXGI_FORMAT_R32_UINT;
        td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &extStaging_))) return;
    }
    context_->CopyResource(extStaging_.Get(), extTex_.Get());
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(context_->Map(extStaging_.Get(), 0, D3D11_MAP_READ, 0, &ms))) return;
    extData_.assign((size_t)extCols_ * rows, 0);
    const uint8_t* base = (const uint8_t*)ms.pData;
    for (UINT r = 0; r < rows; ++r) {
        const uint32_t* row = (const uint32_t*)(base + (size_t)r * ms.RowPitch);
        for (UINT c = 0; c < extCols_; ++c) extData_[(size_t)r * extCols_ + c] = row[c];
    }
    context_->Unmap(extStaging_.Get(), 0);
    extReadbackValid_ = true;
}

bool WaveformScope::EnsureRT(UINT w, UINT h) {
    if (rt_ && rtW_ == w && rtH_ == h) return true;
    rt_.Reset(); rtv_.Reset(); rtSRV_.Reset();
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.ArraySize = 1; td.MipLevels = 1;
    td.SampleDesc = { 1, 0 }; td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &rt_))) return false;
    if (FAILED(device_->CreateRenderTargetView(rt_.Get(), nullptr, &rtv_))) return false;
    if (FAILED(device_->CreateShaderResourceView(rt_.Get(), nullptr, &rtSRV_))) return false;
    rtW_ = w; rtH_ = h;
    return true;
}

double WaveformScope::YAxisTop01(const ScopeFrame& f, const Settings& s) const {
    return s.sdrWhiteZoom ? std::max(pq::NitsToPos01(f.sdrWhiteNits), 1e-4) : 1.0;
}

void WaveformScope::Render(UINT outW, UINT outH, const ScopeFrame& f, const Settings& s) {
    if (!EnsureRT(outW, outH) || !binsSRV_) return;

    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(context_->Map(graphCB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        GraphCB* cb = (GraphCB*)ms.pData;
        cb->graphCols = graphCols_; cb->bins = bins_; cb->channels = channels_; cb->mode = curMode_;
        // Normalize brightness so a given gain looks the same across quality levels.
        cb->gain = s.gain * (2025.0f / perColSamples_);
        cb->colorize = s.waveColorize ? 1u : 0u;
        cb->extentsPoints = (s.waveExtents && s.waveExtentsStyle == 0) ? 1u : 0u;
        cb->chanMask = (s.channelEnabled[0] ? 1u : 0u) | (s.channelEnabled[1] ? 2u : 0u) | (s.channelEnabled[2] ? 4u : 0u);
        cb->uvScaleX = 1.0f / f.zoom; cb->uvScaleY = 1.0f / f.zoom;
        cb->uvOffX = f.panX; cb->uvOffY = f.panY;
        cb->yAxisTop01 = (float)YAxisTop01(f, s); cb->extentCols = extCols_;
        cb->extentsOpacity = std::clamp(s.waveExtentsOpacity, 0.0f, 1.0f);
        cb->p1 = 0;
        context_->Unmap(graphCB_.Get(), 0);
    }

    const float clear[4] = { 0, 0, 0, 1 };
    context_->ClearRenderTargetView(rtv_.Get(), clear);
    D3D11_VIEWPORT vp = {}; vp.Width = (float)outW; vp.Height = (float)outH; vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);
    ID3D11RenderTargetView* rtvs[] = { rtv_.Get() };
    context_->OMSetRenderTargets(1, rtvs, nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->IASetInputLayout(nullptr);
    context_->VSSetShader(vs_.Get(), nullptr, 0);
    context_->PSSetShader(ps_.Get(), nullptr, 0);
    ID3D11Buffer* cbs[] = { graphCB_.Get() };
    context_->PSSetConstantBuffers(0, 1, cbs);
    ID3D11ShaderResourceView* srvs[] = { binsSRV_.Get(), extSRV_.Get(), colorSRV_.Get() };
    context_->PSSetShaderResources(0, 3, srvs);
    context_->Draw(3, 0);
    ID3D11ShaderResourceView* nSRV[] = { nullptr, nullptr, nullptr };
    context_->PSSetShaderResources(0, 3, nSRV);
    ID3D11RenderTargetView* nRTV[] = { nullptr };
    context_->OMSetRenderTargets(1, nRTV, nullptr);
}

// ---- axis mapping (mirrors the shader) --------------------------------------
float WaveformScope::NitsToScreenY(double nits, const ScopeFrame& f, const Settings& s) const {
    double pos01 = pq::NitsToPos01(nits);
    double yTop = YAxisTop01(f, s);
    double sampleUVy = 1.0 - pos01 / yTop;
    double screenUVy = (sampleUVy - f.panY) * f.zoom;
    return f.graphP0.y + (float)(screenUVy * (f.graphP1.y - f.graphP0.y));
}
double WaveformScope::ScreenYToNits(float y, const ScopeFrame& f, const Settings& s) const {
    double screenUVy = (double)(y - f.graphP0.y) / (double)(f.graphP1.y - f.graphP0.y);
    double sampleUVy = screenUVy / f.zoom + f.panY;
    double yTop = YAxisTop01(f, s);
    double pos01 = (1.0 - sampleUVy) * yTop;
    return pq::Pos01ToNits(std::clamp(pos01, 0.0, 1.0));
}

void WaveformScope::DrawOverlay(ImDrawList* dl, const ScopeFrame& f, Settings& s) {
    const float top = f.graphP0.y, bot = f.graphP1.y;
    const float left = f.graphP0.x, right = f.graphP1.x;
    const float graphW = right - left, graphH = bot - top;
    const float u = UiScale();
    auto inRange = [&](float y) { return y >= top - 0.5f && y <= bot + 0.5f; };

    const ImVec4 gc = ImVec4(s.graticuleColor.x, s.graticuleColor.y, s.graticuleColor.z, s.graticuleOpacity);
    const ImU32 colMajor = ImGui::GetColorU32(gc);
    const ImU32 colMinor = ImGui::GetColorU32(ImVec4(gc.x, gc.y, gc.z, gc.w * 0.6f));
    const ImU32 colText  = ImGui::GetColorU32(ImVec4(0.82f, 0.82f, 0.82f, std::max(0.5f, s.graticuleOpacity)));

    // Major decade lines span the graph (clipped to it).
    const double decades[] = { 1, 10, 100, 1000, 10000 };
    dl->PushClipRect(f.graphP0, f.graphP1, true);
    for (double dec : decades) {
        float y = NitsToScreenY(dec, f, s);
        if (inRange(y)) dl->AddLine(ImVec2(left, y), ImVec2(right, y), colMajor, 1.0f);
    }
    dl->PopClipRect();

    // 10% minor ticks sit OUTSIDE the graph in the left margin (left-20 .. left).
    for (double dec : decades) {
        if (dec >= 10000.0) break;
        for (int m = 2; m <= 9; ++m) {
            double n = dec * m; if (n > 10000.0) break;
            float my = NitsToScreenY(n, f, s);
            if (inRange(my)) dl->AddLine(ImVec2(left - 20.0f * u, my), ImVec2(left, my), colMinor, 1.0f);
        }
    }

    // Decade labels in the LEFT margin (outside the graph). The topmost ("10k")
    // is clamped so it isn't clipped off the top of the window.
    for (double dec : decades) {
        float y = NitsToScreenY(dec, f, s);
        if (!inRange(y)) continue;
        char lab[16];
        if (dec >= 1000.0) snprintf(lab, sizeof(lab), "%gk", dec / 1000.0);
        else               snprintf(lab, sizeof(lab), "%g", dec);
        ImVec2 ts = ImGui::CalcTextSize(lab);
        float ly = std::max(y - ts.y * 0.5f, top);
        dl->AddText(ImVec2(left - 4.0f * u - ts.x, ly), colText, lab);
    }

    // Extents white envelope line (style 1).
    if (s.waveExtents && s.waveExtentsStyle == 1 && extReadbackValid_ && extCols_ > 1) {
        auto binToY = [&](double bin) -> float {
            double fullPos = bin / (double)(bins_ - 1);
            double yTop = YAxisTop01(f, s);
            double sampleUVy = 1.0 - fullPos / yTop;
            double screenUVy = (sampleUVy - f.panY) * f.zoom;
            return top + (float)(screenUVy * graphH);
        };
        std::vector<ImVec2> hi, lo;
        hi.reserve(extCols_); lo.reserve(extCols_);
        for (UINT c = 0; c < extCols_; ++c) {
            uint32_t mn = bins_, mx = 0;
            for (UINT ch = 0; ch < channels_; ++ch) {
                uint32_t cmn = extData_[(size_t)(2 * ch + 0) * extCols_ + c];
                uint32_t cmx = extData_[(size_t)(2 * ch + 1) * extCols_ + c];
                if (cmn <= cmx) { mn = std::min(mn, cmn); mx = std::max(mx, cmx); }
            }
            if (mn > mx) continue; // no samples in this column
            float u = ((float)c + 0.5f) / (float)extCols_;
            float x = left + (u - f.panX) * f.zoom * graphW;
            hi.push_back(ImVec2(x, binToY(mx)));
            lo.push_back(ImVec2(x, binToY(mn)));
        }
        ImU32 envCol = IM_COL32(255, 255, 255, (int)(std::clamp(s.waveExtentsOpacity, 0.0f, 1.0f) * 230.0f));
        dl->PushClipRect(f.graphP0, f.graphP1, true);
        if (hi.size() > 1) dl->AddPolyline(hi.data(), (int)hi.size(), envCol, 0, 1.0f);
        if (lo.size() > 1) dl->AddPolyline(lo.data(), (int)lo.size(), envCol, 0, 1.0f);
        dl->PopClipRect();
    }

    // Reference lines (draggable).
    ImGuiIO& io = ImGui::GetIO();
    bool hovered = ImGui::IsMouseHoveringRect(f.graphP0, f.graphP1);
    const int refAlpha = (int)(std::clamp(s.refLineOpacity, 0.0f, 1.0f) * 255.0f);
    for (size_t i = 0; i < s.refLines.size(); ++i) {
        if (!s.refLines[i].enabled) continue;
        float y = NitsToScreenY(s.refLines[i].nits, f, s);
        if (!inRange(y)) continue;
        ImU32 col = IM_COL32(255, 210, 90, refAlpha);
        dl->PushClipRect(f.graphP0, f.graphP1, true);
        dl->AddLine(ImVec2(left, y), ImVec2(right, y), col, std::max(1.0f, s.refLineThickness));
        dl->PopClipRect();
        // Label sits inside the plot at the left edge, matching the graticule
        // labels' side (those live outside; keeping this inside avoids overlap).
        char lab[32]; snprintf(lab, sizeof(lab), "%.0f", s.refLines[i].nits);
        dl->AddText(ImVec2(left + 6.0f * u, y - ImGui::GetFontSize() - 1.0f * u), col, lab);

        if (hovered && io.MouseClicked[0] && fabsf(io.MousePos.y - y) < 5.0f * u) draggingRef_ = (int)i;
    }
    if (draggingRef_ >= 0) {
        if (io.MouseDown[0] && draggingRef_ < (int)s.refLines.size())
            s.refLines[draggingRef_].nits = std::clamp(ScreenYToNits(io.MousePos.y, f, s), 0.0, 10000.0);
        else draggingRef_ = -1;
    }

    // Nit value of the axis position under the cursor, attached to the cursor.
    if (s.showCursorNits && hovered && ImGui::IsWindowHovered()) {
        char lab[32];
        FormatNits(std::clamp(ScreenYToNits(io.MousePos.y, f, s), 0.0, 10000.0), lab, sizeof(lab));
        ImVec2 ts = ImGui::CalcTextSize(lab);
        ImVec2 tp(io.MousePos.x + 14.0f * u, io.MousePos.y - ts.y * 0.5f);
        if (tp.x + ts.x > right) tp.x = io.MousePos.x - ts.x - 10.0f * u;
        tp.y = std::clamp(tp.y, top, bot - ts.y);
        dl->AddText(ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, 200), lab);
        dl->AddText(tp, IM_COL32(235, 235, 235, 230), lab);
    }

    // Hover probe circles.
    if (s.showHoverProbe && f.probeValid) {
        float px = left + (float)((f.probeU - f.panX) * f.zoom * graphW);
        if (px >= left - 2 && px <= right + 2) {
            dl->PushClipRect(f.graphP0, f.graphP1, true);
            float cr = s.hoverCircleRadius;
            if (curMode_ == 0) {
                double lum = 0.2126390 * f.probeRGB[0] + 0.7151686 * f.probeRGB[1] + 0.0721923 * f.probeRGB[2];
                float y = NitsToScreenY(std::max(0.0, lum) * 80.0, f, s);
                dl->AddCircle(ImVec2(px, y), cr, IM_COL32(255, 255, 255, 255), 0, 1.6f);
            } else {
                const ImU32 cc[3] = { IM_COL32(255, 80, 80, 255), IM_COL32(80, 255, 80, 255), IM_COL32(110, 150, 255, 255) };
                for (int ch = 0; ch < 3; ++ch) {
                    if (!s.channelEnabled[ch]) continue;
                    float y = NitsToScreenY(std::max(0.0f, f.probeRGB[ch]) * 80.0, f, s);
                    dl->AddCircle(ImVec2(px, y), cr, cc[ch], 0, 1.6f);
                }
            }
            dl->PopClipRect();
        }
    }
}

void WaveformScope::DrawControls(Settings& s) {
    const float u = UiScale();
    ImGui::RadioButton("Luminance", &s.waveMode, 0);
    UiReset(s.waveMode, UiDefaults().waveMode);
    UiTip("One trace of overall brightness (luminance) per column."); ImGui::SameLine();
    ImGui::RadioButton("RGB", &s.waveMode, 1);
    UiReset(s.waveMode, UiDefaults().waveMode);
    UiTip("Separate red, green and blue traces, overlaid; toggle channels with the "
          "squares below.");

    if (s.waveMode == 1) {
        // DaVinci-style R G B toggle squares.
        const char* names[3] = { "R", "G", "B" };
        const ImVec4 onCol[3] = { {0.9f,0.2f,0.2f,1}, {0.2f,0.85f,0.2f,1}, {0.3f,0.5f,1.0f,1} };
        for (int i = 0; i < 3; ++i) {
            ImGui::PushID(i);
            ImVec4 c = s.channelEnabled[i] ? onCol[i] : ImVec4(0.2f, 0.2f, 0.2f, 1);
            ImGui::PushStyleColor(ImGuiCol_Button, c);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, c);
            if (ImGui::Button(names[i], ImVec2(26 * u, 26 * u))) s.channelEnabled[i] = !s.channelEnabled[i];
            UiReset(s.channelEnabled[i], true);
            ImGui::PopStyleColor(3); ImGui::PopID();
            if (i < 2) ImGui::SameLine();
        }
    }

    ImGui::Checkbox("Colorize", &s.waveColorize);
    UiReset(s.waveColorize, UiDefaults().waveColorize);
    UiTip("Tint the trace with the source pixels' actual colors instead of drawing "
          "it monochrome.");
    ImGui::Checkbox("Extents", &s.waveExtents);
    UiReset(s.waveExtents, UiDefaults().waveExtents);
    UiTip("Always show each column's true minimum and maximum, even where the main "
          "trace is too faint to see - as colored points or a thin envelope line.");
    if (s.waveExtents) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150 * u);
        const char* styles[] = { "Colored points", "White envelope line" };
        ImGui::Combo("##extstyle", &s.waveExtentsStyle, styles, 2);
        UiReset(s.waveExtentsStyle, UiDefaults().waveExtentsStyle);
        // At per-pixel quality the extents trace already samples every source
        // column, so supersampling would be a no-op — hide the toggle.
        if (!s.perPixelQuality()) {
            ImGui::Checkbox("Supersample extents", &s.waveExtentsSupersample);
            UiReset(s.waveExtentsSupersample, UiDefaults().waveExtentsSupersample);
            UiTip("Read every source pixel for the extents even when Quality samples "
                  "a coarser grid, so single-pixel peaks can't slip through.");
        }
        ImGui::SetNextItemWidth(150 * u);
        ImGui::SliderFloat("Extents opacity", &s.waveExtentsOpacity, 0.0f, 1.0f, "%.2f");
        UiReset(s.waveExtentsOpacity, UiDefaults().waveExtentsOpacity);
    }
    ImGui::SliderFloat("Brightness", &s.gain, 0.001f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic);
    UiReset(s.gain, UiDefaults().gain);
    UiTip("How much each pixel hit brightens the trace (log scale). Raise it for "
          "small or dark sources, lower it for busy full-screen content.");
    ImGui::Checkbox("Zoom Y to SDR white", &s.sdrWhiteZoom);
    UiReset(s.sdrWhiteZoom, UiDefaults().sdrWhiteZoom);
    UiTip("Rescale the vertical axis so its top is the current Windows SDR-white "
          "level instead of 10,000 nits - an SDR-range view of the signal.");
    ImGui::Checkbox("Low pass filter", &s.lowPass);
    UiReset(s.lowPass, UiDefaults().lowPass);
    UiTip("Smooth the trace across neighboring columns (Resolve-style low pass): "
          "exposure bands become easier to read, single-column spikes fade.");
    if (s.lowPass) {
        ImGui::SameLine(); ImGui::SetNextItemWidth(120 * u);
        ImGui::SliderFloat("Strength##lpa", &s.lowPassAmount, 0.0f, 1.0f, "%.2f");
        UiReset(s.lowPassAmount, UiDefaults().lowPassAmount);
    }

    // Reference lines: custom count, log drag (shift = 10x finer), thickness.
    ImGui::SeparatorText("Reference lines");
    ImGui::SetNextItemWidth(120 * u);
    ImGui::SliderFloat("Thickness", &s.refLineThickness, 1.0f, 6.0f, "%.1f px");
    UiReset(s.refLineThickness, UiDefaults().refLineThickness);
    ImGui::SetNextItemWidth(120 * u);
    ImGui::SliderFloat("Opacity##ref", &s.refLineOpacity, 0.0f, 1.0f, "%.2f");
    UiReset(s.refLineOpacity, UiDefaults().refLineOpacity);
    ImGuiIO& io = ImGui::GetIO();
    const Settings& defaults = UiDefaults();
    for (size_t i = 0; i < s.refLines.size(); ++i) {
        ImGui::PushID((int)i + 500);
        const RefLine defaultRef = i < defaults.refLines.size() ? defaults.refLines[i] : RefLine{};
        ImGui::Checkbox("##en", &s.refLines[i].enabled);
        UiReset(s.refLines[i].enabled, defaultRef.enabled);
        UiTip("Show this reference line on the waveform.");
        ImGui::SameLine();
        float v = (float)s.refLines[i].nits;
        float speed = io.KeyShift ? 0.02f : 0.5f; // hold Shift for 10x-finer control
        ImGui::SetNextItemWidth(150 * u);
        if (ImGui::DragFloat("nits", &v, speed, 0.0f, 10000.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
            s.refLines[i].nits = std::clamp((double)v, 0.0, 10000.0);
        UiReset(s.refLines[i].nits, defaultRef.nits);
        UiTip("The line's nit level. Drag here (hold Shift for 10x finer steps) or "
              "drag the line itself on the waveform.");
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) { s.refLines.erase(s.refLines.begin() + i); ImGui::PopID(); break; }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Add reference line")) s.refLines.push_back({ 100.0, true });
}

void WaveformScope::Shutdown() {
    csClearExt_.Reset(); csHisto_.Reset(); csExtents_.Reset(); computeCB_.Reset(); linear_.Reset();
    binsTex_.Reset(); binsUAV_.Reset(); binsSRV_.Reset();
    extTex_.Reset(); extUAV_.Reset(); extSRV_.Reset(); extStaging_.Reset();
    colorTex_.Reset(); colorUAV_.Reset(); colorSRV_.Reset();
    vs_.Reset(); ps_.Reset(); graphCB_.Reset();
    rt_.Reset(); rtv_.Reset(); rtSRV_.Reset();
    context_.Reset(); device_.Reset();
}
