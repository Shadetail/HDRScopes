# HDRScopes — Waveform

Real-time video scopes for the **live Windows HDR** signal (DaVinci/Nobe-style, but
for the actual Windows HDR output that Nobe can't read). This repo implements the
**Waveform** scope per [WAVEFORM_PLAN.md](WAVEFORM_PLAN.md); capture/color/compute
plumbing is built to be reused by Parade → Vectorscope → Histogram → CIE later.

## Stack
C++20 / DX11 / Dear ImGui (1.90.6, vendored clean in `third_party/imgui`). Color math
lifted from SKIV's `colorspaces.hlsli`.

## Build
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
Output: `build/Release/HDRScopes.exe`. Shaders in `shaders/` are compiled at runtime
(path baked in via `HDRSCOPES_SHADER_DIR`), so editing a `.hlsl` needs no rebuild.

## What works (v1)
- **Live HDR capture** via DXGI Desktop Duplication → scRGB FP16 (1.0 = 80 nits),
  persistent loop, ACCESS_LOST recovery, cursor excluded. ~110 FPS at 4K.
- **PQ-spaced nit axis**, fixed 0–10,000, decade graticule (1/10/100/1k/10k) + 10%
  minor ticks. Validated against a synthetic known-nit test pattern (Source → Test
  pattern): 10/100/1000-nit bands land exactly on their decade lines.
- **Luminance** and **RGB per-channel** waveform modes (product default: RGB + Extents).
- **Extents** (per-column min/max outline), **brightness/gain** + **graticule opacity**
  sliders, **2 reference lines** (draggable, nit readout).
- **Region crop** (full output / pick-window-under-cursor / numeric rect) done in-shader.
- **Pan/zoom** (wheel + middle-drag) and freely resizable graph viewport (render-side).
- **Live monitor switching** via the Monitor combo (rebuilds the duplicator at runtime).

## Architecture
`CaptureSource` (DDA) → `Waveform` (compute: clear → atomic per-column histogram +
min/max) → `GraphView` (render-side resolve/normalize into an offscreen RT) →
`Graticule` (ImGui overlay). See `WAVEFORM_PLAN.md` §5.

## Key gotcha
`IDXGIOutput5::DuplicateOutput1` returns `DXGI_ERROR_UNSUPPORTED` unless the process
is **per-monitor DPI aware** (`SetProcessDpiAwarenessContext` at startup).

## Notes / deferred
- Sub-0 (undershoot) RGB excursions currently clamp to the bottom bin rather than
  rendering below the 0-line (plan §3.4 refinement).
- Window layout uses a code-defined default (`io.IniFilename = nullptr`) during bring-up.
- Protected/DRM content captures black; a region on an SDR output reads meaningless —
  both documented-as-acceptable.
