#pragma once
// AYFrameManager.h - 帧率/时间管理

#include <AYCore.h>
#include <AYFrameStats.h>
#include <AYFrameInterpolator.h>
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

    float getDeltaTime() const { return _smoothDeltaTime.getSmoothed(); }
    float getUnscaledDeltaTime() const { return _smoothDeltaTime.getRaw(); }
    float getRealDeltaTime() const { return _rawDeltaTime; }
    float getInterpolationFactor() const;
    float getElapsedTime() const { return _elapsedTime; }
    uint64_t getFrameCount() const { return _frameCount; }

    // 计算下一帧的时间
    void beginFrame();

    // Editor step: advance simulation by a fixed delta without wall clock.
    void beginFrameWithDelta(float deltaSeconds);

    // 获取需要执行的固定更新次数
    int getFixedUpdateCount() const;

    // 时间缩放后的增量时间
    float getScaledDeltaTime() const { return getUnscaledDeltaTime() * _timeScale; }

    // 是否应该跳过渲染（用于低帧率节流）
    bool shouldRender() const;

    // 精确睡眠 + 忙等待限制帧率
    void limitFrameRate();

    // 设置睡眠模式
    void setSleepMode(bool precise) { _preciseSleep = precise; }
    bool isPreciseSleep() const { return _preciseSleep; }

    // 设置忙等待阈值（秒），超过则使用精确睡眠
    void setBusyWaitThreshold(float seconds) { _busyWaitThreshold = seconds; }
    float getBusyWaitThreshold() const { return _busyWaitThreshold; }

    // 平滑 deltaTime
    SmoothDeltaTime& getSmoothDeltaTime() { return _smoothDeltaTime; }
    const SmoothDeltaTime& getSmoothDeltaTime() const { return _smoothDeltaTime; }

    // 设置平滑因子（0-1，越小越平滑）
    void setDeltaTimeSmoothing(float factor) { _smoothDeltaTime.setSmoothingFactor(factor); }
    float getDeltaTimeSmoothing() const { return _smoothDeltaTime.getSmoothingFactor(); }

    // 无限制帧率（用于基准测试）
    void setNoLimitFPS(bool noLimit) { _noLimitFPS = noLimit; }
    bool isNoLimitFPS() const { return _noLimitFPS; }

    // 重置
    void reset();

    // 性能统计
    const FrameStats& getFrameStats() const { return _frameStats.getStats(); }
    FrameStatsCollector& getFrameStats() { return _frameStats; }

private:
    float _targetFPS = 60.0f;
    float _fixedDt = 1.0f / 60.0f;
    float _timeScale = 1.0f;

    float _elapsedTime = 0.0f;

    uint64_t _frameCount = 0;
    uint64_t _renderFrameCount = 0;

    double _lastTime = 0.0;
    double _accumulator = 0.0;
    double _frameInterval = 0.0;

    // 原始 deltaTime（未平滑，用于 TimeType::Real）
    float _rawDeltaTime = 0.0f;

    // 无限制帧率（用于基准测试）
    bool _noLimitFPS = false;

    // 精确睡眠控制
    bool _preciseSleep = true;
    float _busyWaitThreshold = 0.001f;  // 1ms 以下使用忙等待

    // 平滑 deltaTime
    SmoothDeltaTime _smoothDeltaTime;

    // 性能统计
    FrameStatsCollector _frameStats;
};

} // namespace ayt::game