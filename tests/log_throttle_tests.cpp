// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
// Characterization tests for the render-path log schedule (src/log_throttle.h).
//
// These lock the schedule the two diagnostics shipped with, frame for frame.
// The render hook calls ShouldLog() every frame the game draws, so a schedule
// that drifts either buries the install-time lines a bug report is triaged from
// or puts the render thread in the log mutex far more often than intended -
// neither of which shows up as anything but a slower game and a worse log.

#include <cstdio>
#include <vector>

#include "log_throttle.h"

namespace {

using headtracking::LogThrottle;

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::printf("  [PASS] %s\n", name);
    } else {
        std::printf("  [FAIL] %s\n", name);
        ++g_failures;
    }
}

// The 1-based frame numbers a throttle emits a line on over `frames` calls.
std::vector<int> LoggedFrames(LogThrottle& throttle, int frames) {
    std::vector<int> logged;
    for (int frame = 1; frame <= frames; ++frame) {
        if (throttle.ShouldLog()) logged.push_back(frame);
    }
    return logged;
}

void TestRenderViewSchedule() {
    std::printf("render-view diagnostic schedule (6, 30, 200, 2000)\n");

    // camera_hook.cpp: six opening lines, then every 200th frame until 30 lines
    // have gone out, then every 2000th for the rest of the session.
    LogThrottle throttle(6, 30, 200, 2000);
    const std::vector<int> logged = LoggedFrames(throttle, 12000);

    std::vector<int> expected = { 1, 2, 3, 4, 5, 6 };
    for (int frame = 200; frame <= 4800; frame += 200) expected.push_back(frame);
    for (int frame = 6000; frame <= 12000; frame += 2000) expected.push_back(frame);

    Check(logged == expected, "burst, then 200-frame early tier, then 2000-frame steady tier");
    Check(logged.size() == expected.size() && logged.size() == 6 + 24 + 4,
          "30 lines cover the early tier before it thins out");
}

void TestAimSchedule() {
    std::printf("aim diagnostic schedule (4, 20, 600, 2000)\n");

    // aim_point.cpp: four opening lines, then every 600th frame until 20 lines
    // have gone out, then every 2000th for the rest of the session.
    LogThrottle throttle(4, 20, 600, 2000);
    const std::vector<int> logged = LoggedFrames(throttle, 20000);

    std::vector<int> expected = { 1, 2, 3, 4 };
    for (int frame = 600; frame <= 9600; frame += 600) expected.push_back(frame);
    for (int frame = 10000; frame <= 20000; frame += 2000) expected.push_back(frame);

    Check(logged == expected, "burst, then 600-frame early tier, then 2000-frame steady tier");
    Check(logged.size() == expected.size() && logged.size() == 4 + 16 + 6,
          "20 lines cover the early tier before it thins out");
}

void TestFrameCountingIsIndependentOfLineCounting() {
    std::printf("frame and line counters\n");

    // The interval is measured in frames from process start, not from the last
    // line, so a burst that ends mid-interval does not shift the grid. With a
    // burst of 3 and an interval of 10 the first interval line is frame 10, not
    // frame 13.
    LogThrottle throttle(3, 3, 10, 10);
    const std::vector<int> logged = LoggedFrames(throttle, 30);
    const std::vector<int> expected = { 1, 2, 3, 10, 20, 30 };
    Check(logged == expected, "intervals stay on a fixed frame grid across the burst");
}

}  // namespace

int RunLogThrottleTests() {
    std::printf("\nLog throttle\n============\n");
    TestRenderViewSchedule();
    TestAimSchedule();
    TestFrameCountingIsIndependentOfLineCounting();
    return g_failures;
}
