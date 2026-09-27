// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

#include <cstdint>

#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

#include "hotkeys.h"

namespace headtracking {

// The one home for every shipped default. The struct initializers below, the
// generated HeadTracking.ini and the fallback used when a key is missing or
// malformed all read from here, so a default cannot drift between the file a
// user is handed and the value the mod actually runs with.
constexpr uint16_t kDefaultPort = 4242;
constexpr bool     kDefaultEnableOnStartup = true;

constexpr float kDefaultSensitivity = 1.0f;
constexpr float kDefaultDeadzone    = 0.0f;

constexpr float kDefaultLocalSmoothing  = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
constexpr float kDefaultRemoteSmoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

constexpr bool  kDefaultPosEnabled     = true;
constexpr float kDefaultPosSensitivity = 1.0f;
constexpr float kDefaultPosLimitX      = cameraunlock::PositionSettings{}.limit_x;
constexpr float kDefaultPosLimitY      = cameraunlock::PositionSettings{}.limit_y;
constexpr float kDefaultPosLimitZ      = cameraunlock::PositionSettings{}.limit_z;
constexpr float kDefaultPosLimitZBack  = cameraunlock::PositionSettings{}.limit_z_back;
constexpr float kDefaultPosWorldScale  = 39.37f;

constexpr bool  kDefaultWorldSpaceYaw = true;
constexpr float kDefaultFovOverride   = 0.0f;
// On by default. Off, the log holds a single loader line and a "no head
// tracking" report cannot be answered without first asking the user to turn
// this on and play again. The log is truncated per launch and its one
// steady-state line is throttled to a frame in two thousand, so leaving it on
// costs well under 100 KB an hour.
constexpr bool  kDefaultLogToFile     = true;

// An override outside this band is refused: the renderer builds a projection
// from tan(fov/2), so a value at or past 180 has none at all and one near it
// stretches the frame into uselessness.
constexpr float kMinFovOverride = 30.0f;
constexpr float kMaxFovOverride = 150.0f;

struct Config {
    uint16_t port = kDefaultPort;
    bool enabled_on_startup = kDefaultEnableOnStartup;

    float sens_yaw = kDefaultSensitivity;
    float sens_pitch = kDefaultSensitivity;
    float sens_roll = kDefaultSensitivity;
    bool invert_yaw = false;
    bool invert_pitch = false;
    bool invert_roll = false;

    // Smoothing is chosen per connection: local for a tracker on this machine
    // (loopback), remote for a device on the network. Both cover rotation and
    // position.
    float local_smoothing = kDefaultLocalSmoothing;
    float remote_smoothing = kDefaultRemoteSmoothing;

    float deadzone_yaw = kDefaultDeadzone;
    float deadzone_pitch = kDefaultDeadzone;
    float deadzone_roll = kDefaultDeadzone;

    // Positional (6DOF) tracking. Head displacement is applied to the render
    // view origin only - the player's own eye position and angles are
    // untouched, so bullets, traces and physics are unaffected. See
    // camera_hook.cpp.
    bool  pos_enabled    = kDefaultPosEnabled;
    float pos_sens_x     = kDefaultPosSensitivity;
    float pos_sens_y     = kDefaultPosSensitivity;
    float pos_sens_z     = kDefaultPosSensitivity;
    bool  pos_invert_x   = false;
    bool  pos_invert_y   = false;
    bool  pos_invert_z   = false;
    // Head-movement envelope in metres (clamped before world scaling). Z is
    // asymmetric: pos_limit_z = forward lean (generous), z_back = backward.
    float pos_limit_x      = kDefaultPosLimitX;
    float pos_limit_y      = kDefaultPosLimitY;
    float pos_limit_z      = kDefaultPosLimitZ;
    float pos_limit_z_back = kDefaultPosLimitZBack;
    // Source world units per metre of head movement. 1 unit = 1 inch, so
    // 39.37 is 1:1 with real-world head movement. Primary lean tuning knob.
    float pos_world_scale  = kDefaultPosWorldScale;

    int toggle_vk     = hotkeys::kVkEnd;
    int yaw_mode_vk   = hotkeys::kVkPageDown;
    // Page Up: cycles 6DOF -> rotation -> position.
    int mode_cycle_vk = hotkeys::kVkPageUp;

    // true  = horizon-locked yaw (yaw around world up axis, default)
    // false = camera-local yaw (yaw composed with pitch/roll)
    bool world_space_yaw = kDefaultWorldSpaceYaw;

    // Field of view in the same units as the game's own `fov_desired` cvar:
    // horizontal degrees referenced to a 4:3 screen, which the mod widens for
    // the actual viewport exactly as the engine does. 0 = leave the game's FOV
    // alone. Written straight into the render view the frame is built from,
    // which is also what keeps the crosshair reprojection consistent with it -
    // the reticle is projected through the engine's own matrices for that view.
    float fov_override = kDefaultFovOverride;

    // The weapon is drawn through a second FOV (CViewSetup::fovViewmodel). A
    // wider world FOV leaves the gun looking oversized against it; LOWER this
    // to shrink the gun. Same units, same 0 = leave alone.
    float fov_viewmodel_override = kDefaultFovOverride;

    // Reads [Debug] LogToFile on its own, before the rest of the config: the
    // log has to be open for the bootstrap lines that precede a full load, and
    // opening it is exactly what LogToFile=0 asks us not to do. Returns the
    // default when the ini does not exist yet (it is written later, with the
    // default in it).
    static bool FileLoggingRequested();

    // Reads <game folder>\HeadTracking.ini, writing it with the defaults above
    // first if it is not there yet. Every value is validated on the way in, so
    // nothing downstream re-checks one.
    static Config LoadOrCreateDefault();
};

}  // namespace headtracking
