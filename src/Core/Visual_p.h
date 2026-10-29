#pragma once
#include <iiVisualCodec/Spatial.h>
#include "Pixel_p.h"
#include <cmath>

namespace ii::visual::detail {
inline bool valid_pose(const spatial::Pose& pose) {
    double length = 0;
    for (auto value : pose.translation) if (!std::isfinite(value)) return false;
    for (auto value : pose.rotation) { if (!std::isfinite(value)) return false; length += value * value; }
    return std::isfinite(length) && length > 1e-20;
}
inline std::array<double, 3> rotate(std::array<double, 3> v, std::array<double, 4> q, bool inverse=false) {
    double norm = 0; for (auto value : q) norm += value * value;
    norm = std::sqrt(norm); for (auto& value : q) value /= norm;
    if (inverse) for (unsigned i=0;i<3;++i) q[i] = -q[i];
    const std::array<double,3> t{2*(q[1]*v[2]-q[2]*v[1]), 2*(q[2]*v[0]-q[0]*v[2]), 2*(q[0]*v[1]-q[1]*v[0])};
    return {v[0]+q[3]*t[0]+q[1]*t[2]-q[2]*t[1],
            v[1]+q[3]*t[1]+q[2]*t[0]-q[0]*t[2],
            v[2]+q[3]*t[2]+q[0]*t[1]-q[1]*t[0]};
}
}
