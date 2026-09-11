// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

#include <atomic>
#include <memory>

#include "config.h"
#include "tracker_feed.h"

namespace headtracking {

class CameraHook;
class CrosshairHook;
class HotkeyHandler;

// Mod-level coordinator: owns the config, the tracker feed, the render-view
// hook and the hotkeys, and is the single object the detour asks for the
// frame's pose. Constructed once and never destroyed (see GetPlugin).
class Plugin {
public:
    Plugin();
    ~Plugin();

    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;

    void Initialize();

    bool IsEnabled() const { return m_enabled.load(); }
    void ToggleEnabled();

    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(); }
    void ToggleYawMode();

    void CycleTrackingMode();

    void Update();
    bool GetRotationRadians(float& yaw, float& pitch, float& roll) const;
    bool GetPositionOffset(float& x, float& y, float& z) const;

    const Config& GetConfig() const { return m_config; }

private:
    Config m_config;
    std::atomic<bool> m_enabled{kDefaultEnableOnStartup};
    std::atomic<bool> m_worldSpaceYaw{kDefaultWorldSpaceYaw};

    TrackerFeed m_feed;

    std::unique_ptr<CameraHook>    m_cameraHook;
    std::unique_ptr<CrosshairHook> m_crosshairHook;
    std::unique_ptr<HotkeyHandler> m_hotkeys;
};

Plugin& GetPlugin();

}  // namespace headtracking
