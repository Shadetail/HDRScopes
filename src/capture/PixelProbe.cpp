#include "capture/PixelProbe.h"

// IEEE half -> float.
static float HalfToFloat(unsigned short h) {
    unsigned int sign = (h >> 15) & 1;
    unsigned int exp  = (h >> 10) & 0x1F;
    unsigned int mant = h & 0x3FF;
    unsigned int f;
    if (exp == 0) {
        if (mant == 0) { f = sign << 31; }
        else {
            exp = 127 - 15 + 1;
            while ((mant & 0x400) == 0) { mant <<= 1; exp--; }
            mant &= 0x3FF;
            f = (sign << 31) | (exp << 23) | (mant << 13);
        }
    } else if (exp == 0x1F) {
        f = (sign << 31) | (0xFF << 23) | (mant << 13);
    } else {
        f = (sign << 31) | ((exp - 15 + 127) << 23) | (mant << 13);
    }
    float out;
    memcpy(&out, &f, sizeof(out));
    return out;
}

bool PixelProbe::Init(ID3D11Device* device) {
    device_ = device;
    device_->GetImmediateContext(&context_);
    return true;
}

bool PixelProbe::EnsureStaging(DXGI_FORMAT fmt) {
    if (staging_[0] && stagingFmt_ == fmt) return true;
    for (auto& s : staging_) s.Reset();
    pending_[0] = pending_[1] = false;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = 1; td.Height = 1; td.ArraySize = 1; td.MipLevels = 1;
    td.SampleDesc = { 1, 0 };
    td.Format = fmt;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (auto& s : staging_)
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &s))) return false;
    stagingFmt_ = fmt;
    return true;
}

bool PixelProbe::Read(ID3D11ShaderResourceView* srv, UINT srcW, UINT srcH,
                      int x, int y, float outRGB[3]) {
    if (!srv || x < 0 || y < 0 || x >= (int)srcW || y >= (int)srcH) {
        if (haveLast_) { outRGB[0] = lastRGB_[0]; outRGB[1] = lastRGB_[1]; outRGB[2] = lastRGB_[2]; }
        return false;
    }

    ComPtr<ID3D11Resource> res;
    srv->GetResource(&res);
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(res.As(&tex))) return false;
    D3D11_TEXTURE2D_DESC td = {};
    tex->GetDesc(&td);
    if (!EnsureStaging(td.Format)) return false;

    // Copy the current texel into this frame's buffer.
    int writeIdx = writeIdx_;
    int readIdx = 1 - writeIdx_;
    D3D11_BOX box = {};
    box.left = x; box.right = x + 1;
    box.top = y; box.bottom = y + 1;
    box.front = 0; box.back = 1;
    context_->CopySubresourceRegion(staging_[writeIdx].Get(), 0, 0, 0, 0, tex.Get(), 0, &box);
    pending_[writeIdx] = true;
    writeIdx_ = readIdx;

    // Map the OTHER buffer (a frame older) without waiting.
    if (!pending_[readIdx]) {
        if (haveLast_) { outRGB[0] = lastRGB_[0]; outRGB[1] = lastRGB_[1]; outRGB[2] = lastRGB_[2]; return true; }
        return false;
    }
    D3D11_MAPPED_SUBRESOURCE ms;
    HRESULT hr = context_->Map(staging_[readIdx].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &ms);
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING || FAILED(hr)) {
        if (haveLast_) { outRGB[0] = lastRGB_[0]; outRGB[1] = lastRGB_[1]; outRGB[2] = lastRGB_[2]; return true; }
        return false;
    }
    bool ok = true;
    const unsigned char* p = (const unsigned char*)ms.pData;
    switch (td.Format) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: {
        const unsigned short* h = (const unsigned short*)p;
        outRGB[0] = HalfToFloat(h[0]); outRGB[1] = HalfToFloat(h[1]); outRGB[2] = HalfToFloat(h[2]);
        break; }
    case DXGI_FORMAT_R32G32B32A32_FLOAT: {
        const float* f = (const float*)p;
        outRGB[0] = f[0]; outRGB[1] = f[1]; outRGB[2] = f[2];
        break; }
    case DXGI_FORMAT_R10G10B10A2_UNORM: {
        unsigned int v = *(const unsigned int*)p;
        // HDR10 PQ/Rec2020 — approximate back to scRGB is non-trivial; report raw
        // normalized so the probe still tracks. Treated as already ~linear-ish.
        outRGB[0] = ((v >>  0) & 0x3FF) / 1023.0f;
        outRGB[1] = ((v >> 10) & 0x3FF) / 1023.0f;
        outRGB[2] = ((v >> 20) & 0x3FF) / 1023.0f;
        break; }
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: {
        outRGB[2] = p[0] / 255.0f; outRGB[1] = p[1] / 255.0f; outRGB[0] = p[2] / 255.0f;
        break; }
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: {
        outRGB[0] = p[0] / 255.0f; outRGB[1] = p[1] / 255.0f; outRGB[2] = p[2] / 255.0f;
        break; }
    default:
        ok = false;
    }
    context_->Unmap(staging_[readIdx].Get(), 0);
    pending_[readIdx] = false;
    if (ok) { lastRGB_[0] = outRGB[0]; lastRGB_[1] = outRGB[1]; lastRGB_[2] = outRGB[2]; haveLast_ = true; }
    return ok;
}

void PixelProbe::Shutdown() {
    for (auto& s : staging_) s.Reset();
    context_.Reset();
    device_.Reset();
}
