// Font atlas for a UI scale (window DPI / 96), shared by the main window and
// the region picker's own ImGui context so both look the same. The default
// 13px bitmap font stays for 1x (pixel-crisp); above that Consolas (ships with
// Windows) is loaded at the scaled size. Never call mid-frame; the caller
// invalidates the renderer's device objects (or hasn't created them yet) so
// the atlas re-uploads on the next frame.
#pragma once

#include "util/Common.h"
#include "imgui.h"

inline void BuildUiFonts(ImGuiIO& io, float s) {
    io.Fonts->Clear();
    io.FontGlobalScale = 1.0f;
    bool ttf = false;
    if (s > 1.001f) {
        char path[MAX_PATH];
        UINT n = GetWindowsDirectoryA(path, MAX_PATH);
        if (n > 0 && n < MAX_PATH - 20) {
            strcat_s(path, "\\Fonts\\consola.ttf");
            if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
                ttf = io.Fonts->AddFontFromFileTTF(path, (float)(int)(13.0f * s + 0.5f)) != nullptr;
        }
    }
    if (!ttf) {
        io.Fonts->AddFontDefault();
        // The default atlas is always 13px, so scale it in either direction.
        // This also makes the documented 0.5..1.0 test overrides scale the font
        // together with the style and explicitly-sized widgets.
        io.FontGlobalScale = s;
    }
}
