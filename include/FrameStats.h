#pragma once
// AYFrameStats.h - 帧率/性能统计

#include <AYCore.h>
#include <cstdint>

namespace ayt::game
{

// 帧统计信息
struct FrameStats {
    float fps = 0.0f;
    float frameTimeMs = 0.0f;
    float minFrameTimeMs = 0.0f;
    float maxFrameTimeMs = 0.0f;
    float avgFrameTimeMs = 0.0f;
    uint64_t frameCount = 0;
    uint64_t totalFrameTimeUs = 0;  // 微秒累计
};

// 帧统计器
class AYFrameStats {
public:
    AYFrameStats();

    // 更新统计（每帧调用）
    void update(float frameTimeMs);

    // 获取当前统计
    const FrameStats& getStats() const { return _stats; }

    // 重置统计
    void reset();

    // 获取 FPS
    float getFPS() const { return _stats.fps; }

    // 获取平均帧时间（毫秒）
    float getAvgFrameTimeMs() const { return _stats.avgFrameTimeMs; }

    // 获取最小帧时间（毫秒）
    float getMinFrameTimeMs() const { return _stats.minFrameTimeMs; }

    // 获取最大帧时间（毫秒）
    float getMaxFrameTimeMs() const { return _stats.maxFrameTimeMs; }

private:
    FrameStats _stats;
    uint64_t _sampleCount = 0;
    static constexpr uint64_t MAX_SAMPLES = 1000;
};

} // namespace ayt::game