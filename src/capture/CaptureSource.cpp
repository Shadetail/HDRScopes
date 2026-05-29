#include "capture/CaptureSource.h"

// HDR outputs report this color space via IDXGIOutput6::GetDesc1.
static bool OutputIsHDR(IDXGIOutput* output) {
    ComPtr<IDXGIOutput6> o6;
    if (FAILED(output->QueryInterface(IID_PPV_ARGS(&o6)))) return false;
    DXGI_OUTPUT_DESC1 d1 = {};
    if (FAILED(o6->GetDesc1(&d1))) return false;
    return d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
}

std::vector<CaptureSource::OutputInfo> CaptureSource::EnumerateOutputs() {
    std::vector<OutputInfo> result;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return result;

    int globalIndex = 0;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC desc = {};
            output->GetDesc(&desc);
            if (desc.AttachedToDesktop) {
                OutputInfo info;
                info.name  = desc.DeviceName;
                info.rect  = desc.DesktopCoordinates;
                info.hdr   = OutputIsHDR(output.Get());
                info.index = globalIndex;
                result.push_back(info);
            }
            ++globalIndex;
            output.Reset();
        }
        adapter.Reset();
    }
    return result;
}

bool CaptureSource::Init(ID3D11Device* device, POINT pickPoint) {
    device_ = device;
    device_->GetImmediateContext(&context_);
    if (!SelectOutput(pickPoint, -1)) return false;
    return CreateDuplicator();
}

bool CaptureSource::SelectOutput(POINT pt, int explicitIndex) {
    ComPtr<IDXGIFactory1> factory;
    HR_RET(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");

    ComPtr<IDXGIOutput> chosen;
    DXGI_OUTPUT_DESC chosenDesc = {};
    int chosenIndex = -1;
    int globalIndex = 0;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC desc = {};
            output->GetDesc(&desc);
            bool match = false;
            if (explicitIndex >= 0) {
                match = (globalIndex == explicitIndex);
            } else {
                match = desc.AttachedToDesktop && PtInRect(&desc.DesktopCoordinates, pt);
            }
            if (match && desc.AttachedToDesktop) {
                chosen = output;
                chosenDesc = desc;
                chosenIndex = globalIndex;
            }
            ++globalIndex;
            output.Reset();
            if (chosen) break;
        }
        adapter.Reset();
        if (chosen) break;
    }

    // Fallback: first attached output.
    if (!chosen) {
        HDRLog("[CaptureSource] no output matched; falling back to primary.");
        ComPtr<IDXGIAdapter1> ad0;
        if (factory->EnumAdapters1(0, &ad0) != DXGI_ERROR_NOT_FOUND) {
            ComPtr<IDXGIOutput> o0;
            if (ad0->EnumOutputs(0, &o0) != DXGI_ERROR_NOT_FOUND) {
                o0->GetDesc(&chosenDesc);
                chosen = o0;
                chosenIndex = 0;
            }
        }
    }
    if (!chosen) {
        HDRLog("[CaptureSource] no attached desktop output found.");
        return false;
    }

    output_ = chosen;
    outDesc_ = chosenDesc;
    desktopRect_ = chosenDesc.DesktopCoordinates;
    curOutputIndex_ = chosenIndex;
    isHDR_ = OutputIsHDR(chosen.Get());

    if (FAILED(chosen.As(&output5_))) {
        HDRLog("[CaptureSource] IDXGIOutput5 unavailable (need Windows 10 1803+).");
        output5_.Reset();
        return false;
    }
    HDRLog("[CaptureSource] selected output %d  %dx%d  HDR=%d",
           chosenIndex,
           desktopRect_.right - desktopRect_.left,
           desktopRect_.bottom - desktopRect_.top,
           (int)isHDR_);
    return true;
}

bool CaptureSource::CreateDuplicator() {
    if (!output5_) return false;

    // Priority-ordered format list. For HDR we want scRGB FP16 first so the
    // downstream color math (1.0 = 80 nits, linear Rec.709) holds; R10G10B10A2
    // would be HDR10 PQ/Rec2020 and require entirely different handling.
    DXGI_FORMAT hdrFormats[] = {
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R10G10B10A2_UNORM,
        DXGI_FORMAT_R32G32B32A32_FLOAT,
    };
    DXGI_FORMAT sdrFormats[] = {
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM,
    };

    HRESULT hr;
    if (isHDR_)
        hr = output5_->DuplicateOutput1(device_.Get(), 0, _countof(hdrFormats), hdrFormats, &dup_);
    else
        hr = output5_->DuplicateOutput1(device_.Get(), 0, _countof(sdrFormats), sdrFormats, &dup_);

    if (FAILED(hr)) {
        HDRLog("[CaptureSource] DuplicateOutput1 failed hr=0x%08lx", (unsigned long)hr);
        return false;
    }

    DXGI_OUTDUPL_DESC dd = {};
    dup_->GetDesc(&dd);
    format_ = dd.ModeDesc.Format;
    HDRLog("[CaptureSource] duplicator created  fmt=%d  %ux%u",
           (int)format_, dd.ModeDesc.Width, dd.ModeDesc.Height);
    hasFrame_ = false;
    return true;
}

void CaptureSource::ReleaseDuplicator() {
    if (dup_ && frameHeld_) { dup_->ReleaseFrame(); frameHeld_ = false; }
    dup_.Reset();
}

bool CaptureSource::EnsureTexture(UINT w, UINT h, DXGI_FORMAT fmt) {
    if (tex_ && width_ == w && height_ == h && format_ == fmt) return true;
    tex_.Reset();
    srv_.Reset();

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h;
    td.ArraySize = 1; td.MipLevels = 1;
    td.SampleDesc = { 1, 0 };
    td.Format = fmt;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    HR_RET(device_->CreateTexture2D(&td, nullptr, &tex_), "Create persistent capture texture");

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = fmt;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    HR_RET(device_->CreateShaderResourceView(tex_.Get(), &sd, &srv_), "Create capture SRV");

    width_ = w; height_ = h; format_ = fmt;
    return true;
}

bool CaptureSource::AcquireFrame(UINT timeoutMs) {
    if (!dup_) {
        // Throttle re-creation attempts so a persistent failure (e.g. a
        // protected fullscreen app holding the duplicator) doesn't spin.
        ULONGLONG now = GetTickCount64();
        if (now - lastDupAttemptMs_ < 500) return hasFrame_;
        lastDupAttemptMs_ = now;
        if (!CreateDuplicator()) return hasFrame_; // keep last frame if any
    }

    DXGI_OUTDUPL_FRAME_INFO info = {};
    ComPtr<IDXGIResource> res;
    HRESULT hr = dup_->AcquireNextFrame(timeoutMs, &info, &res);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        // No new desktop update within the timeout: reuse the last texture.
        return hasFrame_;
    }
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        HDRLog("[CaptureSource] ACCESS_LOST; rebuilding duplicator.");
        ReleaseDuplicator();
        // Re-evaluate HDR state (a mode change may have toggled it).
        isHDR_ = OutputIsHDR(output_.Get());
        CreateDuplicator();
        return hasFrame_;
    }
    if (FAILED(hr)) {
        HDRLog("[CaptureSource] AcquireNextFrame hr=0x%08lx", (unsigned long)hr);
        return hasFrame_;
    }

    frameHeld_ = true;

    // LastPresentTime == 0 means only the mouse moved (no pixel update): reuse.
    if (info.LastPresentTime.QuadPart != 0) {
        ComPtr<ID3D11Texture2D> srcTex;
        if (SUCCEEDED(res.As(&srcTex))) {
            D3D11_TEXTURE2D_DESC sdesc = {};
            srcTex->GetDesc(&sdesc);
            if (EnsureTexture(sdesc.Width, sdesc.Height, sdesc.Format)) {
                context_->CopyResource(tex_.Get(), srcTex.Get());
                hasFrame_ = true;
            }
        }
    }

    dup_->ReleaseFrame();
    frameHeld_ = false;
    return hasFrame_;
}

bool CaptureSource::RetargetToPoint(POINT pickPoint) {
    ReleaseDuplicator();
    if (!SelectOutput(pickPoint, -1)) return false;
    return CreateDuplicator();
}

bool CaptureSource::RetargetToIndex(int outputIndex) {
    ReleaseDuplicator();
    if (!SelectOutput(POINT{0, 0}, outputIndex)) return false;
    return CreateDuplicator();
}

void CaptureSource::Shutdown() {
    ReleaseDuplicator();
    srv_.Reset();
    tex_.Reset();
    output5_.Reset();
    output_.Reset();
    context_.Reset();
    device_.Reset();
}
