#pragma once

// The mod's canonical version. scripts/ModVersion.psm1 owns the read and the
// write; release.ps1 mirrors the value into scripts/install.cmd and
// CMakeLists.txt, and .github/workflows/release.yml re-reads it to check the
// pushed tag agrees. Bump it with `pixi run release`, not by hand.
#define HEADTRACKING_VERSION_MAJOR 0
#define HEADTRACKING_VERSION_MINOR 0
#define HEADTRACKING_VERSION_PATCH 0
#define HEADTRACKING_VERSION_STRING "0.0.0"
