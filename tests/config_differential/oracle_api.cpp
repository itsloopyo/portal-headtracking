// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
//
// Built with headtracking and cameraunlock renamed on the command line (tests/CMakeLists.txt),
// so the published reader and the core code it calls are a separate copy from today's, and
// nothing here can resolve to a symbol of the mod under test.

#include "oracle_api.h"

#include "cameraunlock/os/module_paths.h"
#include "config.h"

namespace {
std::string g_exe_dir;
}

// The dev build found its folder through core's HostExeDirectoryNarrow, which here would be this test
// executable's own. The oracle links this definition in place of core's module_paths.cpp, so
// the published IniPath() resolves to the scratch folder the test hands Start.
namespace cameraunlock::os {
std::string HostExeDirectoryNarrow() { return g_exe_dir; }
}  // namespace cameraunlock::os

namespace portal_published {

PublishedConfig Start(const std::string& exe_dir) {
    g_exe_dir = exe_dir;
    const bool log_to_file = headtracking::Config::FileLoggingRequested();
    const headtracking::Config c = headtracking::Config::LoadOrCreateDefault();
    return PublishedConfig{
        c.port,
        c.enabled_on_startup,
        c.sens_yaw,
        c.sens_pitch,
        c.sens_roll,
        c.invert_yaw,
        c.invert_pitch,
        c.invert_roll,
        c.local_smoothing,
        c.remote_smoothing,
        c.deadzone_yaw,
        c.deadzone_pitch,
        c.deadzone_roll,
        c.pos_enabled,
        c.pos_sens_x,
        c.pos_sens_y,
        c.pos_sens_z,
        c.pos_invert_x,
        c.pos_invert_y,
        c.pos_invert_z,
        c.pos_limit_x,
        c.pos_limit_y,
        c.pos_limit_z,
        c.pos_limit_z_back,
        c.pos_world_scale,
        c.toggle_vk,
        c.yaw_mode_vk,
        c.mode_cycle_vk,
        c.world_space_yaw,
        c.fov_override,
        c.fov_viewmodel_override,
        log_to_file,
    };
}

}  // namespace portal_published
