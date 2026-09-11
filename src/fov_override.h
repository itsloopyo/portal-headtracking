// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

#include <Windows.h>

#include "builds/build_profile.h"
#include "view_setup.h"

namespace headtracking {

// The [View] Fov / FovViewmodel override, applied to the render view the frame
// is about to be built from.
//
// Separate from the head pose on purpose: it is a view SETTING, not a pose, so
// it neither needs tracker data nor has anything to do with the tracker->Source
// axis mapping the camera hook owns. The only thing the two share is the
// CViewSetup they both write.

// Reads the running client.dll's fov_desired and viewmodel_fov ConVars, which
// the override is expressed relative to. Called once, after the build profile
// has matched. A profile without the ConVars, or a pair that does not read
// back, leaves the override off and the rest of the mod running.
void ResolveFovConVars(HMODULE client, const builds::BuildProfile& profile);

// Scales the render view's two FOVs by the ratio the config asks for. A zero
// override for a field leaves that field as the game rendered it.
void ApplyFovOverride(const ViewSetup& view, float worldFov, float viewmodelFov);

}  // namespace headtracking
