// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
//
// CameraUnlock.ini and what the conversion moved into code: the committed file is the table's
// fresh render, the defaults the dev build ran on map to the defaults, a first start creates the
// committed file, the toggles save only their own lines and leave Defaults.ini and
// HeadTracking.ini alone, End's row cannot be saved, a FOV outside 0 or 30 to 150 is refused,
// and a folder the ANSI code page cannot name imports as the dev build read it.
//
// `--render-config <path>` writes the committed file instead (pixi run render-config).

#include "config.h"

#include "legacy_config/legacy_config.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace headtracking;

namespace {

namespace cfg = cameraunlock::config;
namespace fs = std::filesystem;

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// The file a first start creates.
std::string Rendered() {
    return cfg::RenderCanonicalFresh(MakeConfigTable(), {kConfigDisplayName});
}

std::string Committed() {
    return ReadBytes(fs::path(PORTAL_COMMITTED_CONFIG));
}

void TestCommittedConfigIsRendered() {
    Check(Committed() == Rendered(), "config/CameraUnlock.ini is the table's fresh render (pixi run render-config)");
}

// A scratch game folder, and a Defaults.ini of its own beside it that the first load creates.
struct Scratch {
    fs::path root;
    fs::path game;
    fs::path defaults;

    explicit Scratch(const std::wstring& gameFolder) {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        root = fs::path(temp) / (L"portal-config-tests-" + std::to_wstring(GetCurrentProcessId()));
        game = root / gameFolder;
        fs::remove_all(game);
        fs::create_directories(game);
        fs::create_directories(root / L"user");
        defaults = root / L"user" / L"CameraUnlock" / L"Defaults.ini";
    }
    ~Scratch() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    cfg::ConfigOwnerOptions<Config> Options() const {
        return MakeConfigOwnerOptions(game.wstring() + L"\\", cfg::DefaultsFile::At(defaults.wstring()));
    }
    fs::path ConfigPath() const { return game / kConfigFileName; }
    fs::path LegacyPath() const { return game / kLegacyConfigFileName; }
};

std::vector<std::string> Listing(const fs::path& dir) {
    std::vector<std::string> names;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir)) names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

std::string AllValues(const Config& c) {
    return cfg::RenderCanonical(MakeConfigTable(), c, {kConfigDisplayName});
}

// With neither file there, the first start creates CameraUnlock.ini as the committed file and
// Defaults.ini with the built-in values, and no HeadTracking.ini.
void TestFirstStartCreatesTheCommittedFile() {
    const Scratch s(L"first-start");
    const auto loaded = cfg::ConfigOwner<Config>(s.Options()).Load();
    Check(loaded.status == cfg::ConfigLoadStatus::Created, "a first start with no file is Created");
    Check(ReadBytes(s.ConfigPath()) == Committed(), "a first start creates config/CameraUnlock.ini's bytes as CameraUnlock.ini");
    Check(Listing(s.game) == std::vector<std::string>{"CameraUnlock.ini"}, "a first start creates CameraUnlock.ini and nothing else");
    Check(fs::exists(s.defaults), "a first start creates Defaults.ini");
    Check(AllValues(loaded.config) == AllValues(MakeConfigTable().defaults()), "a first start runs on the built-in values");
}

// A fresh install and an upgrade from the dev build's defaults start the same: the map of the frozen
// defaults holds every row at the table's default, drops nothing, and every pose-shaping value
// is the shipped one, which the code now does.
void TestLegacyDefaultsMapToTheDefaults() {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const fs::path missing = fs::path(temp) / L"portal-no-such-folder" / kLegacyConfigFileName;
    const auto table = MakeConfigTable();
    Config mapped = table.defaults();
    const cfg::ImportResult result =
        MakeLegacyImport().run(cfg::LegacyInput{missing.wstring(), missing.string(), false}, mapped);
    Check(result.status == cfg::ImportStatus::Absent, "no file imports as Absent");
    Check(result.dropped.empty(), "the dev build's defaults drop nothing");
    Check(result.pose_shaping.size() == 16, "every sensitivity, inversion, deadzone and the unit scale is recorded");
    for (const cfg::PoseShapingValue& value : result.pose_shaping) {
        Check(value.folded, "[" + value.section + "] " + value.key + " at its shipped value is folded");
    }
    Check(AllValues(mapped) == AllValues(table.defaults()), "the dev build's defaults map to the defaults");
    Check(mapped.toggle_key_name == "End, Ctrl+Shift+Y" && mapped.cycle_tracking_mode_key_name == "PageUp, Ctrl+Shift+G" &&
              mapped.yaw_mode_key_name == "PageDown, Ctrl+Shift+H",
          "the old hotkeys and their chords become the fleet's key lists");
    Check(legacy::Config{}.pos_world_scale == kWorldUnitsPerMetre, "the code converts a lean at the shipped WorldScale");
}

std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < bytes.size()) {
        const size_t end = bytes.find("\r\n", start);
        lines.push_back(bytes.substr(start, end - start));
        start = end + 2;
    }
    return lines;
}

// The lines of `after` that differ from `before`, which must have as many lines.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before);
    const std::vector<std::string> b = Lines(after);
    if (a.size() != b.size()) return {"a line was added or removed"};
    std::vector<std::string> changed;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(b[i]);
    }
    return changed;
}

bool Contains(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& line : lines) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

// A save changes the lines of its rows and no other byte, writes a value over default, and
// touches neither Defaults.ini nor HeadTracking.ini; the yaw mode and the tracking mode persist,
// and End's row cannot be saved at all.
void TestTogglesSave() {
    const Scratch s(L"save");
    const std::string committed = Committed();
    const std::string legacyBytes = "[Network]\r\nEnableOnStartup=0\r\n";
    WriteBytes(s.ConfigPath(), committed);
    WriteBytes(s.LegacyPath(), legacyBytes);

    {
        cfg::ConfigOwner<Config> owner(s.Options());
        const auto loaded = owner.Load();
        Check(loaded.status == cfg::ConfigLoadStatus::Canonical, "the committed file loads as canonical");
        Check(loaded.config.enable_on_startup, "HeadTracking.ini is not read while CameraUnlock.ini exists");
        Check(Contains(loaded.log, "is left as it was and is not read"),
              "the log says HeadTracking.ini is not read while CameraUnlock.ini exists");
        const std::string defaultsBefore = ReadBytes(s.defaults);

        const cfg::ConfigSaveResult yaw = owner.Save([](Config& c) { c.world_space_yaw = false; });
        Check(yaw.status == cfg::ConfigSaveStatus::Saved, "the yaw mode saves");
        Check(Contains(yaw.log, "WorldSpaceYaw=false is now set for this game, and no longer follows Defaults.ini"),
              "the yaw save says WorldSpaceYaw stopped following Defaults.ini");
        const std::string afterYaw = ReadBytes(s.ConfigPath());
        Check(ChangedLines(committed, afterYaw) == std::vector<std::string>{"WorldSpaceYaw=false"},
              "saving the yaw mode writes its value over default and changes nothing else");

        const auto rotationOnly = cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::RotationOnly);
        Check(owner.Save([rotationOnly](Config& c) {
                  c.rotation_enabled = rotationOnly.rotation_enabled;
                  c.position_enabled = rotationOnly.position_enabled;
              }).status == cfg::ConfigSaveStatus::Saved,
              "the tracking mode saves");
        const std::string afterRotationOnly = ReadBytes(s.ConfigPath());
        Check(ChangedLines(afterYaw, afterRotationOnly) == std::vector<std::string>{"RotationEnabled=true", "PositionEnabled=false"},
              "saving rotation only writes the mode pair over default and changes nothing else");

        const auto positionOnly = cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::PositionOnly);
        Check(owner.Save([positionOnly](Config& c) {
                  c.rotation_enabled = positionOnly.rotation_enabled;
                  c.position_enabled = positionOnly.position_enabled;
              }).status == cfg::ConfigSaveStatus::Saved,
              "the third tracking mode saves");
        const std::string afterPositionOnly = ReadBytes(s.ConfigPath());
        Check(ChangedLines(afterRotationOnly, afterPositionOnly) ==
                  std::vector<std::string>{"RotationEnabled=false", "PositionEnabled=true"},
              "saving position only changes the mode pair and nothing else");

        Check(owner.Save([](Config&) {}).status == cfg::ConfigSaveStatus::Saved, "an empty save succeeds");
        Check(ReadBytes(s.ConfigPath()) == afterPositionOnly, "an empty save writes nothing");

        bool refused = false;
        try {
            owner.Save([](Config& c) { c.enable_on_startup = false; });
        } catch (const std::logic_error&) {
            refused = true;
        }
        Check(refused, "EnableOnStartup is not Writable, so the End toggle cannot persist");
        Check(ReadBytes(s.ConfigPath()) == afterPositionOnly, "a refused save writes nothing");

        Check(ReadBytes(s.defaults) == defaultsBefore, "saving leaves Defaults.ini as it was");
        Check(ReadBytes(s.LegacyPath()) == legacyBytes, "saving leaves HeadTracking.ini as it was");
    }

    const auto again = cfg::ConfigOwner<Config>(s.Options()).Load();
    Check(again.status == cfg::ConfigLoadStatus::Canonical && again.diagnostics.empty() && !again.config.world_space_yaw &&
              !again.config.rotation_enabled && again.config.position_enabled && again.config.enable_on_startup,
          "the saved yaw and tracking mode come back at the next start");
    Check((Listing(s.game) == std::vector<std::string>{"CameraUnlock.ini", "HeadTracking.ini"}),
          "the game folder holds CameraUnlock.ini and HeadTracking.ini and nothing else");
}

// The dev build took its folder from core's HostExeDirectoryNarrow, which refuses a folder the ANSI
// code page cannot name, and then read the bare name HeadTracking.ini, never the player's file:
// it ran on its defaults. Such a player migrates to the defaults, and HeadTracking.ini stays as
// it was.
void TestAFolderTheCodepageCannotNameImportsAsTheDevBuildReadIt() {
    const Scratch s(L"bw-\x4E2D");
    const std::string legacyBytes = "[Network]\r\nPort=5000\r\n";
    WriteBytes(s.LegacyPath(), legacyBytes);
    const auto loaded = cfg::ConfigOwner<Config>(s.Options()).Load();
    if (GetACP() == CP_UTF8) {
        std::printf("note: the ANSI code page is UTF-8 here, so the folder has an ANSI name\n");
        Check(loaded.status == cfg::ConfigLoadStatus::Migrated && loaded.config.udp_port == 5000,
              "with a UTF-8 code page the file is read");
        return;
    }
    Check(loaded.status == cfg::ConfigLoadStatus::Migrated, "a file the dev build never read migrates to its defaults");
    Check(loaded.config.udp_port == 4242, "the port is the dev build's default, as that build ran");
    Check(ReadBytes(s.LegacyPath()) == legacyBytes, "HeadTracking.ini stays as it was");
}

// [View] Fov and FovViewmodel take 0 or 30 to 150, as the dev build did. A value outside keeps the
// row's default, with a diagnostic naming the line.
void TestFovOutsideItsRangeIsRefused() {
    const auto table = MakeConfigTable();
    const auto read = [&table](const std::string& value) {
        Config c = table.defaults();
        const cfg::CanonicalIni doc =
            cfg::ParseCanonicalIni("[View]\r\nFov=" + value + "\r\nFovViewmodel=" + value + "\r\n");
        const bool clean = cfg::ApplyCanonical(doc, table, c).diagnostics.empty();
        return std::make_pair(clean, std::make_pair(c.fov_override, c.fov_viewmodel_override));
    };
    for (const char* ok : {"0", "30", "90.5", "150"}) {
        const auto r = read(ok);
        const float want = std::stof(ok);
        Check(r.first && r.second.first == want && r.second.second == want, std::string("FOV ") + ok + " is read");
    }
    for (const char* refused : {"29.9", "1", "150.5", "-1", "180"}) {
        const auto r = read(refused);
        Check(!r.first && r.second.first == 0.0f && r.second.second == 0.0f,
              std::string("FOV ") + refused + " is refused and keeps 0");
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
            WriteBytes(argv[2], Rendered());
            return 0;
        }
        if (argc != 1) {
            std::printf("usage: %s [--render-config <path>]\n", argv[0]);
            return 2;
        }

        TestCommittedConfigIsRendered();
        TestLegacyDefaultsMapToTheDefaults();
        TestFirstStartCreatesTheCommittedFile();
        TestTogglesSave();
        TestAFolderTheCodepageCannotNameImportsAsTheDevBuildReadIt();
        TestFovOutsideItsRangeIsRefused();
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }

    if (g_failures == 0) {
        std::printf("config tests: all passed\n");
        return 0;
    }
    std::printf("config tests: %d failure(s)\n", g_failures);
    return 1;
}
