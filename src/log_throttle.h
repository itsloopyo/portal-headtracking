// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo / CameraUnlock
#pragma once

namespace headtracking {

// The per-frame log schedule both render-path diagnostics run on: a burst of
// unconditional opening lines, then a dense interval while the run is still
// young, then a thin one for the rest of the session.
//
// The shape is what matters, and it is the same in both places. The opening
// burst is where an install-time fault shows (a wrong offset, a profile that
// does not fit). The thin steady trickle is what a late report is read from -
// "it drifted after an hour", "it stopped when I loaded a save" - which a burst
// that goes silent cannot see. Thinning is also what keeps the render thread
// out of the log mutex.
//
// Counted in two currencies on purpose: `m_frame` advances every call so the
// intervals are frames, while `m_lines` advances only when a line is actually
// emitted, so the tiers are lines. Render-thread only, like both callers.
class LogThrottle {
public:
    // `burst` opening lines are emitted unconditionally. Until `earlyLines`
    // have been emitted the interval is `earlyIntervalFrames`, and from then on
    // `steadyIntervalFrames`. Passing burst == earlyLines is the two-tier case.
    constexpr LogThrottle(int burst, int earlyLines, int earlyIntervalFrames,
                          int steadyIntervalFrames)
        : m_burst(burst),
          m_earlyLines(earlyLines),
          m_earlyIntervalFrames(earlyIntervalFrames),
          m_steadyIntervalFrames(steadyIntervalFrames) {}

    // Call once per frame from the path being logged. True on the frames whose
    // line should be written.
    bool ShouldLog() {
        ++m_frame;
        if (m_lines < m_burst) {
            ++m_lines;
            return true;
        }
        const int interval =
            m_lines < m_earlyLines ? m_earlyIntervalFrames : m_steadyIntervalFrames;
        if (m_frame % interval != 0) return false;
        ++m_lines;
        return true;
    }

private:
    const int m_burst;
    const int m_earlyLines;
    const int m_earlyIntervalFrames;
    const int m_steadyIntervalFrames;

    int m_frame = 0;
    int m_lines = 0;
};

}  // namespace headtracking
