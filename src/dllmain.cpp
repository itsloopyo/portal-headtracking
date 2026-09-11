// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#include "pch.h"

#include <exception>

#include "plugin.h"
#include "config.h"
#include "debug_log.h"
#include "version.h"

namespace {

// This is the mod's outermost frame. An exception leaving a thread entry point
// is std::terminate, which kills the game the mod was supposed to be a guest
// in - so the whole of setup sits inside the guard, and a failure leaves a
// dormant mod and a log line behind instead. Nothing below is expected to
// throw (the config path validates at the boundary and no longer calls the
// throwing filesystem overloads), but it allocates, and a mod is never worth a
// dead process.
DWORD WINAPI BootstrapThread(LPVOID) {
    try {
        if (headtracking::Config::FileLoggingRequested()) headtracking::OpenLogFile();
        HT_LOG("[main] PortalHeadTracking %s loaded into pid %lu",
               HEADTRACKING_VERSION_STRING, GetCurrentProcessId());

        headtracking::GetPlugin().Initialize();
    } catch (const std::exception& e) {
        HT_LOG("[main] startup failed (%s) - the mod is dormant, the game is unaffected",
               e.what());
    } catch (...) {
        HT_LOG("[main] startup failed - the mod is dormant, the game is unaffected");
    }
    return 0;
}

// Takes a permanent reference on this module, so an unload cannot pull the code
// out from under the bootstrap thread or the installed hooks.
void PinSelf() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       reinterpret_cast<LPCWSTR>(&BootstrapThread), &self);
}

}  // namespace

// The third parameter is deliberately unread - see the detach case below for
// what it would say and why nothing acts on it either way.
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            // Pin the module before starting the thread that outlives DllMain.
            // Something in the game's startup FreeLibrary's us - the detach
            // arrives with reserved == nullptr (an explicit unload, not process
            // exit) while the bootstrap thread is still in Config::Load, and the
            // thread then runs on in freed memory. That is a crash in
            // "PortalHeadTracking.asi_unloaded", usually
            // STATUS_INVALID_EXCEPTION_HANDLER, and it takes the game with it.
            //
            // Pinning makes FreeLibrary a no-op for us, which is the right
            // lifetime anyway: the bootstrap thread runs for the whole session,
            // and the render hook is never uninstalled (see camera_hook.h) - so
            // this code must stay mapped for as long as the process lives.
            PinSelf();
            // The handle is closed straight away; the thread runs on. Nothing
            // ever joins it (see the detach case), so holding the handle would
            // only keep a kernel object alive for the life of the process.
            if (HANDLE thread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr)) {
                CloseHandle(thread);
            }
            break;

        case DLL_PROCESS_DETACH:
            // Nothing. Not on process exit (reserved != nullptr): the OS has
            // already killed our worker threads, possibly mid-syscall or
            // holding the log mutex, so joining or unhooking then can hang the
            // game on the way out. And not on a FreeLibrary unload either:
            // DllMain runs under the loader lock, and Shutdown() joins the
            // hotkey and receiver threads whose own exit path (LdrShutdownThread
            // running every other DLL's DLL_THREAD_DETACH) needs that same
            // lock - a textbook deadlock. An ASI plugin is never unloaded in
            // practice; the process teardown reclaims everything.
            break;
    }
    return TRUE;
}
