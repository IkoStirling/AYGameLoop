#pragma once
// AYFrameStats.h - 帧率/性能统计

#include <AYCore.h>
#include <cstdint>

namespace ayt::game
{

// 帧统计信息（一次聚合后的快照）
//
// NOTE: `lastFrameTimeMs` holds the most-recently reported frame time only
// (not a stats aggregate). For min/avg/max see the other fields.
struct FrameStats {
    float fps = 0.0f;
    float lastFrameTimeMs = 0.0f;   // last single frame's time, ms
    float minFrameTimeMs = 0.0f;
    float maxFrameTimeMs = 0.0f;
    float avgFrameTimeMs = 0.0f;
    uint64_t frameCount = 0;
    uint64_t totalFrameTimeUs = 0;  // 微秒累计
};

// 帧统计器（main-thread only）
class FrameStatsCollector {
public:
    FrameStatsCollector();

    // 更新统计（每帧调用）
    void update(float frameTimeMs);

    const FrameStats& getStats() const { return _stats; }

    void reset();

    [[nodiscard]] float getFPS() const { return _stats.fps; }
    [[nodiscard]] float getAvgFrameTimeMs() const { return _stats.avgFrameTimeMs; }
    [[nodiscard]] float getMinFrameTimeMs() const { return _stats.minFrameTimeMs; }
    [[nodiscard]] float getMaxFrameTimeMs() const { return _stats.maxFrameTimeMs; }

private:
    FrameStats _stats;
    uint64_t _sampleCount = 0;
};

} // namespace ayt::game