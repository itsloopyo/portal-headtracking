// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

namespace headtracking {

// Where on screen the crosshair has to sit for it to mark the place the shot
// lands, in the head-tracked frame about to be painted.
//
// The mod never touches the player's own eye position or eye angles, so a
// Portal bullet always leaves the clean camera whatever the head is doing:
// two shots taken at opposite leans go through one hole. What moves is the eye
// the FRAME is drawn from, and that is what takes the crosshair off the target -
// screen centre stops being the aim the moment the two eyes differ.
//
// Correcting it needs a point, not a direction. The point is where the clean aim
// ray stops, so the mod traces for it with MASK_SHOT along the clean aim, on the
// frame that consumes the answer, unsmoothed - the reticle is glued to a
// surface, and when the aim crosses an edge the surface genuinely jumps. Then it
// projects that point through the engine's own world-to-screen matrix for the
// frame being drawn.
//
// Every step is the game's own: trace, projection and viewport scaling are the
// three calls GetDrawPosition itself makes on its VR branch, with the clean
// camera substituted for the rendered one. Reusing them is what keeps the
// reticle on the camera the frame was drawn with, the [View] Fov override
// included - the world-to-screen matrix is built from the same CViewSetup the
// override is written into, so nothing here has to know the FOV at all.
//
// A trace that hits nothing is a target at infinity, which is exactly what the
// far end of the ray projects to, so that case needs no special handling and no
// invented distance. A trace that cannot be read at all disables the correction
// for the session and says so - see below.

// Resolves the client.dll functions and trace_t offsets this needs from the
// active build profile. False leaves the crosshair vanilla-centred.
//
// It does not run a trace, so it cannot prove the trace_t offsets fit: that is
// checked on the first real trace instead, by requiring the result to report a
// point on the ray it was given. A profile whose offsets do not fit fails that
// check, which latches the correction off for the session and logs why.
bool ResolveAimPoint();

// Screen position in pixels for the frame currently being painted, with the
// viewport's top-left as the origin. `behindCamera` says the aim point is not in
// front of the rendered view, which a large head turn reaches; the caller draws
// nothing rather than drawing the crosshair somewhere that lies.
//
// `offsetAngles` is the weapon's own crosshair offset (a QAngle in degrees, the
// argument the caller was handed). It is added to the CLEAN aim rather than to
// the rendered view, so a weapon that corrects its own aim keeps that
// correction measured from the camera the shot actually leaves.
bool ComputeReticlePosition(const float offsetAngles[3], float& x, float& y,
                            bool& behindCamera);

bool ComputeReticleOffsetFromCentre(float& dx, float& dy, bool& behindCamera);

}  // namespace headtracking
