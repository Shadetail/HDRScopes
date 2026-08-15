// Vectorscope: ICtCp chroma distribution with primary/secondary target boxes
// and an optional skin-tone line.
#pragma once
#include "scope/ChromaScope.h"

class VectorScope : public ChromaScope {
public:
    const char* Name() const override { return "Vectorscope"; }
    Margins GetMargins(const Settings&) const override { return { 10, 10, 10, 10 }; }
    void DrawOverlay(ImDrawList*, const ScopeFrame&, Settings&) override;
    void DrawControls(Settings&) override;
protected:
    PlotRange Range(const Settings&) const override { return { 0, 0, 1, 0, 1, kScale }; }
    float Gain(const Settings& s) const override { return s.vectorGain; }
    bool  ShowExtents(const Settings& s) const override { return s.vectorExtents; }
    float ExtentsOpacity(const Settings& s) const override { return s.vectorExtentsOpacity; }
    bool  Colorized(const Settings& s) const override { return s.vectorColorize; }
private:
    static constexpr float kScale = 0.9f;
};
