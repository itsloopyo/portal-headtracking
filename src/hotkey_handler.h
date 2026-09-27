// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

#include "cameraunlock/input/hotkey_poller.h"

namespace headtracking {

class Plugin;
struct Config;

// Owns the polling thread that drives the mod's three actions from the
// keyboard, each from its key list in CameraUnlock.ini, the Ctrl+Shift chords
// included.
class HotkeyHandler {
public:
    void Start(Plugin& plugin, const Config& config);

private:
    // ~60Hz. Fast enough that a tap is never missed, slow enough that the
    // thread costs nothing next to the render loop.
    static constexpr int kPollIntervalMs = 16;

    cameraunlock::input::HotkeyPoller m_poller;
};

}  // namespace headtracking
