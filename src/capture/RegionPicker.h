// ShareX-style interactive region selection, drawn with D3D11 + Dear ImGui so
// the frozen preview keeps its HDR nit values (no GDI, which is SDR-only).
// The frozen frame is the app's own capture texture, shown full-screen dimmed
// on a second scRGB FP16 swapchain; the dragged rectangle shows the undimmed
// pixels. The selected rect is returned in DESKTOP coordinates (ready for
// Region/DragRect). Blocks (runs its own message loop) until confirm/cancel.
#pragma once

#include "util/Common.h"
#include <d3d11.h>

namespace regionpicker {

// The frame to freeze. srv may be null (capture unavailable or not live): the
// picker then works over black. Size and format are read off the SRV's
// texture; a frame that doesn't match the output's size is refused.
// uiBrightness is the backend UI multiplier the picker's own text uses (SDR
// white / 80 on an HDR output, 1 otherwise). appWindow gets DXGI's window
// association back once the picker's swapchain is gone.
struct Source {
    ID3D11Device*             device = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    float                     uiBrightness = 1.0f;
    HWND                      appWindow = nullptr;
};

// Returns true and fills outDesktopRect on confirm; false on cancel/empty.
bool PickScreenRegion(const Source& src, const RECT& outputRect, RECT& outDesktopRect);

} // namespace regionpicker
