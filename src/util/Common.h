// Shared small helpers: COM smart pointer + HRESULT checking + logging.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <cstdio>

using Microsoft::WRL::ComPtr;

// Lightweight logging to the debugger + stderr. Visible in DebugView / VS output.
inline void HDRLog(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    fprintf(stderr, "%s\n", buf);
}

inline bool HDRCheck(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        HDRLog("[HDRScopes] FAILED hr=0x%08lx : %s", (unsigned long)hr, what);
        return false;
    }
    return true;
}

#define HR_RET(expr, what)                  \
    do {                                    \
        HRESULT _hr = (expr);               \
        if (!HDRCheck(_hr, what)) return false; \
    } while (0)
