// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

#include <cstdint>
#include <string>

// The oracle: the HeadTracking.ini reader of the newest published build, the dev pre-release
// (e187816) against cameraunlock-core ee8cc72, compiled only into this test. oracle/src/ holds
// that build's config.h, config.cpp, hotkeys.h and debug_log.h byte for byte as the tag has them.
// Every core file they include is hash-equal at ee8cc72 and at the pin, so they compile against
// the pin's headers and sources (provenance in differential_tests.cpp). oracle_api.cpp builds
// them with their namespaces renamed, so they link beside today's code.
//
// The dev build is the only one published, and it neither shipped nor seeded a config: every
// player's file started as the one the build wrote at first launch.
namespace portal_published {

// headtracking::Config as the dev build declared it, field for field.
struct PublishedConfig {
    uint16_t port;
    bool enabled_on_startup;
    float sens_yaw;
    float sens_pitch;
    float sens_roll;
    bool invert_yaw;
    bool invert_pitch;
    bool invert_roll;
    float local_smoothing;
    float remote_smoothing;
    float deadzone_yaw;
    float deadzone_pitch;
    float deadzone_roll;
    bool pos_enabled;
    float pos_sens_x;
    float pos_sens_y;
    float pos_sens_z;
    bool pos_invert_x;
    bool pos_invert_y;
    bool pos_invert_z;
    float pos_limit_x;
    float pos_limit_y;
    float pos_limit_z;
    float pos_limit_z_back;
    float pos_world_scale;
    int toggle_vk;
    int yaw_mode_vk;
    int mode_cycle_vk;
    bool world_space_yaw;
    float fov_override;
    float fov_viewmodel_override;
    // What Config::FileLoggingRequested returned, which dllmain.cpp asked first.
    bool log_to_file;
};

// The dev build's start in `exe_dir`, which stands for the folder holding hl2.exe: BootstrapThread's
// Config::FileLoggingRequested(), then Plugin::Initialize's Config::LoadOrCreateDefault(),
// which writes the first-run file there when none exists.
PublishedConfig Start(const std::string& exe_dir);

}  // namespace portal_published
