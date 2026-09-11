// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

#include <cstdint>

#include "builds/build_profile.h"

namespace headtracking {

// Typed access to the CViewSetup a hooked render call is about to consume.
// Source ships no header for the struct, so every field is reached through the
// running build's profile offsets rather than a declaration - which is exactly
// why the offsets travel with the view instead of sitting in a global: a
// mismatched pair writes floats into the middle of an unrelated field.
class ViewSetup {
public:
    ViewSetup(void* view, const builds::ViewSetupOffsets& offsets)
        : m_view(static_cast<uint8_t*>(view)), m_offsets(&offsets) {}

    float* Origin() const { return Field<float>(m_offsets->origin); }  // Vector x,y,z
    float* Angles() const { return Field<float>(m_offsets->angles); }  // QAngle p,y,r

    float& Fov() const { return *Field<float>(m_offsets->fov); }
    float& FovViewmodel() const { return *Field<float>(m_offsets->fov_viewmodel); }

    int RectWidth() const { return *Field<int>(m_offsets->rect_width); }
    int RectHeight() const { return *Field<int>(m_offsets->rect_height); }

private:
    template <typename T>
    T* Field(uint32_t offset) const {
        return reinterpret_cast<T*>(m_view + offset);
    }

    uint8_t* m_view;
    const builds::ViewSetupOffsets* m_offsets;
};

}  // namespace headtracking
