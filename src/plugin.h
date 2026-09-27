// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>

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

    // Builds the config owner for the files in `folder` (hl2.exe's, with its trailing
    // separator) and loads CameraUnlock.ini, importing HeadTracking.ini while it is absent. The
    // owner logs nothing: the result carries the lines for the caller to write once the log is
    // open, which the loaded [Debug] LogToFile decides. Called once, on the bootstrap thread,
    // before Initialize.
    cameraunlock::config::ConfigLoadResult<Config> LoadConfig(const std::wstring& folder);

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
    // Logs a save's lines, and its reason when it wrote nothing. Save never retries: the
    // session keeps the new value either way.
    void LogSave(const char* what, const cameraunlock::config::ConfigSaveResult& saved);

    // Built once in LoadConfig, before anything reads CameraUnlock.ini.
    std::optional<cameraunlock::config::ConfigOwner<Config>> m_owner;
    Config m_config;
    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_worldSpaceYaw{true};

    TrackerFeed m_feed;

    std::unique_ptr<CameraHook>    m_cameraHook;
    std::unique_ptr<CrosshairHook> m_crosshairHook;
    std::unique_ptr<HotkeyHandler> m_hotkeys;
};

Plugin& GetPlugin();

}  // namespace headtracking
