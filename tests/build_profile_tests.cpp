// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
// Tests for the build-profile failsafes (src/builds/build_profile.h) and the
// shipped Steam profile (src/builds/steam_offsets.cpp).
//
// Everything here guards a DORMANCY decision. A profile that reports itself
// complete when it is not gets a detour installed against a stale RVA, and a
// trace offset that does not fit the buffer it indexes is read from past the
// end of a stack array. Neither has an in-game symptom short of a crash, and
// neither is visible in a diff of the offsets themselves.

#include <cstdio>
#include <initializer_list>

#include "builds/build_registry.h"

namespace {

using namespace headtracking::builds;

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::printf("  [PASS] %s\n", name);
    } else {
        std::printf("  [FAIL] %s\n", name);
        ++g_failures;
    }
}

// Every profile the mod ships, newest first, mirroring kKnownProfiles. A new
// entry belongs here too - these are the checks that would catch a half-filled
// profile, and a profile nobody asserts on is a profile nobody checked.
const BuildProfile* const kShippedProfiles[] = {
    &kSteamProfile_20250527,
};

void TestShippedSteamProfiles() {
    std::printf("shipped Steam profiles\n");

    for (const BuildProfile* p : kShippedProfiles) {
        std::printf("  %s\n", p->name);
        Check(p->IsComplete(), "carries a RenderView RVA");
        Check(p->HasAimOffsets(), "carries the aim addresses");
        Check(p->HasCentredCrosshairElements(), "carries Portal reticle draw addresses");
        Check(p->HasEngineState(), "carries the gameplay gate");
        Check(p->HasFovConVars(), "carries the FOV cvars");
        Check(TraceFieldsFitBuffer(p->offsets.aim),
              "its trace_t offsets are read inside the trace buffer");
    }
}

// Portal ships one client.dll, so one profile is live today - but routing is
// decided purely by fingerprint, and the registry is append-only, so the moment
// a patch adds a second entry two profiles sharing a fingerprint would not fail
// to build and would fail no check above. MatchProfile walks kKnownProfiles in
// order and returns the first hit, so the shadowed build would silently be
// hooked with the other's RVAs. Distinctness is the whole basis of the routing.
void TestProfileFingerprintsAreDistinct() {
    std::printf("fingerprint distinctness\n");

    const size_t count = sizeof(kShippedProfiles) / sizeof(kShippedProfiles[0]);
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            Check(!kShippedProfiles[i]->fingerprint.Matches(kShippedProfiles[j]->fingerprint),
                  kShippedProfiles[j]->name);
        }
    }
}

void TestIncompleteProfileStaysDormant() {
    std::printf("dormancy failsafes\n");

    // A placeholder: the fingerprint of a build that has been spotted but whose
    // hook target has not been rederived. Landing one must not arm the detour.
    BuildProfile placeholder = kSteamProfile_20250527;
    placeholder.offsets.render_view_rva = 0;
    Check(!placeholder.IsComplete(), "a zero RenderView RVA reports incomplete");

    BuildProfile noAim = kSteamProfile_20250527;
    noAim.offsets.aim.trace_line_rva = 0;
    Check(!noAim.HasAimOffsets(), "a missing aim address disables the crosshair correction");

    BuildProfile noGate = kSteamProfile_20250527;
    for (auto field : { &AimOffsets::portal_crosshair_paint_rva,
                        &AimOffsets::quick_info_paint_rva,
                        &AimOffsets::hud_texture_draw_self_rva }) {
        BuildProfile missingDraw = kSteamProfile_20250527;
        missingDraw.offsets.aim.*field = 0;
        Check(!missingDraw.HasCentredCrosshairElements(),
              "a missing HUD hook address disables Portal reticle correction");
    }

    noGate.offsets.engine.engine_ptr_rva = 0;
    Check(!noGate.HasEngineState(), "a missing engine pointer disables the gameplay gate");

    BuildProfile noFov = kSteamProfile_20250527;
    noFov.offsets.fov.fov_desired_rva = 0;
    Check(!noFov.HasFovConVars(), "a missing cvar address disables the FOV override");
}

void TestTraceOffsetsAreBoundsChecked() {
    std::printf("trace_t offset bounds\n");

    // endpos is three floats and fraction is one, so the last offset that fits
    // is the buffer size minus that field's own width.
    Check(TraceFieldFits(kTraceResultBufferSize - 12u, 12u), "the last in-range endpos fits");
    Check(!TraceFieldFits(kTraceResultBufferSize - 11u, 12u),
          "an endpos one byte past the end is rejected");
    Check(TraceFieldFits(kTraceResultBufferSize - 4u, 4u), "the last in-range fraction fits");
    Check(!TraceFieldFits(kTraceResultBufferSize, 4u),
          "an offset at the end of the buffer is rejected");

    // The check is a subtraction, not an addition, so an offset near the top of
    // the range cannot wrap past it and read as in-bounds.
    Check(!TraceFieldFits(0xFFFFFFF8u, 12u), "a wrap-around offset is rejected");

    BuildProfile overrun = kSteamProfile_20250527;
    overrun.offsets.aim.trace_endpos = kTraceResultBufferSize - 4u;
    Check(!TraceFieldsFitBuffer(overrun.offsets.aim),
          "a profile whose endpos runs past the buffer is refused");

    BuildProfile fractionOverrun = kSteamProfile_20250527;
    fractionOverrun.offsets.aim.trace_fraction = kTraceResultBufferSize + 4u;
    Check(!TraceFieldsFitBuffer(fractionOverrun.offsets.aim),
          "a profile whose fraction sits past the buffer is refused");
}

}  // namespace

int RunBuildProfileTests() {
    std::printf("\nBuild profiles\n==============\n");
    TestShippedSteamProfiles();
    TestProfileFingerprintsAreDistinct();
    TestIncompleteProfileStaysDormant();
    TestTraceOffsetsAreBoundsChecked();
    return g_failures;
}
