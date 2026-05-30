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

// Quality: graph compute resolution. PerPixel means "match the source" (no
// subsampling); the others are fixed grids. Supersample multiplies the bin axis.
enum class Quality { Low = 0, Medium = 1, High = 2, Ultra = 3, PerPixel = 4 };

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
    Quality quality = Quality::High;
    bool   bilinearDownsample = true;
    Vec3f  graticuleColor = { 0.55f, 0.55f, 0.55f };
    float  graticuleOpacity = 0.55f;
    bool   colorize = true;
    bool   showHoverProbe = true;

    // ---- Waveform ----
    int    waveMode = 1;              // 0 = Luminance, 1 = RGB
    bool   channelEnabled[3] = { true, true, true };
    bool   extents = true;
    int    extentsStyle = 0;          // 0 = colored points, 1 = thin white line
    bool   extentsSupersample = true;
    float  gain = 0.05f;
    bool   sdrWhiteZoom = false;      // zoom vertical axis to SDR-white range

    // ---- Reference lines (shared by waveform) ----
    std::vector<RefLine> refLines = { {100.0, false}, {1000.0, false} };
    float  refLineThickness = 1.0f;

    // ---- Histogram ----
    int    histoMode = 0;             // 0 = LRGB rows, 1 = overlaid, 2 = luma
    float  histoGain = 0.05f;
    bool   histoChannelEnabled[3] = { true, true, true };

    // ---- Vectorscope ----
    float  vectorGain = 0.20f;
    bool   vectorShowSkin = true;

    // ---- CIE ----
    int    cieDiagram = 1;            // 0 = xy (1931), 1 = u'v' (1976)
    float  cieGain = 0.20f;
    bool   cieShowRec2020 = true, cieShowP3 = true, cieShowRec709 = true;

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
