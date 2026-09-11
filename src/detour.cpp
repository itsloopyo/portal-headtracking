// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#include "detour.h"

#include "cameraunlock/hooks/hook_manager.h"
#include "debug_log.h"

namespace headtracking {

bool InstallDetour(const char* tag, const char* what, void* target, void* detour,
                   void** original, const char* dormantNote) {
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;

    const HookStatus created = HookManager::Instance().CreateHook(target, detour, original);
    if (created != HookStatus::Ok) {
        HT_LOG("[%s] CreateHook(%s) failed: %s%s", tag, what,
               cameraunlock::hooks::HookStatusToString(created), dormantNote);
        return false;
    }
    if (HookManager::Instance().EnableHook(target) != HookStatus::Ok) {
        HT_LOG("[%s] EnableHook(%s) failed%s", tag, what, dormantNote);
        return false;
    }
    HT_LOG("[%s] %s hook installed at %p", tag, what, target);
    return true;
}

}  // namespace headtracking
