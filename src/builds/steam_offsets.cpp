// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
// Steam Win32 build profile for Portal's client.dll. Portal ships one campaign
// dir, so unlike Half-Life 2 - whose install carries a separate client.dll per
// campaign - there is a single profile here. Append-only: a new patch gets a
// new entry and a new line at the top of kKnownProfiles in build_registry.cpp.
// Nothing in this file is ever edited in place, because editing a shipped
// profile strands every user still on that build.
//
// Rederive by locating CViewRender::RenderView in a disassembler, and read the
// running DLL's fingerprint with `pixi run check-fingerprint`.

#include "builds/build_registry.h"

namespace headtracking::builds {

// CViewSetup is 200 bytes on this branch: RenderView copies it twice with
// `mov ecx, 0x32; rep movsd`, which is 50 dwords, and that is what fixes the
// tail and so the whole layout.
//
// The head of the struct is read straight off RenderView's own use of it. It
// pushes (+0x00, +0x08, +0x10, +0x18) together as one viewport quad in three
// separate calls, so the rect ints are doubled (x/unscaledX, y/unscaledY,
// width/unscaledWidth), and it does `cmp dword ptr [ebx+0x1C], 2`, which makes
// that field m_eStereoEye rather than the fourth unscaled int - the unscaled
// height is pushed out to +0x20. From there the declaration order runs
// m_bOrtho (padded to four), the four ortho floats, fov, fovViewmodel, origin,
// angles. `lea eax, [ebx+0x40]` handed to the area-portal call as the origin
// confirms the back half.
//
// This is the same layout Half-Life 2's 2025 builds carry, and every piece of
// evidence it rests on reproduces here, so the two agree by measurement rather
// than by assumption.
constexpr ViewSetupOffsets kViewSetupLayout_2025 = {
    0x40u,  // origin (Vector x, y, z)
    0x4Cu,  // angles (QAngle pitch, yaw, roll)
    0x38u,  // fov, horizontal degrees, already widened for this viewport
    0x3Cu,  // fovViewmodel - the float straight after fov
    0x10u,  // rect width
    0x18u,  // rect height
};

// Every function here is one CHudCrosshair::GetDrawPosition itself calls, found
// by decompiling it: its VR branch traces MASK_SHOT along the aim and projects
// the impact point, which is exactly the shape the head-tracked reticle needs.
// (That branch is gated on a live HMD, so on a normal session the vanilla
// crosshair is at hard screen centre and the mod's detour supplies the whole
// answer.)
//
// GetDrawPosition is __cdecl here, not __thiscall - it ends in a bare `ret` and
// never touches ecx - and it takes its QAngle by value, so its detour sees six
// stack arguments and no `this`. Its tail writes *pX, *pY and *pbBehindCamera
// through [ebp+8], [ebp+0x0C] and [ebp+0x10], which is what pins the argument
// order.
//
// UTIL_TraceLine is identified by the call that pushes MASK_SHOT (0x46004003)
// as its third argument, with start, end, the ignore entity, a zero collision
// group and the result buffer around it. The buffer it is handed is at
// [ebp-0xA8] and the point ScreenTransform is then given is [ebp-0x9C], twelve
// bytes on, which is trace_t::endpos; fraction follows CBaseTrace's plane at
// 44. Both are checked against the ray the mod asked for before either is
// trusted.
constexpr AimOffsets kAimLayout_20250527 = {
    0x156650u,  // CHudCrosshair::GetDrawPosition
    0x088880u,  // UTIL_TraceLine
    0x1D2F60u,  // ScreenTransform
    0x1C6600u,  // GetFullscreenViewport(&w, &h)
    0x0C55E0u,  // C_BasePlayer::GetLocalPlayer
    12u,        // trace_t::endpos
    44u,        // trace_t::fraction
    0x238700u,  // CHudPortalCrosshair::Paint
    0x22FAD0u,  // CHUDQuickInfo::Paint
    0x145380u,  // CHudTexture::DrawSelf(x, y, w, h, colour), callee pops 20 bytes
};

// The IVEngineClient* client.dll itself calls through, at client.dll+0x4E3F64:
// it is the pointer CreateInterface("VEngineClient014") writes at
// client.dll+0xF9525, and RenderView and ScreenTransform both read it.
//
// The slot numbers are this install's engine.dll, not an assumption carried
// over from another game. engine.dll registers VEngineClient014 with a factory
// returning the singleton at engine.dll+0x3EF0EC, whose vftable is at
// +0x3231C0, and each slot below was read off that table and identified by what
// the function does:
//   21 returns an int global                          - GetMaxClients
//   26 `cmp signon, 6; sete al`                       - IsInGame
//   28 reads a bool global                            - IsDrawingLoadingImage
//   51 returns the level-name buffer or a literal     - GetLevelName
//   84 tail-jumps into the paused test                - IsPaused
//   87 reads a bool global                            - IsLevelMainMenuBackground
// Slot 27 is `cmp signon, 2; setge al` (IsConnected) against the same signon
// global slot 26 compares, which is the cross-check that the indexing is right
// rather than off by a slot.
constexpr EngineStateOffsets kEngineState_20250527 = {
    0x4E3F64u,
    "VEngineClient014",
    26u,  // IsInGame
    84u,  // IsPaused
    87u,  // IsLevelMainMenuBackground
    28u,  // IsDrawingLoadingImage
    21u,  // GetMaxClients
    51u,  // GetLevelName
};

// The FOV ConVars, located from their name strings: each name is pushed as the
// first argument of its ConVar constructor with the object itself in ecx, so
// the instruction pair names the object outright. Read out of the same
// registrations, fov_desired is default "75", min 75, max 120, flags 0x280
// (FCVAR_ARCHIVE | FCVAR_USERINFO), help "Sets the base field-of-view.", and
// viewmodel_fov is default "54", flags 0x4000 (FCVAR_CHEAT) - the reason the
// mod carries its own override at all.
//
// The two field offsets are the standard ConCommandBase / ConVar layout for
// 32-bit MSVC: vtable, m_pNext, m_bRegistered, then m_pszName at 0x0C, and past
// ConVar's second vtable and its parent/default/string members to m_fValue at
// 0x2C. Confirmed at load by reading the name back off the object.
constexpr FovConVarOffsets kFovConVars_20250527 = {
    0x51ACA8u,  // fov_desired
    0x50F130u,  // viewmodel_fov
    0x0Cu,      // ConCommandBase::m_pszName
    0x2Cu,      // ConVar::m_fValue
};

// portal\bin\client.dll, TimeDateStamp 2025-05-27 21:24:25 UTC. RenderView is
// at rva 0x1E3E60, confirmed by the telemetry marker inside it, which names
// both "CViewRender::RenderView" and game\client\viewrender.cpp:2016 - the same
// source line Half-Life 2's 2025 builds give - and by its `ret 0xC`, which pops
// exactly the three stack arguments the detour declares.
extern const BuildProfile kSteamProfile_20250527 = {
    "steam-win32-20250527",
    { 0x68362D89u, 0x005D6000u, 0x00000000u },
    { 0x1E3E60u, kViewSetupLayout_2025, kAimLayout_20250527, kEngineState_20250527,
      kFovConVars_20250527 },
};

}  // namespace headtracking::builds
