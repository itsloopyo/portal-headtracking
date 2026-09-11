// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

#include "cameraunlock/data/position_settings.h"
#include "config.h"

namespace headtracking {

// Config -> core PositionSettings. Pure and header-only so tests/ can lock it:
// both mappings below are silent when wrong (the camera still moves, only the
// travel is off), which is exactly the failure no in-game playtest catches.
inline cameraunlock::PositionSettings MakePositionSettings(const Config& c) {
    cameraunlock::PositionSettings ps;
    ps.sensitivity_x = c.pos_sens_x;
    ps.sensitivity_y = c.pos_sens_y;
    ps.sensitivity_z = c.pos_sens_z;

    // The processor's own inversion is deliberately left off and the user's
    // Invert* preference is applied after the clamp instead (see the world
    // scales in TrackerFeed::Start). Inverting here happens BEFORE the
    // asymmetric Z clamp, so a user flipping InvertZ would silently swap the
    // generous 0.40m forward allowance onto the backward lean and be left with
    // 0.10m for leaning in - the direction looks fixed while the travel
    // quietly breaks.
    ps.invert_x = false;
    ps.invert_y = false;
    ps.invert_z = false;

    ps.limit_x = c.pos_limit_x;
    // limit_y bounds UPWARD travel only; limit_y_down is a separate field with
    // its own default, so leaving it unset pins downward travel at the core's
    // 0.20m whatever the user put in LimitY. The INI exposes one vertical
    // limit, so it has to reach both bounds.
    ps.limit_y = c.pos_limit_y;
    ps.limit_y_down = c.pos_limit_y;
    // Z stays asymmetric: the INI exposes both bounds separately because
    // leaning in wants far more room than pulling back (player-model clipping).
    ps.limit_z = c.pos_limit_z;
    ps.limit_z_back = c.pos_limit_z_back;

    // local_smoothing / remote_smoothing are left at their defaults on purpose:
    // the session owns that pair and writes it into these settings on every
    // update (see TrackerFeed::Start).
    return ps;
}

}  // namespace headtracking
