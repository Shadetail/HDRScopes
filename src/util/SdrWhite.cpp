#include "util/SdrWhite.h"
#include <vector>
#pragma comment(lib, "user32.lib")

// DISPLAYCONFIG_SDR_WHITE_LEVEL and DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL
// are provided by wingdi.h in the Windows 10 SDK (1809+).

namespace sdrwhite {

float QueryNits(const wchar_t* gdiDeviceName, float fallbackNits) {
    UINT32 numPaths = 0, numModes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPaths, &numModes) != ERROR_SUCCESS)
        return fallbackNits;

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(numPaths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(numModes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &numPaths, paths.data(),
                           &numModes, modes.data(), nullptr) != ERROR_SUCCESS)
        return fallbackNits;

    for (UINT32 i = 0; i < numPaths; ++i) {
        const auto& path = paths[i];

        // Resolve the source's GDI device name and match it.
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src = {};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof(src);
        src.header.adapterId = path.sourceInfo.adapterId;
        src.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS)
            continue;
        if (gdiDeviceName && wcscmp(src.viewGdiDeviceName, gdiDeviceName) != 0)
            continue;

        DISPLAYCONFIG_SDR_WHITE_LEVEL wl = {};
        wl.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        wl.header.size = sizeof(wl);
        wl.header.adapterId = path.targetInfo.adapterId;
        wl.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&wl.header) != ERROR_SUCCESS)
            return fallbackNits;
        return 80.0f * (float)wl.SDRWhiteLevel / 1000.0f;
    }
    return fallbackNits;
}

float QueryPrimaryNits(float fallbackNits) {
    POINT pt = { 0, 0 };
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return fallbackNits;
    return QueryNits(mi.szDevice, fallbackNits);
}

} // namespace sdrwhite
