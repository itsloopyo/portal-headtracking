// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

#include <cstdint>

#include "cameraunlock/memory/pe_fingerprint.h"

namespace headtracking::builds {

// Byte offsets of the CViewSetup fields the render-view detour reads and
// writes. Source ships no headers, so these are rederived per build and pinned
// to that build's fingerprint - see the registry below.
struct ViewSetupOffsets {
    uint32_t origin;         // Vector origin
    uint32_t angles;         // QAngle angles (pitch, yaw, roll)
    uint32_t fov;            // float fov, horizontal degrees
    uint32_t fov_viewmodel;  // float fovViewmodel
    uint32_t rect_width;     // int width, the rendered viewport
    uint32_t rect_height;    // int height
};

// The client.dll functions the reticle needs. Every one of them is a function
// Valve's own crosshair code calls from CHudCrosshair::GetDrawPosition, in that
// order: trace along the aim, project what it hit through the rendered view's
// matrices, scale into the viewport. We reuse them rather than re-deriving a
// projection, so ours cannot disagree with the frame the engine drew.
struct AimOffsets {
    uint32_t draw_position_rva;   // CHudCrosshair::GetDrawPosition(x, y, behind, offsetAngle)
    uint32_t trace_line_rva;      // UTIL_TraceLine(start, end, mask, ignore, group, trace)
    uint32_t screen_transform_rva;  // ScreenTransform(worldPoint, ndc) - nonzero = behind
    uint32_t viewport_rva;        // GetFullscreenViewport(&width, &height)
    uint32_t local_player_rva;    // C_BasePlayer::GetLocalPlayer()
    uint32_t trace_endpos;        // byte offset of trace_t::endpos
    uint32_t trace_fraction;      // byte offset of trace_t::fraction
    uint32_t portal_crosshair_paint_rva;
    uint32_t quick_info_paint_rva;
    uint32_t hud_texture_draw_self_rva;
};

// The scratch buffer aim_point.cpp hands the engine's trace, and the bound the
// two trace_t field offsets above are read within. Wider than any trace_t this
// engine writes, so the engine's own write always fits; the OFFSETS are
// per-build data, rederived by hand for every profile, so they are checked
// against it at load rather than trusted. An out-of-range one would read past
// the end of a stack buffer, which is the one way a mistyped offset stops being
// a wrong crosshair and starts being a memory fault.
constexpr uint32_t kTraceResultBufferSize = 256;

// Written as a subtraction so an offset near the top of the range cannot wrap
// the addition and pass the check.
constexpr bool TraceFieldFits(uint32_t offset, uint32_t size) {
    return offset <= kTraceResultBufferSize && size <= kTraceResultBufferSize - offset;
}

constexpr bool TraceFieldsFitBuffer(const AimOffsets& aim) {
    return TraceFieldFits(aim.trace_endpos, static_cast<uint32_t>(sizeof(float) * 3))
        && TraceFieldFits(aim.trace_fraction, static_cast<uint32_t>(sizeof(float)));
}

// The two ConVars the game bases its own field of view on. The mod needs them
// because the INI expresses its override in fov_desired's units, so turning
// that into a render-view FOV means knowing what the game is currently
// measuring from - and because the FOV in the render view is not always the
// player's: a suit zoom or a scripted camera writes its own, and an override
// that ignored that would flatten every one of them.
//
// `convar_name` is ConCommandBase::m_pszName and is there to be checked, not
// used: a ConVar object whose name reads back as the expected string is proof
// the rest of the layout fits, and three plausible floats are not.
struct FovConVarOffsets {
    uint32_t fov_desired_rva;    // the fov_desired ConVar object in client.dll
    uint32_t viewmodel_fov_rva;  // the viewmodel_fov ConVar object
    uint32_t convar_name;        // byte offset of ConCommandBase::m_pszName
    uint32_t convar_value;       // byte offset of ConVar::m_fValue
};

// The gameplay gate. `engine_ptr_rva` is client.dll's own IVEngineClient*, so
// the mod asks the same object the game does; the slot numbers below are that
// interface's, which is why the version string travels with them. Both are
// checked at load: the pointer must agree with engine.dll's CreateInterface for
// exactly this version, or the gate stays unresolved and the mod is dormant.
struct EngineStateOffsets {
    uint32_t engine_ptr_rva;
    const char* interface_version;
    uint16_t slot_is_in_game;
    uint16_t slot_is_paused;
    uint16_t slot_is_menu_background;
    uint16_t slot_is_drawing_loading_image;
    uint16_t slot_get_max_clients;
    uint16_t slot_get_level_name;
};

// The whole surface one client.dll build pins.
struct OffsetTable {
    uint32_t render_view_rva;  // CViewRender::RenderView, RVA in client.dll
    ViewSetupOffsets view_setup;
    AimOffsets aim;
    EngineStateOffsets engine;
    FovConVarOffsets fov;
};

// One entry per shipped Portal client.dll build we have offsets for. The
// PE fingerprint is the routing key.
struct BuildProfile {
    const char* name;
    cameraunlock::memory::PeFingerprint fingerprint;
    OffsetTable offsets;

    // A profile whose hook target is still unresolved is a placeholder: the
    // fingerprint of a build we have spotted but not yet rederived. It must
    // stay dormant rather than hook a stale address, so the entry can be
    // landed the moment a patch appears without risking a user's session.
    bool IsComplete() const { return offsets.render_view_rva != 0; }

    // Reticle compensation is a separate, optional surface: a profile can drive
    // the camera without it. A build whose aim addresses have not been derived
    // keeps head tracking and draws the vanilla centred crosshair.
    bool HasAimOffsets() const {
        return offsets.aim.draw_position_rva != 0 && offsets.aim.trace_line_rva != 0 &&
               offsets.aim.screen_transform_rva != 0 && offsets.aim.viewport_rva != 0 &&
               offsets.aim.local_player_rva != 0;
    }

    bool HasCentredCrosshairElements() const {
        return offsets.aim.portal_crosshair_paint_rva != 0 &&
               offsets.aim.quick_info_paint_rva != 0 &&
               offsets.aim.hud_texture_draw_self_rva != 0;
    }

    bool HasEngineState() const {
        return offsets.engine.engine_ptr_rva != 0 && offsets.engine.interface_version != nullptr;
    }

    // Also optional, and separately so: a build whose FOV ConVars have not been
    // located still tracks the head and still draws the reticle on the shot, it
    // just leaves the [View] Fov keys inert.
    bool HasFovConVars() const {
        return offsets.fov.fov_desired_rva != 0 && offsets.fov.viewmodel_fov_rva != 0;
    }
};

}  // namespace headtracking::builds
