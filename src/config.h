// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

#include <string>
#include <string_view>

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/config_table.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/head_tracking_config.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/value_codecs.h"

namespace headtracking {

// CameraUnlock.ini, beside hl2.exe, in cameraunlock-core's canonical config format. One
// ConfigOwner reads and writes it; nothing else in the mod touches it.
constexpr const wchar_t* kConfigFileName = L"CameraUnlock.ini";
// The file every build before the canonical format read, beside kConfigFileName. Imported once
// while kConfigFileName is absent, and never written.
constexpr const wchar_t* kLegacyConfigFileName = L"HeadTracking.ini";
// The game's name as cameraunlock-core's data/games.json spells it.
constexpr const char* kConfigDisplayName = "Portal";

// Source units per metre of head movement: 1 unit is 1 inch, so this is 1:1 with the real
// lean. Every build before the canonical format read it from [Position] WorldScale, shipped and
// defaulted at this value, which the axis conversion now applies as a constant.
constexpr float kWorldUnitsPerMetre = 39.37f;

// [View] Fov and FovViewmodel: 0, the game's own, or 30 to 150 degrees. The renderer builds a
// projection from tan(fov/2), so a value at or past 180 has none at all, one near it stretches
// the frame into uselessness, and below 30 the frame is a telescope.
class FovCodec {
public:
    using Value = float;

    static constexpr float kMin = 30.0f;
    static constexpr float kMax = 150.0f;

    cameraunlock::config::CodecParseResult<float> Parse(std::string_view text) const;
    // Throws std::invalid_argument for a value Parse would not read back.
    std::string Render(float value) const;
    bool Equal(float a, float b) const { return angle_.Equal(a, b); }

private:
    cameraunlock::config::FloatCodec angle_{0.0f, kMax};
};

// Core's config with this game's own rows.
struct Config : cameraunlock::HeadTrackingConfig {
    // Field of view in the same units as the game's own fov_desired cvar: horizontal degrees
    // referenced to a 4:3 screen, which the mod widens for the actual viewport exactly as the
    // engine does. 0 leaves the game's FOV alone. Written straight into the render view the
    // frame is built from, which is also what keeps the crosshair reprojection consistent with
    // it: the reticle is projected through the engine's own matrices for that view.
    float fov_override = 0.0f;

    // The weapon is drawn through a second FOV (CViewSetup::fovViewmodel). A wider world FOV
    // leaves the gun looking oversized against it; LOWER this to shrink the gun. Same units,
    // same 0 = leave alone.
    float fov_viewmodel_override = 0.0f;

    // On by default. Off, the log holds nothing, and a "no head tracking" report cannot be
    // answered without first asking the player to turn this on and play again. The log is
    // truncated per launch and its one steady-state line is throttled to a frame in two
    // thousand, so leaving it on costs well under 100 KB an hour.
    bool log_to_file = true;
};

// The rows of CameraUnlock.ini. Only the tracking mode pair and WorldSpaceYaw are Writable: the
// mode and yaw hotkeys save the player's choice, and End changes the session only.
cameraunlock::config::ConfigTable<Config> MakeConfigTable();

// HeadTracking.ini as the dev build read it (legacy_config/), mapped into Config.
cameraunlock::config::LegacyImport<Config> MakeLegacyImport();

// The owner's options for the files in `folder` (with its trailing separator): the settings in
// CameraUnlock.ini, imported once from HeadTracking.ini. The mod passes DefaultsFile::PerUser()
// and a test a scratch file.
cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& folder,
                                                                        cameraunlock::config::DefaultsFile defaults);

}  // namespace headtracking
