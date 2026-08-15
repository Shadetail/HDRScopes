# Third-party notices

HDRScopes is licensed under the GNU GPLv3 (see [LICENSE](LICENSE)). It
incorporates the following third-party components, all MIT-licensed and
GPL-compatible:

## Dear ImGui

Vendored at `third_party/imgui/` (version 1.90.6, core + Win32/DX11 backends).

- Copyright (c) 2014-2024 Omar Cornut
- MIT License — see [third_party/imgui/LICENSE.txt](third_party/imgui/LICENSE.txt)
- https://github.com/ocornut/imgui

The `imstb_*.h` files inside are vendored copies of the stb libraries by Sean
Barrett (public domain / MIT, as noted in each file), distributed as part of
Dear ImGui.

## SKIV color-space shaders

`shaders/colorspaces.hlsli` is adapted from SKIV (Special K Image Viewer)'s
HLSL color-space conversion library. The original MIT license notice is
retained at the top of the file.

- Copyright (c) 2024 Andon "Kaldaien" Coleman
- SKIV project: Copyright (c) 2024 Aemony, MIT License
- https://github.com/SpecialKO/SKIV
