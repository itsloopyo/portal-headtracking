// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
//
// CViewSetup::fov is the horizontal FOV, in degrees, the frame is actually
// rendered with, and it is the mod's answer to "what is the camera's FOV".
// CViewRender::SetUpView has already taken the player's fov_desired - which
// Source defines against a 4:3 screen - and widened it for the real viewport by
// the time RenderView sees the struct, and it has already applied whatever the
// game itself is doing to the FOV that frame (a zoomed weapon, a scripted
// sequence, the suit zoom). So this one float is the live value, not a setting.
//
// It is also the right place to CHANGE it. Everything the frame is built from
// comes out of this struct, including the world-to-screen matrix the crosshair
// projection goes through, so an override written here needs nothing kept in
// sync with it: widen the FOV and the reticle lands on the same world point it
// did before, because both come from the same matrix (see crosshair_hook.cpp).
//
// The game's own knobs are the fov_desired and viewmodel_fov cvars. Read out of
// this client.dll's ConVar registrations: fov_desired is default 75, min 75,
// max 120, flags 0x280 (FCVAR_ARCHIVE | FCVAR_USERINFO), and viewmodel_fov is
// default 54, flags 0x4000 (FCVAR_CHEAT). So the world FOV is adjustable in
// game but only from 75 to 120, and it rides along in the player's userinfo;
// the viewmodel FOV is not adjustable at all without sv_cheats. A write into
// the render view has none of those three properties, which is what this
// override buys.
//
// The override is a RATIO against those cvars, not a value written over the top
// of whatever the frame happens to hold. CViewSetup::fov is not always the
// player's FOV: a suit zoom, a weapon zoom and a scripted camera each write
// their own, and the trainstation opening alone sweeps it from 6 degrees up to
// the player's. Writing the configured number in unconditionally would flatten
// every one of them, so the zoom key would visibly do nothing. Scaling by
// (configured / cvar) instead shows exactly the configured number on a frame at
// the player's own FOV, scales a zoom by the same factor as everything else,
// and is continuous through the transition, so there is no frame where the
// override snaps in or out.
//
// The scaling happens in the cvars' own 4:3 reference rather than on the
// rendered value: the widening is a tangent scaling, so a ratio applied to the
// widened number is not the ratio the player asked for.

#include "fov_override.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "debug_log.h"
#include "source_math.h"

namespace headtracking {

namespace {

// At and past this the projection degenerates - tan(fov/2) runs away - so a
// frame that scales into it is left as the game rendered it.
constexpr float kMaxRenderableFov = 179.0f;

// The live values of the two cvars, read every frame rather than latched: the
// player can change fov_desired from the console mid-session, and the override
// is defined relative to it.
const float* g_fovDesired = nullptr;
const float* g_viewmodelFov = nullptr;

// A ConVar object whose name reads back as the expected string proves the rest
// of the layout fits. Three plausible-looking floats prove nothing, which is
// exactly the failure this check exists to catch.
const float* ResolveConVar(HMODULE client, const builds::FovConVarOffsets& off, uint32_t rva,
                           const char* expectedName) {
    const uintptr_t object = reinterpret_cast<uintptr_t>(client) + rva;
    const char* name = *reinterpret_cast<const char* const*>(object + off.convar_name);
    if (!name || std::strcmp(name, expectedName) != 0) {
        HT_LOG("[view] client.dll+0x%X does not read as the %s ConVar (its name is '%s') - the "
               "[View] Fov keys are inert this session; head tracking is unaffected",
               rva, expectedName, name ? name : "(null)");
        return nullptr;
    }
    return reinterpret_cast<const float*>(object + off.convar_value);
}

// Per-field diagnostic state. Owned by the caller so the two FOV fields report
// independently: `logged_factor` fires once per factor - when the player edits
// the cvar, not on every frame of a zoom - and `refusal_logged` once per field.
// One shared latch let a refusal on the world FOV silence the viewmodel one for
// the whole session.
struct FieldLog {
    float logged_factor = 0.0f;
    bool refusal_logged = false;
};

// Scales one of the view's FOV fields.
void ScaleField(float& target, float desired, float base, float ratio, int w, int h,
                const char* what, FieldLog& log) {
    if (desired <= 0.0f) return;
    // The game renders a zero FOV on the frames where the field is not in
    // use - the viewmodel one, during a scripted camera. There is nothing
    // to scale, and a ratio against a zero cvar has no meaning either.
    if (!(base > 0.0f) || !std::isfinite(target) || target <= 0.0f) return;

    const float factor = desired / base;
    const float rendered = source::ScaleFovByWidthRatio(
        source::UnscaleFovByWidthRatio(target, ratio) * factor, ratio);
    if (!std::isfinite(rendered) || rendered <= 0.0f || rendered >= kMaxRenderableFov) {
        // This frame only. The game is rendering something wide enough that
        // the player's factor takes it past having a projection at all - a
        // cinematic, not a broken offset - so the honest answer is the frame
        // the game built, not an override disabled for the rest of the run.
        if (!log.refusal_logged) {
            log.refusal_logged = true;
            HT_LOG("[view] %s FOV left alone on a frame the game rendered at %.2f: scaling "
                   "it by %.3f gives %.2f degrees, which has no projection",
                   what, target, factor, rendered);
        }
        return;
    }
    if (log.logged_factor != factor) {
        log.logged_factor = factor;
        HT_LOG("[view] %s FOV override: %.1f over the game's %.1f = x%.3f, so a normal "
               "frame renders at %.2f in a %dx%d viewport", what, desired, base, factor,
               source::ScaleFovByWidthRatio(desired, ratio), w, h);
    }
    target = rendered;
}

}  // namespace

void ResolveFovConVars(HMODULE client, const builds::BuildProfile& profile) {
    if (!profile.HasFovConVars()) {
        HT_LOG("[view] build profile has no FOV cvar addresses - the [View] Fov keys are inert "
               "(head tracking is unaffected)");
        return;
    }
    const builds::FovConVarOffsets& off = profile.offsets.fov;
    const float* world = ResolveConVar(client, off, off.fov_desired_rva, "fov_desired");
    const float* viewmodel = ResolveConVar(client, off, off.viewmodel_fov_rva, "viewmodel_fov");
    if (!world || !viewmodel) return;

    g_fovDesired = world;
    g_viewmodelFov = viewmodel;
    HT_LOG("[view] FOV cvars resolved: fov_desired=%.1f viewmodel_fov=%.1f", *world, *viewmodel);
}

// A viewport that does not read as a rect disables the override for the rest of
// the session rather than being retried every frame: it means the profile's
// CViewSetup offsets do not fit this client.dll, and a projection built from
// the NaN that follows is a black screen, not a cosmetic fault.
void ApplyFovOverride(const ViewSetup& view, float worldFov, float viewmodelFov) {
    static bool s_disabled = false;
    if (s_disabled || !g_fovDesired) return;
    if (worldFov <= 0.0f && viewmodelFov <= 0.0f) return;

    const int w = view.RectWidth();
    const int h = view.RectHeight();
    if (w <= 0 || h <= 0) {
        s_disabled = true;
        HT_LOG("[view] FOV override disabled: viewport reads as %dx%d - the build profile's "
               "CViewSetup offsets do not fit this client.dll", w, h);
        return;
    }
    const float ratio = (static_cast<float>(w) / static_cast<float>(h))
                        * source::kReferenceAspectInverse;

    static FieldLog s_world;
    static FieldLog s_viewmodel;
    ScaleField(view.Fov(), worldFov, *g_fovDesired, ratio, w, h, "world", s_world);
    ScaleField(view.FovViewmodel(), viewmodelFov, *g_viewmodelFov, ratio, w, h, "viewmodel",
               s_viewmodel);
}

}  // namespace headtracking
