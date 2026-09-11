// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#include "game_state.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "builds/build_registry.h"
#include "debug_log.h"

namespace headtracking {

namespace {

// Calls one slot of a Source interface. The slot numbers are pinned per build
// profile and belong to the interface VERSION named there, which is why
// Resolve() proves the running client is on that version before any of this
// runs.
template <typename R, typename... Args>
R CallSlot(void* iface, unsigned slot, Args... args) {
    using Fn = R(__thiscall*)(void*, Args...);
    void** vtable = *reinterpret_cast<void***>(iface);
    return reinterpret_cast<Fn>(vtable[slot])(iface, args...);
}

using CreateInterfaceFn = void* (*)(const char* name, int* returnCode);

// How long Resolve() will wait for client.dll to be handed its engine
// interface. Generous: it covers a cold start off a slow disk, and the wait
// runs on the bootstrap thread where it costs the game nothing.
constexpr int   kConnectWaitAttempts   = 600;
constexpr DWORD kConnectWaitIntervalMs = 100;

// The animated maps the main menu runs behind itself. GetLevelName reports them
// as "maps/background03.bsp", so the substring is enough and it does not depend
// on how many of them a given install ships.
bool IsMenuBackgroundLevel(const char* level) {
    return level != nullptr && std::strstr(level, "background") != nullptr;
}

}  // namespace

GameState& GetGameState() {
    static GameState instance;
    return instance;
}

bool GameState::Resolve() {
    const builds::BuildProfile* profile = builds::ActiveProfile();
    if (!profile || !profile->HasEngineState()) {
        HT_LOG("[state] build profile has no gameplay gate - tracking would apply in menus, "
               "so the mod stays dormant");
        return false;
    }
    const builds::EngineStateOffsets& off = profile->offsets.engine;

    HMODULE client = GetModuleHandleA("client.dll");
    if (!client) {
        HT_LOG("[state] client.dll not loaded");
        return false;
    }
    HMODULE engine = GetModuleHandleA("engine.dll");
    if (!engine) {
        HT_LOG("[state] engine.dll not loaded");
        return false;
    }
    auto createInterface =
        reinterpret_cast<CreateInterfaceFn>(GetProcAddress(engine, "CreateInterface"));
    if (!createInterface) {
        HT_LOG("[state] engine.dll exports no CreateInterface");
        return false;
    }

    // client.dll's copy of the pointer is written when the engine connects it,
    // which is seconds after the module itself appears - the module being
    // loaded is not the same event as it being initialised. Reading once on the
    // way past finds a null and gives up on a build that is perfectly fine, so
    // wait for it the same way the camera hook waits for the module.
    void** slot = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(client)
                                           + off.engine_ptr_rva);
    void* fromClient = nullptr;
    for (int i = 0; i < kConnectWaitAttempts && fromClient == nullptr; ++i) {
        fromClient = *slot;
        if (!fromClient) Sleep(kConnectWaitIntervalMs);
    }
    if (!fromClient) {
        HT_LOG("[state] client.dll never published its %s pointer within %d seconds - the "
               "gameplay gate cannot be trusted on this build", off.interface_version,
               (kConnectWaitAttempts * kConnectWaitIntervalMs) / 1000);
        return false;
    }

    // The slot numbers are only meaningful for one interface version. Asking
    // engine.dll for that version by name and finding the very object client.dll
    // is already calling through proves both halves at once: the version is
    // still what the profile says, and the pointer is the live one.
    void* fromEngine = createInterface(off.interface_version, nullptr);
    if (!fromEngine || fromEngine != fromClient) {
        HT_LOG("[state] %s is %p but client.dll calls %p - the gameplay gate cannot be trusted "
               "on this build", off.interface_version, fromEngine, fromClient);
        return false;
    }

    m_engine = fromClient;
    m_offsets = &off;
    HT_LOG("[state] gameplay gate on %s at %p", off.interface_version, m_engine);
    return true;
}

void GameState::LogTransition(bool active, const char* why) {
    if (m_everLogged && active == m_lastActive) return;
    m_everLogged = true;
    m_lastActive = active;
    HT_LOG("[state] tracking %s (%s)", active ? "active" : "suspended", why);
}

bool GameState::IsGameplayActive() {
    if (!m_engine) return false;
    const builds::EngineStateOffsets& off = *m_offsets;

    if (!CallSlot<bool>(m_engine, off.slot_is_in_game)) {
        LogTransition(false, "not in a level");
        return false;
    }
    if (CallSlot<bool>(m_engine, off.slot_is_drawing_loading_image)) {
        LogTransition(false, "loading");
        return false;
    }
    if (CallSlot<bool>(m_engine, off.slot_is_paused)) {
        LogTransition(false, "paused");
        return false;
    }
    if (CallSlot<bool>(m_engine, off.slot_is_menu_background)) {
        LogTransition(false, "main menu background map");
        return false;
    }
    const int maxClients = CallSlot<int>(m_engine, off.slot_get_max_clients);
    if (maxClients > 1) {
        LogTransition(false, "multiplayer session - head tracking is single-player only");
        return false;
    }
    const char* level = CallSlot<const char*>(m_engine, off.slot_get_level_name);
    if (IsMenuBackgroundLevel(level)) {
        LogTransition(false, "main menu background map");
        return false;
    }

    LogTransition(true, level ? level : "in game");
    return true;
}

}  // namespace headtracking
