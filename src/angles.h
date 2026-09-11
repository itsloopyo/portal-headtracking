// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

namespace headtracking {

// Float-typed on purpose. The render path is float end to end - CViewSetup's
// QAngle, the tracker pose, the basis math - so narrowing the core's double
// cameraunlock::math constants at every use site would only add rounding.
constexpr float kPi       = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

}  // namespace headtracking
