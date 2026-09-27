// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
// Tests for the config -> core PositionSettings mapping (src/position_mapping.h).
//
// Every assertion here covers a mapping whose failure is silent in game: the
// camera still moves along the right axis, only the travel is wrong. That is
// the one class of positional bug a playtest does not catch, and it has bitten
// this fleet more than once.

#include <cstdio>

#include "position_mapping.h"

namespace {

using headtracking::Config;
using headtracking::MakePositionSettings;

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::printf("  [PASS] %s\n", name);
    } else {
        std::printf("  [FAIL] %s\n", name);
        ++g_failures;
    }
}

void TestVerticalLimitsMapToTheirOwnBounds() {
    std::printf("PositionSettings vertical limits\n");

    // PositionLimitY and PositionLimitYDown are separate keys, and each has to reach its own
    // bound: the core struct's limit_y_down defaults independently, so a mapping that left it
    // unset would pin downward travel at 0.20m whatever the file says.
    Config c;
    c.position.limit_y = 0.40f;
    c.position.limit_y_down = 0.05f;
    const auto ps = MakePositionSettings(c);
    Check(ps.limit_y == 0.40f, "PositionLimitY is the upward bound");
    Check(ps.limit_y_down == 0.05f, "PositionLimitYDown is the downward bound");
}

void TestForwardLeanKeepsTheGenerousBound() {
    std::printf("PositionSettings Z asymmetry\n");

    // Z is the axis that stays asymmetric on purpose: the processor clamps to
    // [-limit_z, +limit_z_back], so the generous allowance has to sit on
    // limit_z (leaning in) and the tight one on limit_z_back (pulling back).
    const auto ps = MakePositionSettings(Config{});
    Check(ps.limit_z == 0.40f, "PositionLimitZ maps to the forward bound");
    Check(ps.limit_z_back == 0.10f, "PositionLimitZBack maps to the backward bound");
    Check(ps.limit_z > ps.limit_z_back, "forward lean keeps the generous allowance");
}

void TestPoseReachesTheProcessorUnshaped() {
    std::printf("PositionSettings sensitivity and inversion\n");

    // The tracker shapes the pose. The axis signs are the camera hook's, applied after the
    // asymmetric Z clamp; inverting in the processor would happen before it, which swaps the
    // 0.40m forward allowance onto the backward lean - direction fixed, travel quietly broken.
    Config c;
    c.position.sensitivity_x = 2.0f;
    c.position.invert_z = true;
    const auto ps = MakePositionSettings(c);
    Check(ps.sensitivity_x == 1.0f && ps.sensitivity_y == 1.0f && ps.sensitivity_z == 1.0f,
          "the processor runs at identity sensitivity");
    Check(!ps.invert_x && !ps.invert_y && !ps.invert_z, "the processor inverts no axis");
}

void TestLimitsMapStraightThrough() {
    std::printf("PositionSettings limits\n");

    Config c;
    c.position.limit_x = 0.11f;
    c.position.limit_z = 0.22f;
    c.position.limit_z_back = 0.33f;
    const auto ps = MakePositionSettings(c);
    Check(ps.limit_x == 0.11f && ps.limit_z == 0.22f && ps.limit_z_back == 0.33f,
          "per-axis limits map straight through");
}

}  // namespace

int RunPositionMappingTests() {
    std::printf("\nPosition mapping\n================\n");
    TestVerticalLimitsMapToTheirOwnBounds();
    TestForwardLeanKeepsTheGenerousBound();
    TestPoseReachesTheProcessorUnshaped();
    TestLimitsMapStraightThrough();
    return g_failures;
}
