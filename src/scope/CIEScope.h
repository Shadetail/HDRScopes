// CIE chromaticity scope: 1931 xy or 1976 u'v' diagram with the spectral locus
// and Rec.709 / P3 / Rec.2020 gamut triangles.
#pragma once
#include "scope/ChromaScope.h"

class CIEScope : public ChromaScope {
public:
    const char* Name() const override { return "CIE Chromaticity"; }
    Margins GetMargins(const Settings&) const override { return { 10, 10, 10, 10 }; }
    void DrawOverlay(ImDrawList*, const ScopeFrame&, Settings&) override;
    void DrawControls(Settings&) override;
protected:
    PlotRange Range(const Settings& s) const override {
        if (s.cieDiagram == 0) return { 1, 0.0f, 0.75f, 0.0f, 0.85f, 0 };  // xy
        return { 2, 0.0f, 0.62f, 0.0f, 0.60f, 0 };                          // u'v'
    }
    float Gain(const Settings& s) const override { return s.cieGain; }
    bool  ShowExtents(const Settings& s) const override { return s.cieExtents; }
    float ExtentsOpacity(const Settings& s) const override { return s.cieExtentsOpacity; }
    bool  Colorized(const Settings& s) const override { return s.cieColorize; }
};
