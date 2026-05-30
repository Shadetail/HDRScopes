// Hosts one scope: owns its instance (recreated when the panel's type changes),
// lays out the graph rect inside the panel (margins + optional square aspect),
// drives compute/render, draws the image (HDR-preserving) and the overlay, and
// handles zoom-to-mouse + middle-drag pan.
#pragma once

#include "scope/IScope.h"
#include <memory>

class ScopePanel {
public:
    void Init(ID3D11Device* device) { device_ = device; }

    // panelP0/P1: the ImGui screen rect allotted to this panel.
    // index: which Settings.zoom/pan slot + panelScope entry to use.
    void Draw(int index, const ImVec2& panelP0, const ImVec2& panelP1,
              const ScopeInput& input, Settings& s, float sdrWhiteNits,
              const ScopeFrame& probe, float uiBrightness);

    IScope* Scope() const { return scope_.get(); }
    ScopeType Type() const { return type_; }

private:
    ID3D11Device* device_ = nullptr;
    std::unique_ptr<IScope> scope_;
    ScopeType type_ = (ScopeType)-1;
};
