// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#include "debug_log.h"

#include <string>

#include "cameraunlock/os/module_paths.h"

namespace headtracking {

// The core resolver rather than a local GetModuleFileNameW: that call truncates
// instead of failing, so a game installed under a long path would silently put
// the log somewhere else. An unresolvable directory leaves the name relative and
// the log lands in the process's working directory.
void OpenLogFile() {
    std::wstring path = cameraunlock::os::HostExeDirectory();
    if (!path.empty()) path += L'\\';
    cameraunlock::logging::Open(path + L"HeadTracking.log");
}

}  // namespace headtracking
