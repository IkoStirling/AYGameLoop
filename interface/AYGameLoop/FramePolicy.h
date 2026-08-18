#pragma once

#include <cstdint>

namespace ayt::game
{

// Behaviour when more fixed simulation steps are due than may run in one
// host frame.
enum class FixedBacklogPolicy : uint8_t {
    // Run the per-frame maximum, then discard complete overdue steps while
    // retaining the interpolation remainder. Best for real-time applications.
    Drop,

    // Never discard accumulated simulation time. The backlog is carried into
    // later host frames and may grow when simulation cannot keep up.
    Preserve,

    // Limit newly accepted game time so the accumulator never receives more
    // work than the current host frame can process. The effective time scale
    // may therefore be lower than the requested scale.
    ClampScale
};

} // namespace ayt::game
