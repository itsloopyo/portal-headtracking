// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#include "crosshair_hook.h"

#include <Windows.h>
#include <cmath>
#include <cstdint>

#include "aim_point.h"
#include "builds/build_registry.h"
#include "debug_log.h"
#include "detour.h"

namespace headtracking {
namespace {

// GetDrawPosition takes a QAngle by value on the x86 stack and uses caller cleanup.
using GetDrawPositionFn = void(__cdecl*)(float*, float*, bool*, float, float, float);
using PaintFn = void(__fastcall*)(void*, void*);
using DrawSelfFn = void(__fastcall*)(void*, void*, int, int, int, int, const void*);

GetDrawPositionFn g_originalGetDrawPosition = nullptr;
PaintFn g_originalPortalPaint = nullptr;
PaintFn g_originalQuickInfoPaint = nullptr;
DrawSelfFn g_originalDrawSelf = nullptr;

// Only textures drawn inside these two Paint calls belong to the reticle cluster.
bool g_shiftActive = false;
int g_shiftX = 0;
int g_shiftY = 0;

void __cdecl Hook_GetDrawPosition(float* x, float* y, bool* behindCamera,
                                  float offsetPitch, float offsetYaw, float offsetRoll) {
    g_originalGetDrawPosition(x, y, behindCamera, offsetPitch, offsetYaw, offsetRoll);
    const float offset[3] = {offsetPitch, offsetYaw, offsetRoll};
    float px = 0.0f, py = 0.0f;
    bool behind = false;
    if (!ComputeReticlePosition(offset, px, py, behind)) return;
    *x = px;
    *y = py;
    *behindCamera = behind;
}

void PaintShifted(PaintFn original, void* ecx, void* edx) {
    float dx = 0.0f, dy = 0.0f;
    bool behind = false;
    const bool shifted = ComputeReticleOffsetFromCentre(dx, dy, behind);
    if (shifted && behind) return;
    if (shifted) {
        // Projection includes the engine's half-pixel rounding bias.
        g_shiftX = static_cast<int>(std::floor(dx));
        g_shiftY = static_cast<int>(std::floor(dy));
    }
    g_shiftActive = shifted;
    original(ecx, edx);
    g_shiftActive = false;
}

void __fastcall Hook_PortalCrosshairPaint(void* ecx, void* edx) {
    PaintShifted(g_originalPortalPaint, ecx, edx);
}

void __fastcall Hook_QuickInfoPaint(void* ecx, void* edx) {
    PaintShifted(g_originalQuickInfoPaint, ecx, edx);
}

void __fastcall Hook_DrawSelf(void* ecx, void* edx, int x, int y, int w, int h,
                              const void* colour) {
    if (g_shiftActive) {
        x += g_shiftX;
        y += g_shiftY;
    }
    g_originalDrawSelf(ecx, edx, x, y, w, h, colour);
}

}  // namespace

bool CrosshairHook::Install() {
    if (!ResolveAimPoint()) return false;
    const builds::BuildProfile* profile = builds::ActiveProfile();
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
    const builds::AimOffsets& off = profile->offsets.aim;

    const bool sharedOk = InstallDetour(
        "crosshair", "GetDrawPosition", reinterpret_cast<void*>(base + off.draw_position_rva),
        reinterpret_cast<void*>(&Hook_GetDrawPosition),
        reinterpret_cast<void**>(&g_originalGetDrawPosition),
        " - shared crosshair compensation unavailable");

    if (!profile->HasCentredCrosshairElements()) {
        HT_LOG("[crosshair] build profile lacks Portal HUD addresses; Portal reticle stays centred");
        return false;
    }

    // Paint must not arm a shift until the texture hook can apply it.
    if (!InstallDetour(
            "crosshair", "CHudTexture::DrawSelf",
            reinterpret_cast<void*>(base + off.hud_texture_draw_self_rva),
            reinterpret_cast<void*>(&Hook_DrawSelf), reinterpret_cast<void**>(&g_originalDrawSelf),
            " - Portal reticle stays centred")) return false;

    const bool portalOk = InstallDetour(
        "crosshair", "CHudPortalCrosshair::Paint",
        reinterpret_cast<void*>(base + off.portal_crosshair_paint_rva),
        reinterpret_cast<void*>(&Hook_PortalCrosshairPaint),
        reinterpret_cast<void**>(&g_originalPortalPaint), " - Portal reticle stays centred");
    const bool quickInfoOk = InstallDetour(
        "crosshair", "CHUDQuickInfo::Paint",
        reinterpret_cast<void*>(base + off.quick_info_paint_rva),
        reinterpret_cast<void*>(&Hook_QuickInfoPaint),
        reinterpret_cast<void**>(&g_originalQuickInfoPaint), " - portal brackets stay centred");
    return sharedOk && portalOk && quickInfoOk;
}

}  // namespace headtracking
