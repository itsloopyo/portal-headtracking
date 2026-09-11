# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo / CameraUnlock
# Characterization tests for the mod's pure logic. Deliberately links nothing
# from the game or the hooks: anything that needs a live client.dll is verified
# in-game, and anything that does not belongs here.
add_executable(portal_headtracking_tests
    test_main.cpp
    source_math_tests.cpp
    position_mapping_tests.cpp
    log_throttle_tests.cpp
    build_profile_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/source_math.cpp
    ${CMAKE_SOURCE_DIR}/src/builds/steam_offsets.cpp
)

# The core include dir is for header-only types only (PositionSettings, and
# PeFingerprint for the build profiles), so the suite still links nothing from
# the game or the hooks.
target_include_directories(portal_headtracking_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp/include
)

if(MSVC)
    target_compile_options(portal_headtracking_tests PRIVATE /W4 /WX)
else()
    target_compile_options(portal_headtracking_tests PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME portal_headtracking_tests COMMAND portal_headtracking_tests)
