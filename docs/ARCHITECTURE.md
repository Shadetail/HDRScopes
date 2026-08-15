# Architecture

Developer notes for HDRScopes. For user-facing docs see the [README](../README.md).

## Stack

C++20 / DX11 / Dear ImGui (1.90.6, vendored clean in `third_party/imgui`).
Captures the desktop via DXGI Desktop Duplication as **scRGB FP16** (linear
Rec.709, 1.0 = 80 nits). Color-conversion HLSL adapted from SKIV's
`colorspaces.hlsli` (MIT, notice retained in the file).

## Pipeline

`CaptureSource` (DDA) → `IScope::Compute` (compute pass into a uint bin
texture) → `IScope::Render` (render-side resolve into an offscreen RT) →
`ScopePanel` (HDR-preserving image draw + `DrawOverlay` graticule/labels/
probe). `ScopeFactory` + `ScopePanel` host the 1/2/4-up layout.

- `util/Settings` persists everything to `%LOCALAPPDATA%\HDRScopes\settings.ini`
  (plus window placement).
- `util/SdrWhite` queries the Windows SDR-white level.
- `util/PQ.h` mirrors the HLSL PQ math on the CPU for the overlays.
- The ImGui UI is drawn at the Windows SDR-white brightness on HDR outputs
  while the scope graphs keep their true HDR nit values.

## Shaders

Compiled at runtime via d3dcompiler. Shipped builds load from a `shaders/`
folder next to the exe; dev builds fall back to the source-tree path baked in
by CMake (`HDRSCOPES_SHADER_DIR`), so editing a `.hlsl` needs no rebuild —
just relaunch.

## Build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `build/Release/HDRScopes.exe`. Static CRT, so the exe has no VC++
redistributable dependency.

## Key gotcha

`IDXGIOutput5::DuplicateOutput1` returns `DXGI_ERROR_UNSUPPORTED` unless the
process is **per-monitor DPI aware** (`SetProcessDpiAwarenessContext` at
startup).

## Known approximations / deferred work

- Parade scope is not yet implemented (the picker entry currently maps to
  Waveform).
- Vectorscope skin-tone line angle is an approximation (configurable);
  target positions are exact (75%/100% amplitudes of the Y'CbCr matrix).
- Sub-0 (undershoot) RGB excursions clamp to the bottom bin rather than
  rendering below the 0-line.
- Protected/DRM content captures black; a region on an SDR output reads
  meaningless — both documented-as-acceptable.

## Releasing

Bump `project(HDRScopes VERSION x.y.z)` in `CMakeLists.txt`, commit, then tag
`vx.y.z` and push the tag — the `release` workflow builds, packages
exe + shaders + licenses into a zip, and publishes a GitHub Release.
