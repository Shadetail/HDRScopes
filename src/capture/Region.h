// The sub-rect of the captured output to scope. Given to the compute pass as a
// constant buffer; cropping happens in-shader so changing region never touches
// the capture path.
#pragma once

#include "util/Common.h"

enum class RegionMode { FullOutput, Window, DragRect };

struct Region {
    RegionMode mode = RegionMode::FullOutput;
    // Rect in DESKTOP coordinates (matches GetWindowRect / drag selection).
    RECT desktopRect = {};
    HWND targetWindow = nullptr; // for Window mode, re-queried each frame

    // Resolve to pixel coordinates within the capture texture given the output's
    // desktop origin. Clamps to the texture bounds. Returns false if empty.
    bool ResolveToTexel(const RECT& outputDesktopRect, UINT texW, UINT texH,
                        int& outX, int& outY, int& outW, int& outH) const {
        RECT r = desktopRect;
        if (mode == RegionMode::Window && targetWindow && IsWindow(targetWindow)) {
            GetWindowRect(targetWindow, &r);
        }
        if (mode == RegionMode::FullOutput) {
            outX = 0; outY = 0; outW = (int)texW; outH = (int)texH;
            return texW > 0 && texH > 0;
        }
        // Translate desktop-space rect into capture-texture space.
        int x = r.left   - outputDesktopRect.left;
        int y = r.top    - outputDesktopRect.top;
        int w = r.right  - r.left;
        int h = r.bottom - r.top;
        // Clamp to texture bounds.
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > (int)texW) w = (int)texW - x;
        if (y + h > (int)texH) h = (int)texH - y;
        outX = x; outY = y; outW = w; outH = h;
        return w > 0 && h > 0;
    }
};
