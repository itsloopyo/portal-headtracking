// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#include "legacy_config/legacy_config.h"

#include <cmath>

#include "cameraunlock/config/ini_reader.h"
#include "debug_log.h"

namespace headtracking::legacy {

namespace {

using Reader = cameraunlock::IniReader;

constexpr uint16_t kDefaultPort = 4242;
constexpr bool     kDefaultEnableOnStartup = true;

constexpr float kDefaultSensitivity = 1.0f;
constexpr float kDefaultDeadzone    = 0.0f;

constexpr float kDefaultLocalSmoothing  = 0.0f;
constexpr float kDefaultRemoteSmoothing = 0.15f;

constexpr bool  kDefaultPosEnabled     = true;
constexpr float kDefaultPosSensitivity = 1.0f;
constexpr float kDefaultPosLimitX      = 0.30f;
constexpr float kDefaultPosLimitY      = 0.20f;
constexpr float kDefaultPosLimitZ      = 0.40f;
constexpr float kDefaultPosLimitZBack  = 0.10f;
constexpr float kDefaultPosWorldScale  = 39.37f;

constexpr bool  kDefaultWorldSpaceYaw = true;
constexpr float kDefaultFovOverride   = 0.0f;
constexpr bool  kDefaultLogToFile     = true;

constexpr float kMinFovOverride = 30.0f;
constexpr float kMaxFovOverride = 150.0f;

constexpr int kVkEnd      = 0x23;
constexpr int kVkPageDown = 0x22;
constexpr int kVkPageUp   = 0x21;
// The Ctrl+Shift chord letters the dev build registered, which a bare rebind could not take.
constexpr int kChordLetters[] = { 0x59, 0x47, 0x48 };  // Y, G, H

float ReadFinite(const Reader& r, const char* section, const char* key, float fallback) {
    const float value = r.ReadFloat(section, key, fallback);
    return std::isfinite(value) ? value : fallback;
}

float ReadNonNegative(const Reader& r, const char* section, const char* key, float fallback) {
    const float value = r.ReadFloat(section, key, fallback);
    return (std::isfinite(value) && value >= 0.0f) ? value : fallback;
}

float ReadSmoothing(const Reader& r, const char* key, float fallback) {
    const float value = r.ReadFloat("Smoothing", key, fallback);
    if (!std::isfinite(value)) return fallback;
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

float ReadDeadzone(const Reader& r, const char* key) {
    const float value = r.ReadFloat("Deadzone", key, kDefaultDeadzone);
    return (std::isfinite(value) && value > 0.0f) ? value : 0.0f;
}

int ValidHotkeyOr(int vk, int fallback, const char* name) {
    for (int letter : kChordLetters) {
        if (vk == letter) {
            HT_LOG("[config] hotkey %s (0x%02X) collides with a Ctrl+Shift chord "
                   "letter - using default 0x%02X", name, vk, fallback);
            return fallback;
        }
    }
    if (vk < 0x01 || vk > 0xFE) {
        HT_LOG("[config] hotkey %s (0x%02X) is not a virtual-key code "
               "- using default 0x%02X", name, vk, fallback);
        return fallback;
    }
    return vk;
}

float ReadFovOverride(const Reader& r, const char* key) {
    const float value = r.ReadFloat("View", key, kDefaultFovOverride);
    if (std::isfinite(value)
        && (value == 0.0f || (value >= kMinFovOverride && value <= kMaxFovOverride))) {
        return value;
    }
    HT_LOG("[config] [View] %s %.2f is out of range - leaving the game's FOV alone. "
           "Valid values are %.0f to %.0f (degrees, as fov_desired), or 0 for off.",
           key, value, kMinFovOverride, kMaxFovOverride);
    return kDefaultFovOverride;
}

void WarnRetiredSmoothingKey(const Reader& reader, const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (reader.ReadString(section, key, "").empty()) return;
    warned = true;
    HT_LOG(
        "Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

uint16_t ReadPort(const Reader& r, const char* key, uint16_t fallback) {
    const int port = r.ReadInt("Network", key, fallback);
    if (port < 1 || port > 65535) {
        HT_LOG("[config] [Network] %s %d is not a valid port - using %u. "
               "Point your tracker app at %u, or set a port in 1-65535.",
               key, port, fallback, fallback);
        return fallback;
    }
    return static_cast<uint16_t>(port);
}

void LoadNetwork(const Reader& r, Config& c) {
    c.port = ReadPort(r, "Port", kDefaultPort);
    c.enabled_on_startup = r.ReadBool("Network", "EnableOnStartup", kDefaultEnableOnStartup);
}

void LoadRotation(const Reader& r, Config& c) {
    c.sens_yaw   = ReadFinite(r, "Sensitivity", "Yaw",   kDefaultSensitivity);
    c.sens_pitch = ReadFinite(r, "Sensitivity", "Pitch", kDefaultSensitivity);
    c.sens_roll  = ReadFinite(r, "Sensitivity", "Roll",  kDefaultSensitivity);
    c.invert_yaw   = r.ReadBool("Sensitivity", "InvertYaw",   false);
    c.invert_pitch = r.ReadBool("Sensitivity", "InvertPitch", false);
    c.invert_roll  = r.ReadBool("Sensitivity", "InvertRoll",  false);

    c.deadzone_yaw   = ReadDeadzone(r, "Yaw");
    c.deadzone_pitch = ReadDeadzone(r, "Pitch");
    c.deadzone_roll  = ReadDeadzone(r, "Roll");
}

void LoadSmoothing(const Reader& r, Config& c) {
    c.local_smoothing  = ReadSmoothing(r, "LocalSmoothing",  kDefaultLocalSmoothing);
    c.remote_smoothing = ReadSmoothing(r, "RemoteSmoothing", kDefaultRemoteSmoothing);
    WarnRetiredSmoothingKey(r, "Smoothing", "Amount");
    WarnRetiredSmoothingKey(r, "Position", "Smoothing");
}

void LoadPosition(const Reader& r, Config& c) {
    c.pos_enabled = r.ReadBool("Position", "Enabled", kDefaultPosEnabled);

    c.pos_world_scale = ReadFinite(r, "Position", "WorldScale", kDefaultPosWorldScale);
    if (c.pos_world_scale <= 0.0f) {
        HT_LOG("[config] WorldScale must be positive (got %.3f) - using %.2f. "
               "To flip an axis use [Position] InvertX/Y/Z.",
               c.pos_world_scale, kDefaultPosWorldScale);
        c.pos_world_scale = kDefaultPosWorldScale;
    }

    c.pos_sens_x = ReadNonNegative(r, "Position", "SensX", kDefaultPosSensitivity);
    c.pos_sens_y = ReadNonNegative(r, "Position", "SensY", kDefaultPosSensitivity);
    c.pos_sens_z = ReadNonNegative(r, "Position", "SensZ", kDefaultPosSensitivity);
    c.pos_invert_x = r.ReadBool("Position", "InvertX", false);
    c.pos_invert_y = r.ReadBool("Position", "InvertY", false);
    c.pos_invert_z = r.ReadBool("Position", "InvertZ", false);

    c.pos_limit_x      = ReadNonNegative(r, "Position", "LimitX", kDefaultPosLimitX);
    c.pos_limit_y      = ReadNonNegative(r, "Position", "LimitY", kDefaultPosLimitY);
    c.pos_limit_z      = ReadNonNegative(r, "Position", "LimitZ", kDefaultPosLimitZ);
    c.pos_limit_z_back = ReadNonNegative(r, "Position", "LimitZBack", kDefaultPosLimitZBack);
}

void LoadHotkeys(const Reader& r, Config& c) {
    c.toggle_vk =
        ValidHotkeyOr(r.ReadHex("Hotkeys", "Toggle", kVkEnd), kVkEnd, "Toggle");
    c.yaw_mode_vk =
        ValidHotkeyOr(r.ReadHex("Hotkeys", "YawMode", kVkPageDown), kVkPageDown, "YawMode");
    c.mode_cycle_vk =
        ValidHotkeyOr(r.ReadHex("Hotkeys", "ModeCycle", kVkPageUp), kVkPageUp, "ModeCycle");
}

void LoadView(const Reader& r, Config& c) {
    c.world_space_yaw = r.ReadBool("View", "WorldSpaceYaw", kDefaultWorldSpaceYaw);
    c.fov_override           = ReadFovOverride(r, "Fov");
    c.fov_viewmodel_override = ReadFovOverride(r, "FovViewmodel");
}

}  // namespace

ReadStatus Read(const char* path, Config& c) {
    cameraunlock::IniReader r;
    if (!r.Open(path)) return ReadStatus::Absent;

    LoadNetwork(r, c);
    LoadRotation(r, c);
    LoadSmoothing(r, c);
    LoadPosition(r, c);
    LoadHotkeys(r, c);
    LoadView(r, c);
    c.log_to_file = r.ReadBool("Debug", "LogToFile", kDefaultLogToFile);
    return ReadStatus::Read;
}

}  // namespace headtracking::legacy
