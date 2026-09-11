// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#include "tracker_feed.h"

#include "angles.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "debug_log.h"
#include "position_mapping.h"

namespace headtracking {

namespace {

void ApplyRotationConfig(cameraunlock::TrackingProcessor& processor, const Config& c) {
    cameraunlock::SensitivitySettings s;
    s.yaw = c.sens_yaw;
    s.pitch = c.sens_pitch;
    s.roll = c.sens_roll;
    s.invert_yaw = c.invert_yaw;
    s.invert_pitch = c.invert_pitch;
    s.invert_roll = c.invert_roll;
    processor.SetSensitivity(s);

    cameraunlock::DeadzoneSettings d;
    d.yaw = c.deadzone_yaw;
    d.pitch = c.deadzone_pitch;
    d.roll = c.deadzone_roll;
    processor.SetDeadzone(d);
}

}  // namespace

void TrackerFeed::Start(const Config& config) {
    m_port = config.port;
    // Fold the user's per-axis inversion into the world scale so it lands
    // after the processor's asymmetric Z clamp - see MakePositionSettings.
    m_scaleX = config.pos_world_scale * (config.pos_invert_x ? -1.0f : 1.0f);
    m_scaleY = config.pos_world_scale * (config.pos_invert_y ? -1.0f : 1.0f);
    m_scaleZ = config.pos_world_scale * (config.pos_invert_z ? -1.0f : 1.0f);
    m_session.SetMode(config.pos_enabled
                          ? cameraunlock::TrackingMode::RotationAndPosition
                          : cameraunlock::TrackingMode::RotationOnly);

    ApplyRotationConfig(m_session.GetProcessor(), config);
    m_session.SetLocalSmoothing(config.local_smoothing);
    m_session.SetRemoteSmoothing(config.remote_smoothing);
    // Through the session, not straight onto the processor: the session owns
    // the smoothing pair and recomposes it onto the incoming struct, so the two
    // calls compose in either order rather than the settings silently carrying
    // the struct's own defaults. The session feeds the connection flag that
    // picks between them, from the receiver's source address, every update.
    m_session.SetPositionSettings(MakePositionSettings(config));
    // Our trackers report head position directly, so the core's synthetic
    // pivot-forward term (which cancels a webcam pivot) only injects phantom
    // rotation-coupled movement. Disable it.
    m_session.GetPositionProcessor().SetTrackerPivotForward(0.0f);

    m_receiver.SetLog([](const std::string& msg) { HT_LOG("[receiver] %s", msg.c_str()); });
    if (m_receiver.Start(m_port)) {
        HT_LOG("[plugin] listening on UDP %u", m_port);
    } else {
        HT_LOG("[plugin] UDP port %u busy, receiver will retry in background", m_port);
    }
}

void TrackerFeed::LogConnectionChange() {
    const bool isRemote = m_session.IsRemoteConnection();
    if (m_remoteConnectionKnown && isRemote == m_isRemoteConnection) return;
    m_isRemoteConnection = isRemote;
    m_remoteConnectionKnown = true;

    const double effective = cameraunlock::math::GetEffectiveSmoothing(
        m_session.GetLocalSmoothing(), m_session.GetRemoteSmoothing(), isRemote);
    HT_LOG("[plugin] tracker is %s, smoothing=%.3f", isRemote ? "remote" : "local", effective);
}

void TrackerFeed::Invalidate() {
    m_cachedValid.store(false, std::memory_order_release);
    m_cachedPosValid.store(false, std::memory_order_release);
}

void TrackerFeed::CycleMode() { m_session.CycleMode(); }

const char* TrackerFeed::ModeName() const {
    switch (m_session.GetMode()) {
        case cameraunlock::TrackingMode::RotationAndPosition: return "6DOF (rotation + position)";
        case cameraunlock::TrackingMode::RotationOnly:        return "rotation only";
        case cameraunlock::TrackingMode::PositionOnly:        return "position only";
    }
    return "?";
}

void TrackerFeed::Update(bool enabled) {
    if (!enabled) {
        Invalidate();
        return;
    }

    if (!m_receiver.IsReceiving()) {
        if (m_wasConnected) {
            HT_LOG("[plugin] tracking source disconnected (no packets within timeout)");
            m_wasConnected = false;
        }
        Invalidate();
        return;
    }
    if (!m_wasConnected) {
        HT_LOG("[plugin] tracking source connected on UDP %u (remote=%d)",
               m_port, m_receiver.IsRemoteConnection() ? 1 : 0);
        m_wasConnected = true;
    }

    const float dt = m_frameClock.Tick();
    if (!m_session.Update(dt)) {
        Invalidate();
        return;
    }
    LogConnectionChange();

    float yaw_deg = 0.0f, pitch_deg = 0.0f, roll_deg = 0.0f;
    m_session.GetRotation(yaw_deg, pitch_deg, roll_deg);
    m_cachedYaw.store(yaw_deg     * kDegToRad, std::memory_order_release);
    m_cachedPitch.store(pitch_deg * kDegToRad, std::memory_order_release);
    m_cachedRoll.store(roll_deg   * kDegToRad, std::memory_order_release);
    m_cachedValid.store(true, std::memory_order_release);

    float ox = 0.0f, oy = 0.0f, oz = 0.0f;
    if (m_session.GetPositionOffset(ox, oy, oz)) {
        m_cachedPosX.store(ox * m_scaleX, std::memory_order_release);
        m_cachedPosY.store(oy * m_scaleY, std::memory_order_release);
        m_cachedPosZ.store(oz * m_scaleZ, std::memory_order_release);
        m_cachedPosValid.store(true, std::memory_order_release);
    } else {
        m_cachedPosValid.store(false, std::memory_order_release);
    }
}

bool TrackerFeed::GetRotationRadians(float& yaw, float& pitch, float& roll) const {
    if (!m_cachedValid.load(std::memory_order_acquire)) return false;
    yaw   = m_cachedYaw.load(std::memory_order_acquire);
    pitch = m_cachedPitch.load(std::memory_order_acquire);
    roll  = m_cachedRoll.load(std::memory_order_acquire);
    return true;
}

bool TrackerFeed::GetPositionOffset(float& x, float& y, float& z) const {
    if (!m_cachedPosValid.load(std::memory_order_acquire)) return false;
    x = m_cachedPosX.load(std::memory_order_acquire);
    y = m_cachedPosY.load(std::memory_order_acquire);
    z = m_cachedPosZ.load(std::memory_order_acquire);
    return true;
}

}  // namespace headtracking
