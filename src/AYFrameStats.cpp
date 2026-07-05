// FrameStats.cpp - 帧率/性能统计实现

#include <AYFrameStats.h>
#include <algorithm>
#include <cmath>

namespace ayt::game
{

FrameStatsCollector::FrameStatsCollector() {
    reset();
}

void FrameStatsCollector::update(float frameTimeMs) {
    _sampleCount++;

    // 更新帧时间
    _stats.frameTimeMs = frameTimeMs;

    // 更新最小/最大帧时间
    if (_sampleCount == 1) {
        _stats.minFrameTimeMs = frameTimeMs;
        _stats.maxFrameTimeMs = frameTimeMs;
    } else {
        _stats.minFrameTimeMs = std::min(_stats.minFrameTimeMs, frameTimeMs);
        _stats.maxFrameTimeMs = std::max(_stats.maxFrameTimeMs, frameTimeMs);
    }

    // 更新累计帧时间（用于计算平均值）
    _stats.totalFrameTimeUs += static_cast<uint64_t>(frameTimeMs * 1000.0f);

    // 计算平均帧时间
    if (_sampleCount > 0) {
        _stats.avgFrameTimeMs = static_cast<float>(_stats.totalFrameTimeUs) / (_sampleCount * 1000.0f);
    }

    // 计算 FPS（基于平均帧时间）
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