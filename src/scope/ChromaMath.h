// CPU mirrors of the chroma mappings used by chroma_cs.hlsl, so graticule
// targets (vectorscope) and gamut triangles (CIE) land where points plot.
#pragma once
#include "util/PQ.h"
#include <array>

namespace chroma {

inline std::array<double,3> Mul3(const double m[9], const std::array<double,3>& v) {
    return { m[0]*v[0]+m[1]*v[1]+m[2]*v[2],
             m[3]*v[0]+m[4]*v[1]+m[5]*v[2],
             m[6]*v[0]+m[7]*v[1]+m[8]*v[2] };
}

// Rec709 linear -> ICtCp (matches colorspaces.hlsli Rec709toICtCp).
inline std::array<double,3> Rec709toICtCp(double r, double g, double b) {
    static const double Rec709toXYZ[9] = {
        0.4123907983303070, 0.3575843274593353, 0.1804807931184768,
        0.2126390039920806, 0.7151686549186706, 0.0721923187375068,
        0.0193308182060718, 0.1191947832703590, 0.9505321383476257 };
    static const double XYZtoLMS[9] = {
        0.3592, 0.6976, -0.0358,
       -0.1922, 1.1004,  0.0755,
        0.0070, 0.0749,  0.8434 };
    static const double LMStoICtCp[9] = {
        0.5000,  0.5000,  0.0000,
        1.6137, -3.3234,  1.7097,
        4.3780, -4.2455, -0.1325 };
    auto xyz = Mul3(Rec709toXYZ, { std::max(r,0.0), std::max(g,0.0), std::max(b,0.0) });
    auto lms = Mul3(XYZtoLMS, xyz);
    std::array<double,3> pql = {
        pq::LinearToPQ(std::max(lms[0],0.0), 125.0),
        pq::LinearToPQ(std::max(lms[1],0.0), 125.0),
        pq::LinearToPQ(std::max(lms[2],0.0), 125.0) };
    return Mul3(LMStoICtCp, pql);
}

// xy chromaticity -> u'v'.
inline void xyToUv(double x, double y, double& up, double& vp) {
    double d = -2.0 * x + 12.0 * y + 3.0;
    up = 4.0 * x / d;
    vp = 9.0 * y / d;
}

} // namespace chroma
