// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

#include "cameraunlock/logging/file_log.h"

namespace headtracking {

// Opens HeadTracking.log next to the game EXE. The core truncates it on open and
// keeps the launch before it as HeadTracking.prev.log, so a session never appends
// to an older one and the pair is all the mod ever writes. Called once from the
// bootstrap thread before the first HT_LOG so the loader-presence line is
// captured, and only when [Debug] LogToFile is on - the caller checks first, so
// LogToFile=0 leaves no file of either generation behind.
void OpenLogFile();

}  // namespace headtracking

#define HT_LOG(...) ::cameraunlock::logging::Line(__VA_ARGS__)
