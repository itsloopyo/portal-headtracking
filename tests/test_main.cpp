// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#include <cstdio>

int RunSourceMathTests();
int RunPositionMappingTests();
int RunLogThrottleTests();
int RunBuildProfileTests();

int main() {
    std::printf("PortalHeadTracking tests\n========================\n");
    const int failures =
        RunSourceMathTests() + RunPositionMappingTests() + RunLogThrottleTests() +
        RunBuildProfileTests();
    if (failures == 0) {
        std::printf("\nAll tests passed\n");
        return 0;
    }
    std::printf("\n%d test(s) FAILED\n", failures);
    return 1;
}
