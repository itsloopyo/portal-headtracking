// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#include "aim_state.h"

namespace headtracking {

namespace {

AimState g_state;

}  // namespace

void PublishAimState(const AimState& state) { g_state = state; }

const AimState& CurrentAimState() { return g_state; }

}  // namespace headtracking
