// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#include "source_math.h"

#include <cmath>

#include "angles.h"

namespace headtracking::source {

float ScaleFovByWidthRatio(float fovDegrees, float ratio) {
    const float halfRad = fovDegrees * 0.5f * kDegToRad;
    return 2.0f * std::atan(std::tan(halfRad) * ratio) * kRadToDeg;
}

float UnscaleFovByWidthRatio(float fovDegrees, float ratio) {
    return ScaleFovByWidthRatio(fovDegrees, 1.0f / ratio);
}

void AngleVectors(const float* ang, float fwd[3], float right[3], float up[3]) {
    const float p = ang[0] * kDegToRad;
    const float y = ang[1] * kDegToRad;
    const float r = ang[2] * kDegToRad;
    const float sp = std::sin(p), cp = std::cos(p);
    const float sy = std::sin(y), cy = std::cos(y);
    const float sr = std::sin(r), cr = std::cos(r);
    fwd[0]   = cp * cy;            fwd[1]   = cp * sy;            fwd[2]   = -sp;
    right[0] = -sr * sp * cy + cr * sy;
    right[1] = -sr * sp * sy - cr * cy;
    right[2] = -sr * cp;
    up[0]    = cr * sp * cy + sr * sy;
    up[1]    = cr * sp * sy - sr * cy;
    up[2]    = cr * cp;
}

void BasisToAngles(const float* fwd, const float* left, const float* up, float* ang) {
    // Below this the forward vector is vertical to within float noise, and yaw
    // and roll collapse onto the same axis.
    constexpr float kGimbalEpsilon = 0.001f;

    const float xyDist = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1]);
    ang[0] = std::atan2(-fwd[2], xyDist) * kRadToDeg;
    if (xyDist > kGimbalEpsilon) {
        ang[1] = std::atan2(fwd[1], fwd[0]) * kRadToDeg;
        ang[2] = std::atan2(left[2], up[2]) * kRadToDeg;
    } else {
        // Looking straight up or down: Source folds the whole rotation into
        // yaw and zeroes roll.
        ang[1] = std::atan2(-left[0], left[1]) * kRadToDeg;
        ang[2] = 0.0f;
    }
}

void ApplyCameraLocalRotation(float* ang, float dpitch, float dyaw, float droll) {
    float fwd[3], right[3], up[3];
    AngleVectors(ang, fwd, right, up);

    const float head[3] = { dpitch, dyaw, droll };
    float hf[3], hr[3], hu[3];
    AngleVectors(head, hf, hr, hu);

    // AngleVectors works in an (x = forward, y = left, z = up) frame, so the
    // head vectors' components are already coordinates in the camera's own
    // frame - mapping them back out is one change of basis.
    const float camLeft[3] = { -right[0], -right[1], -right[2] };
    float outFwd[3], outRight[3], outUp[3], outLeft[3];
    for (int i = 0; i < 3; ++i) {
        outFwd[i]   = hf[0] * fwd[i] + hf[1] * camLeft[i] + hf[2] * up[i];
        outRight[i] = hr[0] * fwd[i] + hr[1] * camLeft[i] + hr[2] * up[i];
        outUp[i]    = hu[0] * fwd[i] + hu[1] * camLeft[i] + hu[2] * up[i];
        outLeft[i]  = -outRight[i];
    }
    BasisToAngles(outFwd, outLeft, outUp, ang);
}

}  // namespace headtracking::source
