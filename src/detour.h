// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

namespace headtracking {

// Creates and enables one MinHook detour, logging the outcome the way both of
// the mod's hooks need it logged.
//
// MinHook itself must already be initialized - CameraHook::Install does that
// once, and every later hook is only reached because it succeeded. Initializing
// here instead would be worse than redundant: the manager answers a second
// Initialize() with ErrorAlreadyInitialized, so a caller that checked the
// result would refuse to install a hook that is perfectly installable.
//
// `tag` is the log prefix ("hook", "crosshair"), `what` names the hooked
// function, and `dormantNote` is appended to both failure lines to say what the
// user loses - empty where the caller says that itself.
bool InstallDetour(const char* tag, const char* what, void* target, void* detour,
                   void** original, const char* dormantNote = "");

}  // namespace headtracking
