#pragma once
// FrameManager.h - 帧率/时间管理

#include <AYCore.h>
#include <FrameStats.h>
#include <cstdint>

namespace ayt::game
{

class FrameManager {
public:
    FrameManager();

    void setTargetFPS(float fps);
    float getTargetFPS() const { return _targetFPS; }

    void setFixedTimestep(float dt);
    float getFixedTimestep() const { return _fixedDt; }

    void setTimeScale(float scale);
    float getTimeScale() const { return _timeScale; }

    float getDeltaTime() const { return _deltaTime; }
    float getUnscaledDeltaTime() const { return _unscaledDeltaTime; }
    float getElapsedTime() const { return _elapsedTime; }
    uint64_t getFrameCount() const { return _frameCount; }

    // 计算下一帧的时间
    void beginFrame();

    // 获取需要执行的固定更新次数
    int getFixedUpdateCount() const;

    // 时间缩放后的增量时间
    float getScaledDeltaTime() const { return _unscaledDeltaTime * _timeScale; }

    // 是否应该跳过渲染（用于低帧率节流）
    bool shouldRender() const;

    // 限制帧率
    void limitFrameRate();

    // 重置
    void reset();

    // 性能统计
    const FrameStats& getFrameStats() const { return _frameStats.getStats(); }
    FrameStatsCollector& getFrameStats() { return _frameStats; }

private:
    float _targetFPS = 60.0f;
    float _fixedDt = 1.0f / 60.0f;
    float _timeScale = 1.0f;

    float _deltaTime = 0.0f;
    float _unscaledDeltaTime = 0.0f;
    float _elapsedTime = 0.0f;

    uint64_t _frameCount = 0;
    uint64_t _renderFrameCount = 0;

    double _lastTime = 0.0;
    double _accumulator = 0.0;
    double _frameInterval = 0.0;

    // 性能统计
    FrameStatsCollector _frameStats;
};

} // namespace ayt::game