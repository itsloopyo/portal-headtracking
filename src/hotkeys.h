// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

namespace headtracking::hotkeys {

// Nav-cluster defaults, per the shared convention every mod in the fleet uses.
constexpr int kVkEnd      = 0x23;  // toggle tracking
constexpr int kVkPageDown = 0x22;  // yaw mode
constexpr int kVkPageUp   = 0x21;  // tracking-mode cycle

// Ctrl+Shift chord alternatives from the Y/G/H cluster, for keyboards with no
// nav cluster.
constexpr int kVkY = 0x59;  // toggle tracking
constexpr int kVkG = 0x47;  // mode cycle
constexpr int kVkH = 0x48;  // yaw mode

// Rebinding a nav action bare onto one of these would make the letter itself a
// hotkey, so typing "portal" in the dev console would cycle the yaw mode.
// Config::LoadOrCreateDefault refuses such a rebind, which is only sound while
// this list stays the one the chords are registered from.
constexpr int kChordLetters[] = { kVkY, kVkG, kVkH };

}  // namespace headtracking::hotkeys
