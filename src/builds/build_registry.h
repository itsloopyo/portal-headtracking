// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

#include "builds/build_profile.h"

namespace headtracking::builds {

// Append-only: never edit a shipped profile's offsets in place (that strands
// every user still on that build) - add a new entry and put it on top of
// kKnownProfiles in build_registry.cpp. Definitions live in steam_offsets.cpp.
extern const BuildProfile kSteamProfile_20250527;

// The profile whose fingerprint matches this client.dll, or nullptr.
const BuildProfile* MatchProfile(const cameraunlock::memory::PeFingerprint& fp);

// The profile MatchProfile last matched, for the hooks that install after the
// camera hook has already resolved the build. nullptr until then, and on an
// unrecognised build it stays nullptr - which is what keeps every later hook
// dormant too.
const BuildProfile* ActiveProfile();

// Dormant-path diagnostic for a build no profile matches: which direction the
// running build differs in, plus every profile it was compared against, so a
// user's report needs no follow-up round trip.
void LogUnrecognisedBuild(const cameraunlock::memory::PeFingerprint& fp);

}  // namespace headtracking::builds
