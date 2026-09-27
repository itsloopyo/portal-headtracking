// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#include "config.h"

#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/os/module_paths.h"
#include "debug_log.h"

namespace headtracking {

namespace {

using Reader = cameraunlock::IniReader;

// ----- Key readers ----------------------------------------------------------
//
// The INI is a system boundary: every value below is whatever a user typed, so
// each one is validated here and nothing downstream re-checks it. Reading and
// sanitizing are one step so a key's default is named once - passing one
// default to the reader and a different one to the check is exactly the drift
// this shape prevents.

float ReadFinite(const Reader& r, const char* section, const char* key, float fallback) {
    const float value = r.ReadFloat(section, key, fallback);
    return std::isfinite(value) ? value : fallback;
}

// A magnitude - sensitivities and limits. A negative one would scale the axis
// before the processor's asymmetric Z clamp, which is the one place inversion
// must never happen: it moves the generous forward allowance onto the backward
// lean. InvertX/Y/Z is the supported way to flip an axis, applied after the
// clamp.
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

// Remapping is allowed to any real virtual key - that is the user's call -
// except the Ctrl+Shift chord letters, which are already registered.
int ValidHotkeyOr(int vk, int fallback, const char* name) {
    for (int letter : hotkeys::kChordLetters) {
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

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it. The flag is shared across both
// retired keys on purpose - one line names both replacements.
//
// The old value is deliberately NOT migrated into the new keys. The single
// smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
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

// ----- Section loaders ------------------------------------------------------

// ReadInt yields 0 for a present-but-unparseable value rather than the
// fallback, so the range check below is what catches a typo too. Logged
// because this is the one config error whose symptom is "no head tracking at
// all": the mod binds the default port, the tracker keeps sending to the one
// in the file, and nothing else in the log says why the packets never arrive.
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

    // Zero or negative scale is not "no movement", it is a typo: it silently
    // kills 6DOF while every other setting still reports healthy. Fall back
    // loudly. A user wanting tracking off has Enabled and the mode hotkey.
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
    using namespace hotkeys;
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

// ----- Writing the default ini ---------------------------------------------
//
// One writer per section, paired with the loader above it: a key that gains a
// validation rule and a key that gains a comment sit next to each other, so
// neither half can quietly stop describing the other.

void WriteNetwork(cameraunlock::IniWriter& w) {
    w.WriteSection("Network");
    w.WriteInt("Port", kDefaultPort);
    w.WriteBool("EnableOnStartup", kDefaultEnableOnStartup);
}

// Sensitivity and Deadzone are one concern on the reading side (LoadRotation
// covers both) but two sections in the file, and the file's own order is what
// is preserved here.
void WriteSensitivity(cameraunlock::IniWriter& w) {
    w.WriteSection("Sensitivity");
    w.WriteDouble("Yaw", kDefaultSensitivity);
    w.WriteDouble("Pitch", kDefaultSensitivity);
    w.WriteDouble("Roll", kDefaultSensitivity);
    w.WriteBool("InvertYaw", false);
    w.WriteBool("InvertPitch", false);
    w.WriteBool("InvertRoll", false);
}

void WriteSmoothing(cameraunlock::IniWriter& w) {
    w.WriteSection("Smoothing");
    w.WriteComment(" Picked per connection from the tracker's source address, and applied");
    w.WriteComment(" to both rotation and position. 0 = no smoothing, 1 = heavy.");
    w.WriteComment(" LocalSmoothing: tracker runs on this machine (loopback)");
    w.WriteDouble("LocalSmoothing", kDefaultLocalSmoothing);
    w.WriteComment(" RemoteSmoothing: tracker is a remote device on the network");
    w.WriteDouble("RemoteSmoothing", kDefaultRemoteSmoothing);
}

void WriteDeadzone(cameraunlock::IniWriter& w) {
    w.WriteSection("Deadzone");
    w.WriteDouble("Yaw", kDefaultDeadzone);
    w.WriteDouble("Pitch", kDefaultDeadzone);
    w.WriteDouble("Roll", kDefaultDeadzone);
}

void WritePosition(cameraunlock::IniWriter& w) {
    w.WriteSection("Position");
    w.WriteComment(" 6DOF head position, applied to the render view origin only");
    w.WriteBool("Enabled", kDefaultPosEnabled);
    w.WriteComment(" WorldScale = Source units per metre of head movement (1 unit = 1 inch; 39.37 = 1:1)");
    w.WriteDouble("WorldScale", kDefaultPosWorldScale);
    w.WriteDouble("SensX", kDefaultPosSensitivity);
    w.WriteDouble("SensY", kDefaultPosSensitivity);
    w.WriteDouble("SensZ", kDefaultPosSensitivity);
    w.WriteComment(" Flip an axis if leaning moves the view the wrong way. Trackers");
    w.WriteComment(" disagree on whether they report in your frame or the camera's");
    w.WriteComment(" mirrored view of it. Inversion is applied after the limits below,");
    w.WriteComment(" so flipping Z keeps the generous forward allowance on leaning in.");
    w.WriteBool("InvertX", false);
    w.WriteBool("InvertY", false);
    w.WriteBool("InvertZ", false);
    w.WriteComment(" Movement envelope in metres before world scaling");
    w.WriteDouble("LimitX", kDefaultPosLimitX);
    w.WriteDouble("LimitY", kDefaultPosLimitY);
    w.WriteDouble("LimitZ", kDefaultPosLimitZ);
    w.WriteDouble("LimitZBack", kDefaultPosLimitZBack);
}

void WriteHotkeys(cameraunlock::IniWriter& w) {
    w.WriteSection("Hotkeys");
    w.WriteHex("Toggle", hotkeys::kVkEnd);
    w.WriteHex("YawMode", hotkeys::kVkPageDown);
    w.WriteComment(" Page Up: cycle 6DOF -> rotation-only -> position-only");
    w.WriteHex("ModeCycle", hotkeys::kVkPageUp);
}

void WriteView(cameraunlock::IniWriter& w) {
    w.WriteSection("View");
    w.WriteComment(" true = horizon-locked yaw (default), false = camera-local yaw");
    w.WriteBool("WorldSpaceYaw", kDefaultWorldSpaceYaw);
    w.WriteComment(" Field of view, same units as the game's fov_desired cvar (horizontal");
    w.WriteComment(" degrees at 4:3; the mod widens it for your real aspect ratio as the");
    w.WriteComment(" engine does). Written into the render view rather than the cvar, so it");
    w.WriteComment(" is not bound by fov_desired's own range. 0 = leave the game's FOV");
    w.WriteComment(" alone. Applies only while tracking is enabled (End).");
    w.WriteDouble("Fov", kDefaultFovOverride);
    w.WriteComment(" The weapon is drawn with its own FOV. Widening Fov leaves the gun");
    w.WriteComment(" looking oversized against the wider world: LOWER this to shrink it.");
    w.WriteComment(" 0 = leave the game's viewmodel FOV alone.");
    w.WriteDouble("FovViewmodel", kDefaultFovOverride);
}

void WriteDebug(cameraunlock::IniWriter& w) {
    w.WriteSection("Debug");
    w.WriteComment(" Writes HeadTracking.log next to hl2.exe, fresh every launch (the");
    w.WriteComment(" previous session is kept as HeadTracking.prev.log, and nothing else). It");
    w.WriteComment(" records the build profile, the tracker connection and the pose being");
    w.WriteComment(" applied. That is the file to attach to a bug report - leave it on.");
    w.WriteBool("LogToFile", kDefaultLogToFile);
}

// ----- Paths ----------------------------------------------------------------

// The core resolver rather than a local GetModuleFileNameA: that call truncates
// a long install path instead of failing, and best-fit ANSI narrowing can map a
// directory onto the name of a DIFFERENT one that exists, which would read and
// write the config somewhere the user never looks. Both are refused there. An
// unresolvable directory leaves the name relative, the same fallback this has
// always had.
std::string IniPath() {
    const std::string dir = cameraunlock::os::HostExeDirectoryNarrow();
    if (dir.empty()) return "HeadTracking.ini";
    return dir + "\\HeadTracking.ini";
}

void WriteDefaultIni(const std::string& path) {
    cameraunlock::IniWriter w;
    if (!w.Open(path)) {
        HT_LOG("[config] failed to write default ini at %s", path.c_str());
        return;
    }
    w.WriteComment(" Portal head tracking - default config");

    void (*const sections[])(cameraunlock::IniWriter&) = {
        WriteNetwork,  WriteSensitivity, WriteSmoothing, WriteDeadzone,
        WritePosition, WriteHotkeys,     WriteView,      WriteDebug,
    };
    for (auto section : sections) {
        w.WriteBlankLine();
        section(w);
    }
}

}  // namespace

bool Config::FileLoggingRequested() {
    // Open() is the existence check: it stats the path and returns false when
    // the file is not there, which is the same answer for a config that has not
    // been written yet and one that cannot be read. std::filesystem::exists
    // would be a second stat that THROWS on anything but a plain "not found",
    // and this runs on the bootstrap thread before the log is even open, where
    // an escaping exception is a terminated game.
    cameraunlock::IniReader r;
    if (!r.Open(IniPath())) return kDefaultLogToFile;
    return r.ReadBool("Debug", "LogToFile", kDefaultLogToFile);
}

Config Config::LoadOrCreateDefault() {
    const std::string path = IniPath();
    // The error_code overload, not the throwing one: see FileLoggingRequested.
    // A path that cannot be queried reads as absent, so the default is written
    // (and a failed write says so) rather than unwinding out of the thread.
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        WriteDefaultIni(path);
    }

    cameraunlock::IniReader r;
    Config c;
    if (!r.Open(path)) {
        HT_LOG("[config] could not open %s, using defaults", path.c_str());
        return c;
    }

    LoadNetwork(r, c);
    LoadRotation(r, c);
    LoadSmoothing(r, c);
    LoadPosition(r, c);
    LoadHotkeys(r, c);
    LoadView(r, c);
    return c;
}

}  // namespace headtracking
