// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

namespace headtracking {

// The bridge between the render-view detour and the crosshair detour.
// RenderView writes the head pose into the CViewSetup the frame is drawn from;
// the crosshair is painted later in the same frame and has no view of its own to
// read, so it needs both cameras: the one the game aims along and the one the
// frame is actually rendered with.
//
// It needs the ORIGINS as well as the angles. A rotation-only reticle can be
// placed from the angles alone, because a point and a direction project to the
// same pixel while the two eyes coincide. A 6DOF lean separates them - the frame
// is drawn from an eye up to 30cm away from the one the shot leaves - and from
// then on the reticle marks a point in the world, at a distance only a trace can
// give. See aim_point.cpp.
//
// Render-thread only, and deliberately unsynchronised: the HUD paint that
// consumes this happens in the same frame, on the same thread, as the RenderView
// call that publishes it. An atomic here would buy a fence on the hot path and
// nothing else.
struct AimState {
    bool  applied = false;                  // tracking modified this frame's view
    float clean_origin[3]  = {0.0f, 0.0f, 0.0f};  // eye the game shoots from
    float clean_angles[3]  = {0.0f, 0.0f, 0.0f};  // QAngle the game aims along
    float render_origin[3] = {0.0f, 0.0f, 0.0f};  // eye the frame is drawn from
    float render_angles[3] = {0.0f, 0.0f, 0.0f};  // QAngle the frame renders with
};

// Called once per frame by the render-view detour, before the original call.
void PublishAimState(const AimState& state);

// The state the frame currently being painted was rendered with.
const AimState& CurrentAimState();

}  // namespace headtracking
