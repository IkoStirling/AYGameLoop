#pragma once
// AYFrameManager.h - 帧率/时间管理

#include <AYCore.h>
#include <AYFrameStats.h>
#include <AYFrameInterpolator.h>
#include <cstdint>

namespace ayt::game
{

// FrameManager owns wall-clock time + smoothing + frame statistics.
// Threading: main-thread only.
class FrameManager {
public:
    FrameManager();

    // fps is clamped to [1, 10000]; <=0 falls back to 60.
    void setTargetFPS(float fps);
    [[nodiscard]] float getTargetFPS() const { return _targetFPS; }

    // dt is clamped to (>0); <=0 falls back to 1/60.
    void setFixedTimestep(float dt);
    [[nodiscard]] float getFixedTimestep() const { return _fixedDt; }

    // scale is clamped to >= 0; <0 falls back to 1.
    void setTimeScale(float scale);
    [[nodiscard]] float getTimeScale() const { return _timeScale; }

    [[nodiscard]] float getDeltaTime() const { return _smoothDeltaTime.getSmoothed(); }
    [[nodiscard]] float getUnscaledDeltaTime() const { return _smoothDeltaTime.getRaw(); }
    [[nodiscard]] float getRealDeltaTime() const { return _rawDeltaTime; }
    [[nodiscard]] float getInterpolationFactor() const;
    [[nodiscard]] float getElapsedTime() const { return _elapsedTime; }
    [[nodiscard]] uint64_t getFrameCount() const { return _frameCount; }

    // Compute next-frame timing. Caps elapsed at 0.25s to prevent tunnelling
    // after long stalls (debugger break, OS pause). The cap affects only
    // the *reported* delta / smoothed value; _lastTime is NOT advanced by
    // the cap, so the next frame's elapsed reflects wall-clock recovery.
    void beginFrame();

    // Editor step: advance simulation by a fixed delta without wall clock.
    void beginFrameWithDelta(float deltaSeconds);

    [[nodiscard]] int getFixedUpdateCount() const;

    [[nodiscard]] float getScaledDeltaTime() const { return getUnscaledDeltaTime() * _timeScale; }

    // 精确睡眠 + 忙等待限制帧率
    void limitFrameRate();

    void setSleepMode(bool precise) { _preciseSleep = precise; }
    [[nodiscard]] bool isPreciseSleep() const { return _preciseSleep; }

    void setBusyWaitThreshold(float seconds) { _busyWaitThreshold = seconds; }
    [[nodiscard]] float getBusyWaitThreshold() const { return _busyWaitThreshold; }

    SmoothDeltaTime& getSmoothDeltaTime() { return _smoothDeltaTime; }
    const SmoothDeltaTime& getSmoothDeltaTime() const { return _smoothDeltaTime; }

    void setDeltaTimeSmoothing(float factor) { _smoothDeltaTime.setSmoothingFactor(factor); }
    [[nodiscard]] float getDeltaTimeSmoothing() const { return _smoothDeltaTime.getSmoothingFactor(); }

    void setNoLimitFPS(bool noLimit) { _noLimitFPS = noLimit; }
    [[nodiscard]] bool isNoLimitFPS() const { return _noLimitFPS; }

    void reset();

    const FrameStats& getFrameStats() const { return _frameStats.getStats(); }
    FrameStatsCollector& getFrameStats() { return _frameStats; }

private:
    float _targetFPS = 60.0f;
    float _fixedDt = 1.0f / 60.0f;
    float _timeScale = 1.0f;

    float _elapsedTime = 0.0f;

    uint64_t _frameCount = 0;

    double _lastTime = 0.0;
    double _accumulator = 0.0;
    double _frameInterval = 0.0;

    // Raw deltaTime (pre-smoothing). For TimeType::Real consumers.
    float _rawDeltaTime = 0.0f;

    bool _noLimitFPS = false;

    // Precise sleep control
    bool _preciseSleep = true;
    float _busyWaitThreshold = 0.001f;  // 1ms 以下使用忙等待

    SmoothDeltaTime _smoothDeltaTime;
    FrameStatsCollector _frameStats;
};

} // namespace ayt::game