// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#include "config.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "legacy_config/legacy_config.h"

#include "cameraunlock/config/head_tracking_config_table.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace headtracking {

namespace {

using cameraunlock::config::DroppedValue;
using cameraunlock::config::ImportResult;
using cameraunlock::config::LegacyInput;
using cameraunlock::config::LegacyKey;
using cameraunlock::config::LegacyPoseShaping;
using cameraunlock::config::PoseShapingValue;
using cameraunlock::input::KeyModifiers;

// A legacy hotkey code and the Ctrl+Shift chord the dev build always registered beside it, as one key
// list: what core gives for the code, then the chord. A Ctrl, Shift or Alt code gives nothing
// and is logged, and the player keeps the chord.
std::string KeyList(int vk, char letter, const char* key, std::vector<DroppedValue>& dropped) {
    const std::string code = cameraunlock::config::LegacyVirtualKeyToBindings(vk, "Hotkeys", key, dropped);
    const std::string chord = cameraunlock::input::FormatKeyBindings({{KeyModifiers::kCtrl | KeyModifiers::kShift, letter}});
    return code.empty() ? chord : code + ", " + chord;
}

ImportResult Import(const LegacyInput& input, Config& out) {
    // The dev build opened the file by the ANSI path of hl2.exe's folder. Where that path has a
    // character the code page cannot hold, core's HostExeDirectoryNarrow refused it, and the
    // build fell back to the bare name "HeadTracking.ini", which the Windows profile API looks
    // up in the Windows folder, not beside hl2.exe: it never read the player's file and ran on
    // its defaults.
    legacy::Config c;
    const legacy::ReadStatus read =
        input.ansi_lossy ? legacy::ReadStatus::Absent : legacy::Read(input.ansi_path.c_str(), c);

    std::vector<DroppedValue> dropped;
    std::vector<PoseShapingValue> shaping;

    out.udp_port = c.port;
    out.enable_on_startup = c.enabled_on_startup;
    out.world_space_yaw = c.world_space_yaw;

    // [Position] Enabled chose only the startup mode: the cycle key reached every mode either
    // way.
    const cameraunlock::TrackingModeChannels mode = cameraunlock::EncodeTrackingMode(
        c.pos_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                      : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = mode.rotation_enabled;
    out.position_enabled = mode.position_enabled;

    out.local_smoothing = c.local_smoothing;
    out.position.local_smoothing = c.local_smoothing;
    out.remote_smoothing = c.remote_smoothing;
    out.position.remote_smoothing = c.remote_smoothing;

    // The old file had one vertical limit, which the old runtime applied both ways.
    out.position.limit_x = c.pos_limit_x;
    out.position.limit_y = c.pos_limit_y;
    out.position.limit_y_down = c.pos_limit_y;
    out.position.limit_z = c.pos_limit_z;
    out.position.limit_z_back = c.pos_limit_z_back;

    out.fov_override = c.fov_override;
    out.fov_viewmodel_override = c.fov_viewmodel_override;
    out.log_to_file = c.log_to_file;

    // Every sensitivity, inversion and deadzone shipped at identity, so nothing folds and a
    // value the player changed is dropped. WorldScale shipped at kWorldUnitsPerMetre, which the
    // lean now converts by as a constant.
    LegacyPoseShaping(c.sens_yaw, 1.0f, "Sensitivity", "Yaw", shaping, dropped);
    LegacyPoseShaping(c.sens_pitch, 1.0f, "Sensitivity", "Pitch", shaping, dropped);
    LegacyPoseShaping(c.sens_roll, 1.0f, "Sensitivity", "Roll", shaping, dropped);
    LegacyPoseShaping(c.invert_yaw, false, "Sensitivity", "InvertYaw", shaping, dropped);
    LegacyPoseShaping(c.invert_pitch, false, "Sensitivity", "InvertPitch", shaping, dropped);
    LegacyPoseShaping(c.invert_roll, false, "Sensitivity", "InvertRoll", shaping, dropped);
    LegacyPoseShaping(c.deadzone_yaw, 0.0f, "Deadzone", "Yaw", shaping, dropped);
    LegacyPoseShaping(c.deadzone_pitch, 0.0f, "Deadzone", "Pitch", shaping, dropped);
    LegacyPoseShaping(c.deadzone_roll, 0.0f, "Deadzone", "Roll", shaping, dropped);
    LegacyPoseShaping(c.pos_world_scale, kWorldUnitsPerMetre, "Position", "WorldScale", shaping, dropped);
    LegacyPoseShaping(c.pos_sens_x, 1.0f, "Position", "SensX", shaping, dropped);
    LegacyPoseShaping(c.pos_sens_y, 1.0f, "Position", "SensY", shaping, dropped);
    LegacyPoseShaping(c.pos_sens_z, 1.0f, "Position", "SensZ", shaping, dropped);
    LegacyPoseShaping(c.pos_invert_x, false, "Position", "InvertX", shaping, dropped);
    LegacyPoseShaping(c.pos_invert_y, false, "Position", "InvertY", shaping, dropped);
    LegacyPoseShaping(c.pos_invert_z, false, "Position", "InvertZ", shaping, dropped);

    out.toggle_key_name = KeyList(c.toggle_vk, 'Y', "Toggle", dropped);
    out.cycle_tracking_mode_key_name = KeyList(c.mode_cycle_vk, 'G', "ModeCycle", dropped);
    out.yaw_mode_key_name = KeyList(c.yaw_mode_vk, 'H', "YawMode", dropped);

    // A setting the player never changed from what the dev build shipped follows Defaults.ini.
    // LimitY stood for both vertical bounds.
    using cameraunlock::config::schema::Concept;
    const legacy::Config shipped;
    cameraunlock::config::LegacyFollowsDefaultsIni follows;
    follows.Setting(Concept::UdpPort, c.port, shipped.port);
    follows.Setting(Concept::EnableOnStartup, c.enabled_on_startup, shipped.enabled_on_startup);
    follows.Setting(Concept::WorldSpaceYaw, c.world_space_yaw, shipped.world_space_yaw);
    follows.TrackingMode(c.pos_enabled, shipped.pos_enabled);
    follows.Setting(Concept::LocalSmoothing, c.local_smoothing, shipped.local_smoothing);
    follows.Setting(Concept::RemoteSmoothing, c.remote_smoothing, shipped.remote_smoothing);
    follows.Setting(Concept::PositionLimitX, c.pos_limit_x, shipped.pos_limit_x);
    follows.Setting(Concept::PositionLimitY, c.pos_limit_y, shipped.pos_limit_y);
    follows.Setting(Concept::PositionLimitYDown, c.pos_limit_y, shipped.pos_limit_y);
    follows.Setting(Concept::PositionLimitZ, c.pos_limit_z, shipped.pos_limit_z);
    follows.Setting(Concept::PositionLimitZBack, c.pos_limit_z_back, shipped.pos_limit_z_back);
    follows.Setting(Concept::ToggleKey, c.toggle_vk, shipped.toggle_vk);
    follows.Setting(Concept::CycleTrackingModeKey, c.mode_cycle_vk, shipped.mode_cycle_vk);
    follows.Setting(Concept::YawModeKey, c.yaw_mode_vk, shipped.yaw_mode_vk);

    return read == legacy::ReadStatus::Absent
               ? ImportResult::Absent(std::move(dropped), std::move(shaping), follows.Concepts())
               : ImportResult::Imported(std::move(dropped), std::move(shaping), follows.Concepts());
}

// Every key the frozen reader takes a value from. [Smoothing] Amount and [Position] Smoothing
// are read only to warn that they are ignored, so the owner reports them as not carried.
std::vector<LegacyKey> ImportKeys() {
    return {
        {"Network", "Port"},
        {"Network", "EnableOnStartup"},
        {"Sensitivity", "Yaw"},
        {"Sensitivity", "Pitch"},
        {"Sensitivity", "Roll"},
        {"Sensitivity", "InvertYaw"},
        {"Sensitivity", "InvertPitch"},
        {"Sensitivity", "InvertRoll"},
        {"Smoothing", "LocalSmoothing"},
        {"Smoothing", "RemoteSmoothing"},
        {"Deadzone", "Yaw"},
        {"Deadzone", "Pitch"},
        {"Deadzone", "Roll"},
        {"Position", "Enabled"},
        {"Position", "WorldScale"},
        {"Position", "SensX"},
        {"Position", "SensY"},
        {"Position", "SensZ"},
        {"Position", "InvertX"},
        {"Position", "InvertY"},
        {"Position", "InvertZ"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Hotkeys", "Toggle"},
        {"Hotkeys", "YawMode"},
        {"Hotkeys", "ModeCycle"},
        {"View", "WorldSpaceYaw"},
        {"View", "Fov"},
        {"View", "FovViewmodel"},
        {"Debug", "LogToFile"},
    };
}

}  // namespace

cameraunlock::config::CodecParseResult<float> FovCodec::Parse(std::string_view text) const {
    cameraunlock::config::CodecParseResult<float> read = angle_.Parse(text);
    if (read.ok() && read.value != 0.0f && read.value < kMin) {
        return {0.0f, "0, or an angle from 30 to 150"};
    }
    if (!read.ok()) read.error = "0, or an angle from 30 to 150";
    return read;
}

std::string FovCodec::Render(float value) const {
    if (value != 0.0f && value < kMin) {
        throw std::invalid_argument("FOV " + std::to_string(value) + " is neither 0 nor 30 to 150");
    }
    return angle_.Render(value);
}

cameraunlock::config::ConfigTable<Config> MakeConfigTable() {
    using cameraunlock::config::schema::Concept;
    cameraunlock::config::ConfigTable<Config> table = cameraunlock::config::HeadTrackingConfigTable<Config>(
        {Concept::UdpPort, Concept::EnableOnStartup, Concept::WorldSpaceYaw, Concept::RotationEnabled,
         Concept::LocalSmoothing, Concept::RemoteSmoothing, Concept::PositionEnabled, Concept::PositionLimitX,
         Concept::PositionLimitY, Concept::PositionLimitYDown, Concept::PositionLimitZ, Concept::PositionLimitZBack,
         Concept::ToggleKey, Concept::CycleTrackingModeKey, Concept::YawModeKey});
    table.Select(Concept::WorldSpaceYaw).Writable()
        .Select(Concept::RotationEnabled).Writable()
        .Select(Concept::PositionEnabled).Writable();
    table.Local("View", "Fov", &Config::fov_override, FovCodec(),
                "Field of view in degrees, as the game's fov_desired: horizontal, at 4:3, and the mod\n"
                "widens it for your screen as the game does. 0 leaves the game's own. Otherwise 30 to\n"
                "150, which fov_desired's own 75 to 120 does not bound. A zoom still narrows the view by\n"
                "the factor it always did. Applies only while head tracking is on.");
    table.Local("View", "FovViewmodel", &Config::fov_viewmodel_override, FovCodec(),
                "Field of view the weapon in your hands is drawn with, in the same degrees. A wider Fov\n"
                "leaves the weapon looking oversized: lower this to shrink it. 0 leaves the game's own.");
    table.Local("Debug", "LogToFile", &Config::log_to_file, cameraunlock::config::BoolCodec(),
                "true: write HeadTracking.log beside hl2.exe, new at every launch, with the launch before\n"
                "kept as HeadTracking.prev.log. It records the game build, the tracker connection and\n"
                "the view the mod draws. Attach it to a bug report.");
    return table;
}

cameraunlock::config::LegacyImport<Config> MakeLegacyImport() {
    return {&Import, ImportKeys()};
}

cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& folder,
                                                                        cameraunlock::config::DefaultsFile defaults) {
    cameraunlock::config::ConfigOwnerOptions<Config> options;
    options.path = folder + kConfigFileName;
    options.legacy_path = folder + kLegacyConfigFileName;
    options.table = MakeConfigTable();
    options.import = MakeLegacyImport();
    options.header.display_name = kConfigDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

}  // namespace headtracking
