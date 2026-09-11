// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
// Render-view injection for Portal (Source Engine, client.dll).
//
// Hook target: CViewRender::RenderView(CViewSetup* view, int clearFlags,
// int whatToDraw)  [__thiscall].  Discovered via MSVC RTTI: the CViewRender
// vftable, slot identified by the telemetry marker naming
// "CViewRender::RenderView". The arity is not a guess: the
// function ends in `ret 0xC`, so it pops exactly three stack arguments, and a
// detour declaring a fourth would leave the render thread's stack four bytes
// out on every frame.
//
// RenderView runs in the render phase, after the game has already produced the
// frame's CUserCmd / view angles (which drive aim, traces and weapon fire).
// We mutate the CViewSetup the renderer is about to consume - its angles,
// origin and FOV only - so the player sees the head-tracked view while the
// game's own camera (the player's eye angles) is untouched. Look and aim stay
// decoupled for free.
//
// Engagement is gated on a PE-fingerprint build-profile registry (append-only;
// see the "Maintain compatibility across new patches" doctrine and
// builds/build_registry.h). On any client.dll the registry does not recognise,
// the hook is never installed and the game runs vanilla.

#include "camera_hook.h"

#include <Windows.h>
#include <cstdint>

#include "aim_state.h"
#include "angles.h"
#include "builds/build_registry.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/memory/pe_fingerprint.h"
#include "config.h"
#include "debug_log.h"
#include "detour.h"
#include "fov_override.h"
#include "game_state.h"
#include "log_throttle.h"
#include "plugin.h"
#include "source_math.h"
#include "view_setup.h"

namespace headtracking {

namespace {

// ----- Resolved-at-load state ----------------------------------------------
const builds::BuildProfile* g_profile = nullptr;

using RenderViewFn = void(__fastcall*)(void* ecx, void* edx, void* view, int clearFlags,
                                       int whatToDraw);
RenderViewFn g_originalRenderView = nullptr;

// What the pose pipeline contributed to this frame's view, carried to the
// diagnostic line so it can report the delta alongside the resulting camera.
struct TrackingDelta {
    bool applied = false;
    float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;  // degrees, Source sign
    float x = 0.0f, y = 0.0f, z = 0.0f;           // Source units, camera basis
};

void Copy3(float* dst, const float* src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

// ----- Tracker -> Source axis mapping ---------------------------------------
//
// Every sign correction between the tracker frame and Source lives here, at
// the engine boundary. It must NOT be expressed as an INI `Invert*` default:
// the processor applies inversion BEFORE the asymmetric Z clamp, so an
// `InvertZ` used to flip the engine convention silently moves the generous
// LimitZ (0.40m) allowance onto the backward lean and leaves LimitZBack
// (0.10m) for leaning in. The direction still looks right, which is why that
// shape survives testing - the only symptom is that leaning in barely moves.
// The `Invert*` keys stay pure user preferences, defaulting to off.
//
// Tracker frame, as the pipeline delivers it:
//     yaw   > 0  = head turns right   Source yaw   > 0 = turn left   -> negate
//     pitch > 0  = head looks up      Source pitch > 0 = look down   -> negate
//     roll  > 0  = head tilts left    Source roll  > 0 = tilt right  -> negate
//     x     > 0  = head moves left    Source right vector            -> negate
//     y     > 0  = head moves up      Source up vector               -> as-is
//     z     < 0  = head leans forward Source forward vector          -> negate
constexpr float kYawSign   = -1.0f;
constexpr float kPitchSign = -1.0f;
constexpr float kRollSign  = -1.0f;
constexpr float kPosXSign  = -1.0f;
constexpr float kPosYSign  =  1.0f;
constexpr float kPosZSign  = -1.0f;

// ----- Diagnostics ----------------------------------------------------------

// Dense at first (the first frames, then ~every 200), because that is where
// install-time faults show; then one line per ~2000 frames for the rest of the
// session. See log_throttle.h for why the schedule has that shape.
constexpr int kBurstLines           = 6;     // opening frames logged unconditionally
constexpr int kEarlyLines           = 30;    // lines still treated as install-time
constexpr int kEarlyIntervalFrames  = 200;
constexpr int kSteadyIntervalFrames = 2000;

// Confirms the hook fires, the offsets resolve to a sane camera, and the
// head-tracking delta is being applied. Reads the CViewSetup after the delta
// has been written, so the line reports the view the frame will render with.
//
// Both FOVs are on it because that is the mod's read of the camera's field of
// view: a wrong CViewSetup::fov offset shows up here as a number that is not
// the player's fov_desired widened for their window, and there is nothing else
// in the log that would catch it.
void DiagnosticLog(const ViewSetup& view, const TrackingDelta& delta) {
    static LogThrottle s_throttle(kBurstLines, kEarlyLines, kEarlyIntervalFrames,
                                  kSteadyIntervalFrames);
    if (!s_throttle.ShouldLog()) return;

    const float* org = view.Origin();
    const float* ang = view.Angles();
    HT_LOG("[view] rect=%dx%d org=(%.1f,%.1f,%.1f) ang=(p%.2f y%.2f r%.2f) fov=%.2f/%.2f "
           "| track=%d delta=(p%.2f y%.2f r%.2f) pos=(%.2f,%.2f,%.2f)",
           view.RectWidth(), view.RectHeight(), org[0], org[1], org[2], ang[0], ang[1], ang[2],
           view.Fov(), view.FovViewmodel(), delta.applied ? 1 : 0,
           delta.pitch, delta.yaw, delta.roll, delta.x, delta.y, delta.z);
}

// ----- Pose injection -------------------------------------------------------

// Shifts the render origin in the CLEAN view basis - the one built from the
// angles before the head delta - so the lean follows the body rather than the
// head-rotated view. `delta` receives the applied offset in Source units for
// the diagnostic line.
void ApplyPositionalLean(const Plugin& plugin, const float* cleanAngles, float* org,
                         TrackingDelta& delta) {
    if (!plugin.GetPositionOffset(delta.x, delta.y, delta.z)) return;

    float fwd[3], right[3], up[3];
    source::AngleVectors(cleanAngles, fwd, right, up);

    delta.x *= kPosXSign;
    delta.y *= kPosYSign;
    delta.z *= kPosZSign;
    for (int i = 0; i < 3; ++i) {
        org[i] += right[i] * delta.x + up[i] * delta.y + fwd[i] * delta.z;
    }
}

// Composes the head rotation onto the render view's QAngle, in the yaw mode the
// player has selected. `delta` receives the applied rotation in Source degrees.
void ApplyRotationDelta(const Plugin& plugin, float yawRad, float pitchRad, float rollRad,
                        float* ang, TrackingDelta& delta) {
    delta.pitch = pitchRad * kRadToDeg * kPitchSign;
    delta.yaw   = yawRad   * kRadToDeg * kYawSign;
    delta.roll  = rollRad  * kRadToDeg * kRollSign;

    if (plugin.IsWorldSpaceYaw()) {
        // Source QAngle is intrinsically horizon-locked - yaw is about world
        // up, pitch about the yawed right axis - so adding the head delta
        // straight on IS the world-space-yaw composition.
        ang[0] += delta.pitch;
        ang[1] += delta.yaw;
        ang[2] += delta.roll;
    } else {
        source::ApplyCameraLocalRotation(ang, delta.pitch, delta.yaw, delta.roll);
    }
}

// ----- The hook -------------------------------------------------------------

// The tracking work, separated from the detour so the original call can sit
// outside the try. Nothing here is expected to throw, but the pipeline touches
// std::string and std::function, and an exception unwinding out of a __fastcall
// detour through a MinHook trampoline into client.dll frames would skip the
// original RenderView - a black screen, then terminate. Swallowing is the one
// correct answer here: a dropped frame of head tracking is nothing, a frame the
// engine never renders is everything.
void ApplyTracking(const ViewSetup& view) {
    Plugin& plugin = GetPlugin();
    // Unconditional, including in menus: it is what advances the frame clock
    // and drains the socket, so suspending it would hand the pipeline one huge
    // dt and a backlog of stale samples on the way back into gameplay.
    plugin.Update();

    const bool active = plugin.IsEnabled() && GetGameState().IsGameplayActive();

    float* org = view.Origin();
    float* ang = view.Angles();

    AimState aim;
    Copy3(aim.clean_origin, org);
    Copy3(aim.clean_angles, ang);

    TrackingDelta delta;
    if (active) {
        // The FOV is not gated on tracker data - it is a view setting, not a
        // pose, and a player whose tracker is asleep still wants the frame they
        // configured. It IS gated on the tracking toggle, so End leaves a
        // completely vanilla view behind rather than a vanilla view at a
        // modded FOV.
        const Config& config = plugin.GetConfig();
        ApplyFovOverride(view, config.fov_override, config.fov_viewmodel_override);

        float yawRad, pitchRad, rollRad;
        if (plugin.GetRotationRadians(yawRad, pitchRad, rollRad)) {
            delta.applied = true;
            // Position first: it reads the clean angles, which the rotation
            // below overwrites in place.
            ApplyPositionalLean(plugin, aim.clean_angles, org, delta);
            ApplyRotationDelta(plugin, yawRad, pitchRad, rollRad, ang, delta);
        }
    }

    // Published unconditionally, including the untracked case: a stale state
    // left behind after tracking stops would hold the crosshair off-centre with
    // nothing moving the view any more.
    aim.applied = delta.applied;
    Copy3(aim.render_origin, org);
    Copy3(aim.render_angles, ang);
    PublishAimState(aim);

    DiagnosticLog(view, delta);
}

void __fastcall Hook_RenderView(void* ecx, void* edx, void* view, int clearFlags,
                                int whatToDraw) {
    if (view) {
        try {
            ApplyTracking(ViewSetup(view, g_profile->offsets.view_setup));
        } catch (...) {
            // Deliberately silent: logging from here could throw again, and the
            // only thing that matters is reaching the original call below.
        }
    }

    g_originalRenderView(ecx, edx, view, clearFlags, whatToDraw);
}

// ----- Installation ---------------------------------------------------------

// client.dll is loaded long after the ASI, so the bootstrap thread waits for it
// rather than giving up on the first miss.
constexpr int   kClientWaitAttempts   = 200;
constexpr DWORD kClientWaitIntervalMs = 100;

HMODULE WaitForClientModule() {
    for (int i = 0; i < kClientWaitAttempts; ++i) {
        if (HMODULE client = GetModuleHandleA("client.dll")) return client;
        Sleep(kClientWaitIntervalMs);
    }
    return nullptr;
}

// Fingerprints the running client.dll and returns its profile, or nullptr -
// which is the dormant path: the game runs vanilla and the log says why.
const builds::BuildProfile* ResolveBuildProfile(HMODULE client) {
    cameraunlock::memory::PeFingerprint fp{};
    if (!cameraunlock::memory::ReadPeFingerprint(client, fp)) {
        HT_LOG("[hook] could not read client.dll fingerprint");
        return nullptr;
    }
    HT_LOG("[hook] client.dll fingerprint TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X",
           fp.TimeDateStamp, fp.SizeOfImage, fp.CheckSum);

    const builds::BuildProfile* profile = builds::MatchProfile(fp);
    if (!profile) {
        builds::LogUnrecognisedBuild(fp);
        return nullptr;
    }
    if (!profile->IsComplete()) {
        HT_LOG("[hook] build profile '%s' is a placeholder (hook target not yet rederived) "
               "- staying dormant", profile->name);
        return nullptr;
    }
    HT_LOG("[hook] matched build profile '%s'", profile->name);
    return profile;
}

// This is the mod's first hook, so it is where MinHook itself is brought up.
bool InstallRenderViewDetour(void* target) {
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;

    if (HookManager::Instance().Initialize() != HookStatus::Ok) {
        HT_LOG("[hook] MinHook init failed");
        return false;
    }
    return InstallDetour("hook", "RenderView", target,
                         reinterpret_cast<void*>(&Hook_RenderView),
                         reinterpret_cast<void**>(&g_originalRenderView));
}

}  // namespace

bool CameraHook::Install() {
    HMODULE client = WaitForClientModule();
    if (!client) {
        HT_LOG("[hook] client.dll never loaded");
        return false;
    }

    // Published before the detour is armed: the very first RenderView can land
    // inside EnableHook, and it dereferences this.
    g_profile = ResolveBuildProfile(client);
    if (!g_profile) return false;

    // Before the detour too, and fatal if it fails: the gate is what keeps the
    // pose out of the menu backdrop and out of a multiplayer session, so a hook
    // installed without one is worse than no hook at all.
    if (!GetGameState().Resolve()) return false;

    ResolveFovConVars(client, *g_profile);

    void* target = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(client)
                                           + g_profile->offsets.render_view_rva);
    return InstallRenderViewDetour(target);
}

}  // namespace headtracking
