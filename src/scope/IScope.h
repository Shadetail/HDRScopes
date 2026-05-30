// Common scope interface. Each scope: runs a compute pass over the captured
// scRGB texture, renders its graph into an offscreen RT, and draws an ImGui
// overlay (graticule/labels/probe) on top. A ScopePanel hosts one scope.
#pragma once

#include "util/Common.h"
#include "util/Settings.h"
#include "imgui.h"
#include <d3d11.h>

struct ScopeInput {
    ID3D11ShaderResourceView* srcSRV = nullptr;
    UINT srcW = 0, srcH = 0;
    int  cropX = 0, cropY = 0, cropW = 0, cropH = 0;
};

// Per-frame geometry + environment handed to render/overlay.
struct ScopeFrame {
    ImVec2 graphP0{}, graphP1{};   // screen rect of the graph image (inside margins)
    float  zoom = 1.0f, panX = 0.0f, panY = 0.0f; // pan in UV units
    float  sdrWhiteNits = 200.0f;
    bool   probeValid = false;      // a source pixel is under the cursor
    float  probeRGB[3] = { 0, 0, 0 }; // its sanitized scRGB value
    float  probeU = 0, probeV = 0;  // its 0..1 position within the crop region
};

struct Margins { float l = 0, r = 0, t = 0, b = 0; };

class IScope {
public:
    virtual ~IScope() {}
    virtual const char* Name() const = 0;
    virtual bool Init(ID3D11Device*) = 0;

    // Margins (px) the panel should reserve around the graph for labels.
    virtual Margins GetMargins(const Settings&) const { return {}; }
    // Some scopes (CIE/vector) want a square graph; return aspect or 0 for free.
    virtual float AspectRatio() const { return 0.0f; }

    virtual void Compute(const ScopeInput&, const Settings&) = 0;
    virtual void Render(UINT outW, UINT outH, const ScopeFrame&, const Settings&) = 0;
    virtual ID3D11ShaderResourceView* Result() const = 0;
    // Non-const Settings so overlays can handle interaction (ref-line drag, etc).
    virtual void DrawOverlay(ImDrawList*, const ScopeFrame&, Settings&) = 0;
    virtual void DrawControls(Settings&) = 0;
    virtual void Shutdown() = 0;
};
