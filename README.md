# HDRScopes

Real-time video scopes for the **live Windows HDR** signal (DaVinci/Nobe-style, but
for the actual Windows HDR output that Nobe can't read). Waveform · Histogram ·
Vectorscope · CIE Chromaticity, in a single sleek window.

## Stack
C++20 / DX11 / Dear ImGui (1.90.6, vendored clean in `third_party/imgui`). Captures
the desktop via DXGI Desktop Duplication as **scRGB FP16** (linear Rec.709, 1.0 = 80
nits). Color math lifted from SKIV's `colorspaces.hlsli`. Shaders compiled at runtime
from `shaders/` (path baked in via `HDRSCOPES_SHADER_DIR`) — editing a `.hlsl` needs
no rebuild.

## Build
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
Output: `build/Release/HDRScopes.exe`. Settings persist to
`%LOCALAPPDATA%\HDRScopes\settings.ini` (plus window placement).

## Scopes
- **Waveform** — per-column PQ-nit histogram over a fixed 0–10,000 nit axis. Luminance
  or RGB (with R/G/B toggle squares), colorize, extents (colored points or a 1px white
  envelope line, optionally supersampled), custom reference lines (draggable, log drag,
  Shift = finer, global thickness), and an SDR-white vertical-zoom mode that maps the
  axis top to the current Windows SDR-white level.
- **Histogram** — pixel count per nit bin: LRGB (4 stacked bands), overlaid RGB, or luma.
- **Vectorscope** — ICtCp chroma with primary/secondary targets and a skin-tone line.
- **CIE Chromaticity** — 1931 xy or 1976 u'v', with the spectral locus and Rec.709 /
  P3 / Rec.2020 gamut triangles + D65.

## UI
Single full-window scope area; **Controls** button (top-right) opens the settings popup;
layout presets **1 / 2 / 4-up** (top-right) with a per-panel scope picker; **FPS**
(toggle) bottom-right; **zoom** bottom-left (click to reset). Zoom-to-mouse (wheel),
middle-drag pan. Hover any pixel on the captured screen to mark where it lands on each
scope (white = luminance, R/G/B circles). The ImGui UI is drawn at the Windows SDR-white
brightness on HDR outputs while the scope graphs keep their true HDR nit values.

## Quality & performance
Quality presets Low → Per-pixel control the bin grid and source sample density; optional
**bilinear source downsample** suppresses nearest-sampling noise. Optional FPS limiter.
~110 FPS capturing live 4K HDR at High.

## Architecture
`CaptureSource` (DDA) → `IScope::Compute` (compute pass into a uint bin texture) →
`IScope::Render` (render-side resolve into an offscreen RT) → `ScopePanel` (HDR-preserving
image draw + `DrawOverlay` graticule/labels/probe). `ScopeFactory` + `ScopePanel` host
the 1/2/4-up layout. `util/Settings` persists everything; `util/SdrWhite` queries the
Windows SDR-white level; `util/PQ.h` mirrors the HLSL PQ math on the CPU for the overlays.

## Key gotcha
`IDXGIOutput5::DuplicateOutput1` returns `DXGI_ERROR_UNSUPPORTED` unless the process is
**per-monitor DPI aware** (`SetProcessDpiAwarenessContext` at startup).

## Notes / deferred
- Parade scope is not yet implemented (the picker entry currently maps to Waveform).
- Vectorscope target positions / skin-tone line are approximations.
- Sub-0 (undershoot) RGB excursions clamp to the bottom bin rather than rendering below
  the 0-line.
- Protected/DRM content captures black; a region on an SDR output reads meaningless —
  both documented-as-acceptable.
- Superseded dead files from v1 (`src/compute/Waveform.*`, `src/render/GraphView.*`,
  `src/render/Graticule.h`, `shaders/graph.hlsl`) are kept out of the build.
