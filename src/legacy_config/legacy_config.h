// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

// The HeadTracking.ini reader of the dev pre-release (e187816), the only published build that
// read that file, frozen so a player updating from it is converted exactly as that build read
// it. Nothing in this folder is ever edited: tests/config_differential/ pins every file here by
// hash.
//
// Three things differ from the reader it was taken from (src/config.cpp at the dev build):
//   - it fills this frozen copy of that build's Config and its defaults, not the runtime type.
//     The defaults the old Config took from cameraunlock-core (PositionSettings, smoothing_utils.h)
//     are written out as the numbers they were, so a later core cannot move what an old file means;
//   - it writes nothing. A missing file reads as the defaults, which is what the old reader read
//     from the file it wrote there first, and Read reports it apart from a file it read;
//   - Config::FileLoggingRequested's separate read of [Debug] LogToFile joins it, as log_to_file.

#include <cstdint>

namespace headtracking::legacy {

enum class ReadStatus {
    Read,
    // No file at the path, or none the old reader could open. Config holds the defaults.
    Absent,
};

struct Config {
    uint16_t port = 4242;
    bool enabled_on_startup = true;

    float sens_yaw = 1.0f;
    float sens_pitch = 1.0f;
    float sens_roll = 1.0f;
    bool invert_yaw = false;
    bool invert_pitch = false;
    bool invert_roll = false;

    float local_smoothing = 0.0f;
    float remote_smoothing = 0.15f;

    float deadzone_yaw = 0.0f;
    float deadzone_pitch = 0.0f;
    float deadzone_roll = 0.0f;

    // [Position] Enabled: the tracking mode at startup, rotation and position or rotation only.
    bool  pos_enabled    = true;
    float pos_sens_x     = 1.0f;
    float pos_sens_y     = 1.0f;
    float pos_sens_z     = 1.0f;
    bool  pos_invert_x   = false;
    bool  pos_invert_y   = false;
    bool  pos_invert_z   = false;
    // Metres. LimitY bounded both vertical directions, up and down.
    float pos_limit_x      = 0.30f;
    float pos_limit_y      = 0.20f;
    float pos_limit_z      = 0.40f;
    float pos_limit_z_back = 0.10f;
    // Source units per metre of head movement.
    float pos_world_scale  = 39.37f;

    int toggle_vk     = 0x23;  // End
    int yaw_mode_vk   = 0x22;  // Page Down
    int mode_cycle_vk = 0x21;  // Page Up

    bool world_space_yaw = true;

    // Degrees, as fov_desired and viewmodel_fov. 0 leaves the game's own.
    float fov_override = 0.0f;
    float fov_viewmodel_override = 0.0f;

    // [Debug] LogToFile, which the old build read on its own before the rest.
    bool log_to_file = true;
};

// Reads the file at `path`, the ANSI path the dev build opened it by, into a default-constructed `c`.
ReadStatus Read(const char* path, Config& c);

}  // namespace headtracking::legacy
