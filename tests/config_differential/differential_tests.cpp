// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
//
// The differential test for the HeadTracking.ini reader.
//
//   Oracle     the reader of the newest published build (the dev pre-release, e187816,
//              core ee8cc72), compiled from its own sources (oracle_api.h)
//   Import     the frozen reader in src/legacy_config/
//
// Comparison 1, oracle against import, is what a player sees change that the conversion did
// not cause: commits since the published build that change how the file is read. There are
// none. src/config.cpp, config.h and hotkeys.h were unchanged from the dev build to the commit
// the reader was frozen at, and every core source either reader compiles holds the same bytes at
// ee8cc72 and at the pin, which SourcesAreThePinnedOnes checks.
//
// Inputs: the published build's first-run file (no build shipped or seeded a config, so every
// player's file started as that one), no file, an empty file, core's corpus of mutations of the
// first-run file, and the first-run file with each hotkey set to each code from 0x01 to 0xFE.

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "legacy_config/legacy_config.h"
#include "oracle_api.h"

#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace {

using cameraunlock::TrackingMode;
namespace cfg = cameraunlock::config;
namespace fs = std::filesystem;
namespace testing = cameraunlock::config::testing;

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL %s\n", what.c_str());
}

// ---- Provenance ------------------------------------------------------------------------------
//
// Every source the oracle and the import compile, pinned by the SHA-256 of its bytes. The
// oracle's files are the published build's, taken with `git show dev:src/<file>`. The core
// files both readers compile are hash-equal to `git -C cameraunlock-core show ee8cc72:<path>`,
// so the readers differ only where the mod's own reader changed.
// The frozen import is pinned at the commit that froze it, so nothing edits it afterwards.

struct Pinned {
    const char* path;
    const char* sha256;
};

constexpr Pinned kPinned[] = {
    // The oracle: dev:src/...
    {"tests/config_differential/oracle/src/config.cpp", "ab36724fb5f49a4f1ec86e428a579b6e083d3ea66eb4a3155ccb744ce0ff89f8"},
    {"tests/config_differential/oracle/src/config.h", "39dea443ad592d45250e9ef727350e1a5b5fab6d51f9ad8298b1cec8c690e742"},
    {"tests/config_differential/oracle/src/hotkeys.h", "18bec90b2abc673dd141e69ec252c2d552d96a9b35700aa20644a12435bfa30c"},
    {"tests/config_differential/oracle/src/debug_log.h", "93da8c6ce36715bc6f9327274927513998aef9c87c176e71617b89cc12fce5cb"},
    // Both readers: core at the pin, hash-equal to ee8cc72:cpp/...
    {"cameraunlock-core/cpp/include/cameraunlock/config/ini_reader.h", "a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"},
    {"cameraunlock-core/cpp/src/config/ini_reader.cpp", "e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"},
    {"cameraunlock-core/cpp/include/cameraunlock/logging/file_log.h", "43bdd2ef8554c78e5f440333463750c13b95110fe672b0b6e273244df9e7d169"},
    {"cameraunlock-core/cpp/src/logging/file_log.cpp", "73c53c2baa06bbfebe8211f62678aa2b60cb95f604743d3686951ba56b87ea47"},
    // The oracle only: the headers the dev build's config.h and config.cpp include beside the reader.
    {"cameraunlock-core/cpp/include/cameraunlock/os/module_paths.h", "6d049431be9520bd07f4e3567e354d662c73e7ad44db47d00aae0f53a8233dea"},
    {"cameraunlock-core/cpp/include/cameraunlock/data/position_settings.h", "b24dceb8e25475aebc5a468a5c7362a4a4e64204d183d1408525345f32f547f5"},
    {"cameraunlock-core/cpp/include/cameraunlock/math/smoothing_utils.h", "fc2146f8c585e5f610c7234e302f59de4945679cfa28ff479ca47477ec073f22"},
    {"cameraunlock-core/cpp/include/cameraunlock/math/angle_utils.h", "d7a905270933e3cb0c4c361d29d3fd701655498cbcd1875ea79d180468bdbe6a"},
    // The import: src/debug_log.h is the dev build's, the legacy folder is frozen.
    {"src/debug_log.h", "93da8c6ce36715bc6f9327274927513998aef9c87c176e71617b89cc12fce5cb"},
    {"src/legacy_config/legacy_config.h", "263b11b1a38cc590a0486fadc8898ad7ff645ed6a3f164297ff79d8af95dd03a"},
    {"src/legacy_config/legacy_config.cpp", "85767f5d9080ac993b31e7dc88839badead8f30965cdfb02583f3d8c83deb096"},
};

std::string ReadFileBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string Sha256Hex(const std::string& bytes) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider(SHA256) failed");
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32] = {};
    const bool ok =
        BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)) &&
        BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                                      static_cast<ULONG>(bytes.size()), 0)) &&
        BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0));
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) throw std::runtime_error("SHA-256 failed");
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    for (unsigned char b : digest) {
        out += kHex[b >> 4];
        out += kHex[b & 15];
    }
    return out;
}

fs::path SourcePath(const std::string& relative) { return fs::path(PORTAL_SOURCE_DIR) / relative; }

void SourcesAreThePinnedOnes() {
    for (const Pinned& p : kPinned) {
        const std::string actual = Sha256Hex(ReadFileBytes(SourcePath(p.path)));
        if (actual != p.sha256) std::printf("  %s is %s\n", p.path, actual.c_str());
        Check(actual == p.sha256, std::string(p.path) + " holds the pinned bytes");
    }
}

// ---- Scratch folders -------------------------------------------------------------------------
//
// One folder per reading: GetPrivateProfileString, which both readers sit on, is free to cache
// the file it last read. `game` stands for the folder holding hl2.exe.

class Scratch {
public:
    Scratch() {
        static unsigned s_next = 0;
        wchar_t temp[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH + 1, temp) == 0) throw std::runtime_error("GetTempPathW failed");
        root_ = fs::path(temp) / ("portal_ht_diff_" + std::to_string(GetCurrentProcessId()) + "_" +
                                  std::to_string(s_next++));
        Remove();
        fs::create_directories(root_ / "game");
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    // A scanner can still hold a file the test just wrote, and a destructor must not throw, so a
    // folder left behind is reported and the run carries on.
    ~Scratch() {
        try {
            Remove();
        } catch (const fs::filesystem_error& e) {
            std::printf("  scratch folder left behind: %s\n", e.what());
        }
    }

    std::string dir() const { return (root_ / "game").string(); }
    std::string ini() const { return dir() + "\\HeadTracking.ini"; }

    void Write(const std::string& bytes) const {
        std::ofstream out(ini(), std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("cannot write " + ini());
    }

private:
    void Remove() const {
        if (!fs::exists(root_)) return;
        for (const auto& entry : fs::recursive_directory_iterator(root_)) {
            SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        fs::remove_all(root_);
    }

    fs::path root_;
};

// ---- What a reading does ---------------------------------------------------------------------

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

enum Action { kToggle, kCycleMode, kYawMode };

// One registered binding: the action, the virtual-key code, and the modifiers it needs (0, or
// Ctrl+Shift as cameraunlock::input::KeyModifiers spells it).
using Hotkey = std::tuple<int, int, unsigned>;
constexpr unsigned kPlain = 0;
constexpr unsigned kCtrlShift = 3;

// Everything a reading decides that the running mod acts on: the settings, the state at
// startup, what the pose pipeline is handed, and the bindings the poller registers.
struct Observed {
    int port = 0;
    bool start_enabled = false;
    bool start_world_yaw = false;
    int start_mode = 0;
    float sens_yaw = 0, sens_pitch = 0, sens_roll = 0;
    bool invert_yaw = false, invert_pitch = false, invert_roll = false;
    float deadzone_yaw = 0, deadzone_pitch = 0, deadzone_roll = 0;
    float local_smoothing = 0, remote_smoothing = 0;
    float pos_sens_x = 0, pos_sens_y = 0, pos_sens_z = 0;
    float limit_x = 0, limit_y = 0, limit_y_down = 0, limit_z = 0, limit_z_back = 0;
    // Metres to Source units per axis, carrying the inversion as its sign.
    float scale_x = 0, scale_y = 0, scale_z = 0;
    float fov = 0, fov_viewmodel = 0;
    bool log_to_file = false;
    std::vector<Hotkey> hotkeys;
};

std::vector<std::string> Differences(const Observed& a, const Observed& b) {
    std::vector<std::string> out;
    const auto flt = [&out](float x, float y, const char* what) {
        if (Bits(x) != Bits(y)) out.push_back(what);
    };
    if (a.port != b.port) out.push_back("UDP port");
    if (a.start_enabled != b.start_enabled) out.push_back("tracking on at startup");
    if (a.start_world_yaw != b.start_world_yaw) out.push_back("yaw mode at startup");
    if (a.start_mode != b.start_mode) out.push_back("tracking mode at startup");
    flt(a.sens_yaw, b.sens_yaw, "yaw sensitivity");
    flt(a.sens_pitch, b.sens_pitch, "pitch sensitivity");
    flt(a.sens_roll, b.sens_roll, "roll sensitivity");
    if (a.invert_yaw != b.invert_yaw) out.push_back("invert yaw");
    if (a.invert_pitch != b.invert_pitch) out.push_back("invert pitch");
    if (a.invert_roll != b.invert_roll) out.push_back("invert roll");
    flt(a.deadzone_yaw, b.deadzone_yaw, "yaw deadzone");
    flt(a.deadzone_pitch, b.deadzone_pitch, "pitch deadzone");
    flt(a.deadzone_roll, b.deadzone_roll, "roll deadzone");
    flt(a.local_smoothing, b.local_smoothing, "local smoothing");
    flt(a.remote_smoothing, b.remote_smoothing, "remote smoothing");
    flt(a.pos_sens_x, b.pos_sens_x, "position sensitivity x");
    flt(a.pos_sens_y, b.pos_sens_y, "position sensitivity y");
    flt(a.pos_sens_z, b.pos_sens_z, "position sensitivity z");
    flt(a.limit_x, b.limit_x, "limit x");
    flt(a.limit_y, b.limit_y, "limit y");
    flt(a.limit_y_down, b.limit_y_down, "limit y down");
    flt(a.limit_z, b.limit_z, "limit z");
    flt(a.limit_z_back, b.limit_z_back, "limit z back");
    flt(a.scale_x, b.scale_x, "position scale x");
    flt(a.scale_y, b.scale_y, "position scale y");
    flt(a.scale_z, b.scale_z, "position scale z");
    flt(a.fov, b.fov, "FOV override");
    flt(a.fov_viewmodel, b.fov_viewmodel, "viewmodel FOV override");
    if (a.log_to_file != b.log_to_file) out.push_back("log to file");
    if (a.hotkeys != b.hotkeys) out.push_back("hotkeys");
    return out;
}

// Hand copied, not compiled from the published sources: the oracle library exports only the
// reader. dev:src/hotkey_handler.cpp:25-31 (HotkeyHandler::Start): the three configured
// codes NavGuarded, the Y, H and G chords ChordGuarded.
std::vector<Hotkey> LegacyHotkeys(int toggle_vk, int yaw_mode_vk, int mode_cycle_vk) {
    std::vector<Hotkey> keys = {
        {kToggle, toggle_vk, kPlain}, {kYawMode, yaw_mode_vk, kPlain}, {kCycleMode, mode_cycle_vk, kPlain},
        {kToggle, 'Y', kCtrlShift},   {kYawMode, 'H', kCtrlShift},     {kCycleMode, 'G', kCtrlShift},
    };
    std::sort(keys.begin(), keys.end());
    return keys;
}

// Hand copied from the dev build, unchanged at the frozen reader's commit: src/plugin.cpp:28-29
// (tracking on as EnableOnStartup says, the yaw mode from WorldSpaceYaw), src/tracker_feed.cpp:
// 14-28 (sensitivity, inversion and deadzone onto the rotation processor) and 37-52 (the world
// scale signed by InvertX/Y/Z, rotation and position or rotation only as [Position] Enabled
// says, the smoothing pair), and src/position_mapping.h:13-45 (the position sensitivity, LimitY
// on both vertical bounds, the processor's own inversion left off).
template <class C>
Observed ObservePublished(const C& c) {
    Observed o;
    o.port = c.port;
    o.start_enabled = c.enabled_on_startup;
    o.start_world_yaw = c.world_space_yaw;
    o.start_mode = static_cast<int>(c.pos_enabled ? TrackingMode::RotationAndPosition : TrackingMode::RotationOnly);
    o.sens_yaw = c.sens_yaw;
    o.sens_pitch = c.sens_pitch;
    o.sens_roll = c.sens_roll;
    o.invert_yaw = c.invert_yaw;
    o.invert_pitch = c.invert_pitch;
    o.invert_roll = c.invert_roll;
    o.deadzone_yaw = c.deadzone_yaw;
    o.deadzone_pitch = c.deadzone_pitch;
    o.deadzone_roll = c.deadzone_roll;
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.pos_sens_x = c.pos_sens_x;
    o.pos_sens_y = c.pos_sens_y;
    o.pos_sens_z = c.pos_sens_z;
    o.limit_x = c.pos_limit_x;
    o.limit_y = c.pos_limit_y;
    o.limit_y_down = c.pos_limit_y;
    o.limit_z = c.pos_limit_z;
    o.limit_z_back = c.pos_limit_z_back;
    o.scale_x = c.pos_world_scale * (c.pos_invert_x ? -1.0f : 1.0f);
    o.scale_y = c.pos_world_scale * (c.pos_invert_y ? -1.0f : 1.0f);
    o.scale_z = c.pos_world_scale * (c.pos_invert_z ? -1.0f : 1.0f);
    o.fov = c.fov_override;
    o.fov_viewmodel = c.fov_viewmodel_override;
    o.log_to_file = c.log_to_file;
    o.hotkeys = LegacyHotkeys(c.toggle_vk, c.yaw_mode_vk, c.mode_cycle_vk);
    return o;
}

Observed ReadOracle(const std::string& dir) { return ObservePublished(portal_published::Start(dir)); }

Observed ReadImport(const std::string& ini) {
    headtracking::legacy::Config c;
    headtracking::legacy::Read(ini.c_str(), c);
    return ObservePublished(c);
}

// ---- Inputs ----------------------------------------------------------------------------------

fs::path DataPath(const char* name) { return SourcePath(std::string("tests/config_differential/data/") + name); }

// The published build's first-run file, extracted once from what its WriteDefaultIni writes and
// committed. FirstRunFileIsThePublishedBuilds holds it to that.
std::string FirstRunFile() { return ReadFileBytes(DataPath("dev-first-run.ini")); }

// Every key the frozen reader takes a value from, and how the corpus varies each one. The
// out-of-range values sit outside the range each key is refused or clamped outside, and a
// limit above the canonical rows' 10 metres. [Smoothing] Amount and [Position] Smoothing are
// read only to warn that they are ignored, so they are not among them.
std::vector<testing::MutationKey> CorpusKeys() {
    return {
        {"Network", "Port", "5252", {"0", "65536"}},
        {"Network", "EnableOnStartup", "0", {}},
        {"Sensitivity", "Yaw", "1.5", {}},
        {"Sensitivity", "Pitch", "0.5", {}},
        {"Sensitivity", "Roll", "2.0", {}},
        {"Sensitivity", "InvertYaw", "1", {}},
        {"Sensitivity", "InvertPitch", "1", {}},
        {"Sensitivity", "InvertRoll", "1", {}},
        {"Smoothing", "LocalSmoothing", "0.3", {"-0.5", "1.5"}},
        {"Smoothing", "RemoteSmoothing", "0.6", {"-0.5", "1.5"}},
        {"Deadzone", "Yaw", "2.0", {"-0.5"}},
        {"Deadzone", "Pitch", "1.5", {"-0.5"}},
        {"Deadzone", "Roll", "0.5", {"-0.5"}},
        {"Position", "Enabled", "0", {}},
        {"Position", "WorldScale", "50.0", {"-5"}},
        {"Position", "SensX", "2.0", {"-1.5"}},
        {"Position", "SensY", "3.0", {"-1.5"}},
        {"Position", "SensZ", "4.0", {"-1.5"}},
        {"Position", "InvertX", "1", {}},
        {"Position", "InvertY", "1", {}},
        {"Position", "InvertZ", "1", {}},
        {"Position", "LimitX", "0.25", {"-0.1", "11"}},
        {"Position", "LimitY", "0.3", {"-0.1", "11"}},
        {"Position", "LimitZ", "0.5", {"-0.1", "11"}},
        {"Position", "LimitZBack", "0.2", {"-0.1", "11"}},
        {"Hotkeys", "Toggle", "0x70", {"0x59", "0xFF"}, true},
        {"Hotkeys", "YawMode", "0x71", {"0x48", "0xFF"}, true},
        {"Hotkeys", "ModeCycle", "0x72", {"0x47", "0xFF"}, true},
        {"View", "WorldSpaceYaw", "0", {}},
        {"View", "Fov", "90", {"29", "151"}},
        {"View", "FovViewmodel", "40", {"29", "151"}},
        {"Debug", "LogToFile", "0", {}},
    };
}

// The keys of CorpusKeys, which the import names the same way from commit B on.
std::vector<cfg::LegacyKey> CorpusReads() {
    std::vector<cfg::LegacyKey> reads;
    for (const testing::MutationKey& k : CorpusKeys()) reads.push_back({k.section, k.key});
    return reads;
}

struct Input {
    std::string name;
    bool present;
    std::string bytes;
};

// The first-run file with [Hotkeys] `key` set to `code`.
std::string WithHotkey(const char* key, int shipped, int code) {
    std::string bytes = FirstRunFile();
    char line[64];
    std::snprintf(line, sizeof line, "%s=0x%X\r\n", key, shipped);
    const std::size_t at = bytes.find(line);
    if (at == std::string::npos) throw std::runtime_error(std::string("the first-run file has no line ") + line);
    char value[64];
    std::snprintf(value, sizeof value, "%s=0x%02X\r\n", key, code);
    return bytes.replace(at, std::strlen(line), value);
}

std::vector<Input> Inputs() {
    std::vector<Input> inputs = {
        {"dev first-run file", true, FirstRunFile()},
        {"no file", false, {}},
        {"empty file", true, {}},
    };
    for (testing::IniMutation& m : testing::GenerateIniMutations(FirstRunFile(), CorpusReads(), CorpusKeys())) {
        inputs.push_back({"corpus: " + m.name, true, std::move(m.bytes)});
    }
    const std::pair<const char*, int> hotkeys[] = {{"Toggle", 0x23}, {"YawMode", 0x22}, {"ModeCycle", 0x21}};
    for (const auto& [key, shipped] : hotkeys) {
        for (int code = 0x01; code <= 0xFE; ++code) {
            char name[48];
            std::snprintf(name, sizeof name, "%s=0x%02X", key, code);
            inputs.push_back({name, true, WithHotkey(key, shipped, code)});
        }
    }
    return inputs;
}

// ---- Checks ----------------------------------------------------------------------------------

// The first-run file committed as test data is what the published build writes. On a mismatch
// the published bytes are left beside this executable for a look.
void FirstRunFileIsThePublishedBuilds() {
    Scratch s;
    portal_published::Start(s.dir());
    const std::string written = ReadFileBytes(s.ini());
    const bool same = written == FirstRunFile();
    if (!same) {
        std::ofstream(SourcePath("build/dev-first-run.actual.ini"), std::ios::binary) << written;
    }
    Check(same, "dev-first-run.ini is what the published build writes at first run");
}

// Comparison 1. Nothing may differ, floats bit for bit.
void OracleAgainstImport(const std::vector<Input>& inputs) {
    int compared = 0;
    for (const Input& input : inputs) {
        Scratch for_oracle;
        Scratch for_import;
        if (input.present) {
            for_oracle.Write(input.bytes);
            for_import.Write(input.bytes);
        }
        const std::vector<std::string> diff =
            Differences(ReadOracle(for_oracle.dir()), ReadImport(for_import.ini()));
        for (const std::string& d : diff) std::printf("  comparison 1, %s: %s\n", input.name.c_str(), d.c_str());
        Check(diff.empty(), "comparison 1: oracle and import agree on " + input.name);
        ++compared;
    }
    std::printf("comparison 1: %d inputs\n", compared);
}

}  // namespace

int main() {
    // Unbuffered, so the lines before an uncaught exception reach the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SourcesAreThePinnedOnes();
    FirstRunFileIsThePublishedBuilds();
    const std::vector<Input> inputs = Inputs();
    OracleAgainstImport(inputs);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
