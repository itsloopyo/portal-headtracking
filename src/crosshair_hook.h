// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

namespace headtracking {

// Puts the crosshair back on the point the player is actually shooting at.
//
// Shots already fly along the clean mouse aim - the render-view hook never
// touches the player's own eye angles - but the crosshair is drawn from
// CurrentViewAngles(), which RenderView seeds from the CViewSetup we mutate.
// So the vanilla crosshair follows the head and stops marking the shot the
// moment the two cameras differ.
//
// CHudCrosshair::GetDrawPosition supplies the shared crosshair position.
// CHudPortalCrosshair and CHUDQuickInfo instead centre their textures inside
// Paint; their DrawSelf calls receive the same aim-point displacement.
// The shared position hook runs the original, then
// overwrites the answer with the aim point aim_point.cpp resolves: a MASK_SHOT
// trace along the CLEAN aim, projected through the engine's own world-to-screen
// matrix for the frame being drawn. Reusing the engine's projection is what
// keeps the reticle glued to the camera the frame was rendered with - including
// the [View] Fov override, since that is written into the same CViewSetup the
// matrix is built from.
//
// Installed only on a build profile carrying the aim addresses; without them
// the game keeps its vanilla centred crosshair and head tracking is unaffected.
class CrosshairHook {
public:
    CrosshairHook() = default;

    bool Install();
};

}  // namespace headtracking
