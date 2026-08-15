// FrameStats.cpp - 帧率/性能统计实现

#include <AYGameLoop/FrameStats.h>
#include <algorithm>
#include <cmath>

namespace ayt::game
{

FrameStatsCollector::FrameStatsCollector() {
    reset();
}

void FrameStatsCollector::update(float frameTimeMs) {
    _sampleCount++;

    // Last frame time (not a stats aggregate).
    _stats.lastFrameTimeMs = frameTimeMs;

    // Min/max observed since last reset().
    if (_sampleCount == 1) {
        _stats.minFrameTimeMs = frameTimeMs;
        _stats.maxFrameTimeMs = frameTimeMs;
    } else {
        _stats.minFrameTimeMs = std::min(_stats.minFrameTimeMs, frameTimeMs);
        _stats.maxFrameTimeMs = std::max(_stats.maxFrameTimeMs, frameTimeMs);
    }

    _stats.totalFrameTimeUs += static_cast<uint64_t>(frameTimeMs * 1000.0f);

    if (_sampleCount > 0) {
        _stats.avgFrameTimeMs = static_cast<float>(_stats.totalFrameTimeUs) / (_sampleCount * 1000.0f);
    }

    if (_stats.avgFrameTimeMs > 0.0f) {
        _stats.fps = 1000.0f / _stats.avgFrameTimeMs;
    }

    _stats.frameCount++;
}

void FrameStatsCollector::reset() {
    _stats = FrameStats{};
    _sampleCount = 0;
}

} // namespace ayt::game