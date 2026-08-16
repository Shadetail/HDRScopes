// All persistent application state (including the Win32 window placement).
// Saved to settings.ini next to the exe (portable, local-first); falls back to
// %LOCALAPPDATA%\HDRScopes when the exe's folder isn't writable.
#pragma once

#include "util/Common.h"
#include <map>

struct Vec3f { float x = 0, y = 0, z = 0; };

enum class ScopeType { Waveform = 0, Parade = 1, Histogram = 2, Vectorscope = 3, CIE = 4 };
enum class LayoutMode { Single = 1, SideBySide = 2, Quad = 4 };

struct RefLine {
    double nits = 100.0;
    bool   enabled = true;
};

struct Settings {
    // ---- Global / window ----
    bool   showFps = true;
    int    fpsLimit = 0;              // 0 = unlimited
    bool   uiFollowSdrWhite = true;   // draw UI at the Windows SDR-white brightness
    bool   showTooltips = true;       // delayed hover tooltips on controls
    bool   debugShowTestPattern = false; // expose the synthetic test source (debug)
    bool   useTestPattern = false;    // currently using the test source

    // ---- Capture / region ----
    int    outputIndex = -1;          // -1 = auto (first HDR)
    int    regionMode = 0;            // 0 full, 1 window, 2 rect
    int    dragRect[4] = { 0, 0, 1920, 1080 }; // L,T,W,H desktop coords

    // ---- Layout ----
    LayoutMode layout = LayoutMode::Single;
    ScopeType  panelScope[4] = { ScopeType::Waveform, ScopeType::CIE,
                                 ScopeType::Histogram, ScopeType::Vectorscope };

    // ---- Shared scope look ----
    // Quality: scopes sample the source at 1/qualityDownsample resolution.
    // 1 = per pixel (default); larger = cheaper/coarser for weak GPUs.
    float  qualityDownsample = 1.0f;
    bool   bilinearDownsample = false;
    int    renderSupersample = 2;     // render scope RT at Nx then bilinear-downsample (1/2/4)
    float  sourceBlur = 0.0f;         // gaussian blur radius (source px) before scoping
    bool   blurExtents = true;        // does source blur also affect the extents trace?
    Vec3f  graticuleColor = { 0.55f, 0.55f, 0.55f };
    float  graticuleOpacity = 0.55f;
    bool   showHoverProbe = true;
    float  hoverCircleRadius = 24.0f; // probe marker size (px)
    bool   showHoverReadout = true;   // L/R/G/B nits at top-center (peaks when not hovering)
    bool   readoutBg = true;          // translucent black box behind the readout
    bool   showSdr8bit = true;        // also show 8-bit SDR values
    bool   showCursorNits = true;     // nit value attached to the cursor (waveform/histogram)

    bool perPixelQuality() const { return qualityDownsample <= 1.0001f; }

    // ---- Waveform ----
    int    waveMode = 1;              // 0 = Luminance, 1 = RGB
    bool   channelEnabled[3] = { true, true, true };
    bool   waveColorize = true;
    bool   waveExtents = true;
    int    waveExtentsStyle = 1;      // 0 = colored points, 1 = thin white line
    bool   waveExtentsSupersample = false;
    float  waveExtentsOpacity = 0.05f; // 0..1 opacity of the extents overlay
    float  gain = 0.05f;
    bool   sdrWhiteZoom = false;      // zoom vertical axis to SDR-white range
    bool   lowPass = false;           // low-pass filter the trace (smooth columns)
    float  lowPassAmount = 0.25f;

    // ---- Reference lines (shared by waveform) ----
    // Ship two useful HDR markers on by default (and one spare) as examples.
    std::vector<RefLine> refLines = { {500.0, true}, {2000.0, true}, {4000.0, false} };
    float  refLineThickness = 1.0f;
    float  refLineOpacity = 0.2f;

    // ---- Histogram ----
    int    histoMode = 0;             // 0 = LRGB rows, 1 = overlaid, 2 = luma
    float  histoGain = 4.4721e-05f;   // geometric mid of the log brightness slider
    bool   histoChannelEnabled[3] = { true, true, true };
    bool   histoSdrWhiteZoom = false; // zoom X (nit) axis to SDR-white range
    bool   histoColorize = true;

    // ---- Vectorscope ----
    float  vectorGain = 0.004f;
    bool   vectorShowSkin = true;
    float  vectorScale = 1.0f;        // Cb/Cr -> plot radius
    float  vectorSkinAngleDeg = 123.0f; // flesh/I-line angle (math convention, Y up)
    bool   vectorColorize = true;
    bool   vectorExtents = true;
    float  vectorExtentsOpacity = 0.05f;
    bool   vectorPQ = true;           // encode PQ (absolute nits) instead of Rec.709 gamma
    bool   vectorSdrMarkers = true;   // PQ mode: mark where 100% SDR primaries land

    // ---- CIE ----
    int    cieDiagram = 0;            // 0 = xy (1931), 1 = u'v' (1976)
    float  cieGain = 0.005f;
    bool   cieShowRec2020 = true, cieShowP3 = true, cieShowRec709 = true;
    int    chromaDotRadius = 0;       // plotted-point dilation radius (grid px), shared by vector/CIE
    bool   cieColorize = true;
    bool   cieExtents = true;
    float  cieExtentsOpacity = 0.05f;

    // ---- Per-panel view (zoom/pan) ----
    float  zoom[4] = { 1, 1, 1, 1 };
    float  panX[4] = { 0, 0, 0, 0 };
    float  panY[4] = { 0, 0, 0, 0 };

    // ---- Window placement (Win32 WINDOWPLACEMENT) ----
    int  wndL = -1, wndT = -1, wndR = -1, wndB = -1;
    int  wndShow = 0; // SW_* (0 = use default)

    // ---- IO ----
    void Load();
    void Save() const;
};
