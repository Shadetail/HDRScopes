// Reads the Windows "SDR content brightness" (SDR white level) in nits for a
// given monitor via the DisplayConfig APIs. Default Windows value is 200 nits.
//   nits = 80 * SDRWhiteLevel / 1000   (raw 1000 = 80 nits, 6000 = 480 nits)
#pragma once

#include "util/Common.h"

namespace sdrwhite {

// gdiDeviceName matches DXGI_OUTPUT_DESC::DeviceName (e.g. L"\\\\.\\DISPLAY1").
// Returns nits, or fallbackNits if the query fails / monitor not found.
float QueryNits(const wchar_t* gdiDeviceName, float fallbackNits = 200.0f);

// SDR white of the primary monitor.
float QueryPrimaryNits(float fallbackNits = 200.0f);

} // namespace sdrwhite
