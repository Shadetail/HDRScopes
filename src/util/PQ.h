// Shared C++ PQ (SMPTE ST.2084) helpers, matching the HLSL in colorspaces.hlsli.
// The waveform vertical axis maps scRGB -> 0..1 via LinearToPQ(value, 125) where
// 125 scRGB == 10,000 nits (top of axis). scRGB = nits / 80.
#pragma once

#include <cmath>

namespace pq {

constexpr double kMaxPQ = 125.0; // scRGB value at 10,000 nits

inline double LinearToPQ(double x, double maxPQValue = kMaxPQ) {
    const double N  = 2610.0 / 4096.0 / 4.0;
    const double M  = 2523.0 / 4096.0 * 128.0;
    const double C1 = 3424.0 / 4096.0;
    const double C2 = 2413.0 / 4096.0 * 32.0;
    const double C3 = 2392.0 / 4096.0 * 32.0;
    if (x <= 0.0) return 0.0;
    double xx = std::pow(x / maxPQValue, N);
    double nd = (C1 + C2 * xx) / (1.0 + C3 * xx);
    return std::pow(nd, M);
}

inline double PQToLinear(double pos01, double maxPQValue = kMaxPQ) {
    const double N  = 2610.0 / 4096.0 / 4.0;
    const double M  = 2523.0 / 4096.0 * 128.0;
    const double C1 = 3424.0 / 4096.0;
    const double C2 = 2413.0 / 4096.0 * 32.0;
    const double C3 = 2392.0 / 4096.0 * 32.0;
    if (pos01 <= 0.0) return 0.0;
    double xp = std::pow(pos01, 1.0 / M);
    double num = std::fmax(xp - C1, 0.0);
    double den = (C2 - C3 * xp);
    double lin = (den != 0.0) ? std::pow(num / den, 1.0 / N) : 0.0;
    return lin * maxPQValue;
}

// nits <-> 0..1 vertical position (full 0..10,000 axis).
inline double NitsToPos01(double nits) { return LinearToPQ(nits / 80.0); }
inline double Pos01ToNits(double pos01) { return PQToLinear(pos01) * 80.0; }

} // namespace pq
