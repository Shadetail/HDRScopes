// Small shared text formatting helpers.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// Format a nit value with magnitude-appropriate precision: ~4 significant
// figures, trailing zeros stripped. e.g. 0.000634, 0.0101, 13.5, 104.6, 500.
inline void FormatNits(double v, char* out, size_t n) {
    if (!(v > 0.0)) { snprintf(out, n, "0"); return; }
    int e = (int)std::floor(std::log10(v));
    int d = std::clamp(3 - e, 0, 9);  // decimals for ~4 sig figs
    snprintf(out, n, "%.*f", d, v);
    if (d > 0) {  // strip trailing zeros and a dangling decimal point
        char* dot = strchr(out, '.');
        if (dot) {
            char* end = out + strlen(out) - 1;
            while (end > dot && *end == '0') *end-- = '\0';
            if (end == dot) *end = '\0';
        }
    }
}
