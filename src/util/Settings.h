// All persistent application state. Saved to %LOCALAPPDATA%\HDRScopes\settings.ini
// (plus the Win32 window placement) and restored on launch.
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
    bool   bilinearDownsample = true;
    int    renderSupersample = 2;     // render scope RT at Nx then bilinear-downsample (1/2/4)
    float  sourceBlur = 0.0f;         // gaussian blur radius (source px) before scoping
    bool   blurExtents = true;        // does source blur also affect the extents trace?
    Vec3f  graticuleColor = { 0.55f, 0.55f, 0.55f };
    float  graticuleOpacity = 0.55f;
    bool   showHoverProbe = true;
    float  hoverCircleRadius = 9.0f;  // probe marker size (px)
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
    bool   waveExtentsSupersample = true;
    float  waveExtentsOpacity = 1.0f; // 0..1 opacity of the extents overlay
    float  gain = 0.05f;
    bool   sdrWhiteZoom = false;      // zoom vertical axis to SDR-white range
    bool   lowPass = false;           // low-pass filter the trace (smooth columns)
    float  lowPassAmount = 0.5f;

    // ---- Reference lines (shared by waveform) ----
    std::vector<RefLine> refLines = { {100.0, false}, {1000.0, false} };
    float  refLineThickness = 1.0f;
    float  refLineOpacity = 0.5f;

    // ---- Histogram ----
    int    histoMode = 0;             // 0 = LRGB rows, 1 = overlaid, 2 = luma
    float  histoGain = 0.0001f;
    bool   histoChannelEnabled[3] = { true, true, true };
    bool   histoSdrWhiteZoom = false; // zoom X (nit) axis to SDR-white range
    bool   histoColorize = true;

    // ---- Vectorscope ----
    float  vectorGain = 0.05f;
    bool   vectorShowSkin = true;
    float  vectorScale = 0.45f;       // Cb/Cr -> plot radius
    float  vectorSkinAngleDeg = 123.0f; // flesh/I-line angle (math convention, Y up)
    bool   vectorColorize = true;
    bool   vectorExtents = true;
    float  vectorExtentsOpacity = 1.0f;
    bool   vectorPQ = true;           // encode PQ (absolute nits) instead of Rec.709 gamma
    bool   vectorSdrMarkers = true;   // PQ mode: mark where 100% SDR primaries land

    // ---- CIE ----
    int    cieDiagram = 0;            // 0 = xy (1931), 1 = u'v' (1976)
    float  cieGain = 0.05f;
    bool   cieShowRec2020 = true, cieShowP3 = true, cieShowRec709 = true;
    int    chromaDotRadius = 0;       // plotted-point dilation radius (grid px), shared by vector/CIE
    bool   cieColorize = true;
    bool   cieExtents = true;
    float  cieExtentsOpacity = 1.0f;

    // ---- Per-panel view (zoom/pan) ----
    float  zoom[4] = { 1, 1, 1, 1 };
    float  panX[4] = { 0, 0, 0, 0 };
    float  panY[4] = { 0, 0, 0, 0 };

    // ---- Window placement (Win32 WINDOWPLACEMENT) ----
    int  wndL = -1, wndT = -1, wndR = -1, wndB = -1;
    int  wndShow = 0; // SW_* (0 = use default)

    // ---- IO ----
    static std::wstring FilePath();
    void Load();
    void Save() const;
};
