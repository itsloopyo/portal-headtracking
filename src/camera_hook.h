// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

namespace headtracking {

// Installs a MinHook detour on the Source client's render-view function
// (client.dll). The detour injects the head pose into the CViewSetup the
// renderer consumes - the render view origin and angles only.
//
// What stays clean is the player's own state: the eye position and eye angles
// weapons fire from, which the mod never touches, so bullets, traces and physics
// are identical whether tracking is on or off. That is what decouples look from
// aim. Render-phase globals that RenderView itself seeds from this struct
// (CurrentViewOrigin/CurrentViewAngles and the world-to-screen matrix) DO see
// the tracked pose - they are the render view, so sprites, beams and audio
// panning following the head is the correct behaviour, not a leak. The crosshair
// reads those same globals, which is why it needs the correction in
// crosshair_hook.cpp.
//
// The hook is gated on a PE-fingerprint build-profile registry: it engages only
// on a Portal client.dll build it has offsets for, and stays dormant (game
// runs vanilla) on any other build. See builds/build_registry.h.
//
// Once installed it is never removed. There is no Uninstall: unhooking means
// freeing a trampoline a render thread may be about to jump into, and the only
// caller would be a DLL_PROCESS_DETACH path that must not do work anyway (see
// dllmain.cpp). The process teardown reclaims it.
class CameraHook {
public:
    CameraHook() = default;

    bool Install();
};

}  // namespace headtracking
