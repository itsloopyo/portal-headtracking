// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#include "hotkey_handler.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "config.h"
#include "plugin.h"

namespace headtracking {

namespace {

// The table read every list through the hotkey codec, so a list that does not parse here is a
// bug, not a player's typo.
void Register(cameraunlock::input::HotkeyPoller& poller, const std::string& list, const char* key,
              std::function<void()> action) {
    const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) {
        throw std::logic_error(std::string("[Hotkeys] ") + key + "=" + list + " does not parse: " + parsed.error);
    }
    cameraunlock::input::RegisterKeyBindings(poller, parsed.bindings, std::move(action));
}

}  // namespace

void HotkeyHandler::Start(Plugin& plugin, const Config& config) {
    // Each action is one call and nothing else: the Plugin method owns both the
    // state change and the log line, so every key of a list reports the
    // transition once.
    Register(m_poller, config.toggle_key_name, "ToggleKey", [&plugin]() { plugin.ToggleEnabled(); });
    Register(m_poller, config.yaw_mode_key_name, "YawModeKey", [&plugin]() { plugin.ToggleYawMode(); });
    Register(m_poller, config.cycle_tracking_mode_key_name, "CycleTrackingModeKey",
             [&plugin]() { plugin.CycleTrackingMode(); });

    m_poller.Start(kPollIntervalMs);
}

}  // namespace headtracking
