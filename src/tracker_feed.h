// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

#include <atomic>
#include <cstdint>

#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"
#include "config.h"

namespace headtracking {

// The tracker, the pose pipeline and the frame clock behind the rendered view.
//
// Update() runs on the render thread; the getters are read from the same
// thread today but the cache is atomic so a future reader elsewhere sees a
// consistent published pose rather than a half-written one.
class TrackerFeed {
public:
    void Start(const Config& config);

    // Pulls the latest packet, runs the pipeline and caches the frame's
    // rotation (radians) + position offset (Source units). Called once per
    // rendered frame. `enabled` is the plugin's toggle, passed in rather than
    // mirrored here: one owner for the flag means the two cannot drift, and the
    // hotkey thread never writes the pose cache.
    void Update(bool enabled);

    bool GetRotationRadians(float& yaw, float& pitch, float& roll) const;
    bool GetPositionOffset(float& x, float& y, float& z) const;

    void CycleMode();
    const char* ModeName() const;

private:
    void Invalidate();
    // Logs which smoothing parameter is in force when this feed's tracker
    // switches between local and remote. The session does the selection.
    void LogConnectionChange();

    uint16_t m_port = 0;
    bool m_wasConnected = false;

    cameraunlock::UdpReceiver m_receiver;
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session{m_receiver};
    // Without IsRemoteConnection() on the receiver the session silently falls
    // back to LocalSmoothing forever, with nothing at the call site to show it.
    static_assert(decltype(m_session)::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection() or remote smoothing never applies");
    cameraunlock::time::FrameClock m_frameClock;
    // Metres -> Source units, carrying the user's Invert* preference as a sign
    // so inversion lands after the processor's asymmetric Z clamp.
    float m_scaleX = kDefaultPosWorldScale;
    float m_scaleY = kDefaultPosWorldScale;
    float m_scaleZ = kDefaultPosWorldScale;

    bool m_isRemoteConnection = false;
    // Tri-state: false/false is indistinguishable from a local tracker, so a
    // plain equality check never reports the (common) local case at all.
    bool m_remoteConnectionKnown = false;

    std::atomic<float> m_cachedYaw{0.0f};
    std::atomic<float> m_cachedPitch{0.0f};
    std::atomic<float> m_cachedRoll{0.0f};
    std::atomic<bool>  m_cachedValid{false};

    std::atomic<float> m_cachedPosX{0.0f};
    std::atomic<float> m_cachedPosY{0.0f};
    std::atomic<float> m_cachedPosZ{0.0f};
    std::atomic<bool>  m_cachedPosValid{false};
};

}  // namespace headtracking
