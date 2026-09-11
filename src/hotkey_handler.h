// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

#include "cameraunlock/input/hotkey_poller.h"

namespace headtracking {

class Plugin;

// Owns the polling thread that drives the mod's three actions from the
// keyboard: the nav-cluster keys the config binds, plus the fixed Ctrl+Shift
// chord alternatives in hotkeys.h.
class HotkeyHandler {
public:
    void Start(Plugin& plugin, int toggle_vk, int yaw_mode_vk, int mode_cycle_vk);

private:
    // ~60Hz. Fast enough that a tap is never missed, slow enough that the
    // thread costs nothing next to the render loop.
    static constexpr int kPollIntervalMs = 16;

    cameraunlock::input::HotkeyPoller m_poller;
};

}  // namespace headtracking
