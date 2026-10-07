#pragma once
// AYGameLoop/FrameManager.h - 帧率/时间管理

#include <AYCore.h>
#include <AYTime/IMonotonicClock.h>
#include <AYGameLoop/FramePolicy.h>
#include <AYGameLoop/FrameStats.h>
#include <AYGameLoop/FrameInterpolator.h>
#include <AYGameLoop/FixedTimestep.h>
#include <cstdint>

namespace ayt::game
{

// FrameManager owns wall-clock time + smoothing + frame statistics.
// Threading: main-thread only.
class FrameManager {
public:
    explicit FrameManager(ayt::time::IMonotonicClock* clock = nullptr);

    // fps is clamped to [1, 10000]; <=0 falls back to 60.
    void setTargetFPS(float fps);
    [[nodiscard]] float getTargetFPS() const { return _targetFPS; }

    // dt is clamped to (>0); <=0 falls back to 1/60.
    void setFixedTimestep(float dt);
    [[nodiscard]] float getFixedTimestep() const { return _fixedDt; }
    /// Configure a supported rational step atomically; failure leaves timing unchanged.
    [[nodiscard]] bool setFixedTimestepRatio(uint32_t numerator, uint32_t denominator);
    [[nodiscard]] FixedTimestep getFixedStep() const { return _fixedStep; }

    // scale is clamped to >= 0; <0 falls back to 1.
    void setTimeScale(float scale);
    [[nodiscard]] float getTimeScale() const { return _timeScale; }

    [[nodiscard]] float getDeltaTime() const {
        return _smoothDeltaTime.getSmoothed() * _effectiveTimeScale;
    }
    [[nodiscard]] float getUnscaledDeltaTime() const { return _smoothDeltaTime.getRaw(); }
    [[nodiscard]] float getRealDeltaTime() const { return _realWallDeltaTime; }
    [[nodiscard]] float getInterpolationFactor() const;
    [[nodiscard]] float getElapsedTime() const { return _elapsedTime; }
    [[nodiscard]] double getGameElapsedTime() const { return _gameElapsedTime; }
    [[nodiscard]] float getEffectiveTimeScale() const { return _effectiveTimeScale; }
    [[nodiscard]] uint64_t getFrameCount() const { return _frameCount; }

    // Compute next-frame timing. Caps elapsed at 0.25s to prevent tunnelling
    // after long stalls (debugger break, OS pause). The sample timestamp is
    // always advanced to the current AYTime monotonic value, so the cap does
    // not leak the discarded stall time into the next frame.
    // Set advanceGameClock=false for paused host frames: wall/unscaled clocks
    // and frame statistics still advance, but the fixed-step accumulator does not.
    void beginFrame(bool advanceGameClock = true);

    // Deterministic/editor injection without reading the wall clock. A zero
    // delta remains zero (important for an external host's first frame).
    // Editor single-step leaves applyTimeScale=false so one step still
    // advances while paused; tests/replay may opt into normal game-clock
    // scaling.
    void beginFrameWithDelta(float deltaSeconds,
                             bool applyTimeScale = false,
                             bool advanceGameClock = true);

    [[nodiscard]] int getFixedUpdateCount() const;

    // Consume only successfully committed fixed steps. Backlog handling then
    // follows FixedBacklogPolicy.
    void consumeFixedUpdates(int count);

    void setMaxFixedUpdatesPerFrame(uint32_t count);
    [[nodiscard]] uint32_t getMaxFixedUpdatesPerFrame() const { return _maxFixedUpdatesPerFrame; }
    [[nodiscard]] uint64_t getDroppedSimTickCount() const { return _droppedSimTicks; }
    [[nodiscard]] double getDroppedSimulationSeconds() const { return _droppedSimulationSeconds; }
    void setFixedBacklogPolicy(FixedBacklogPolicy policy) { _backlogPolicy = policy; }
    [[nodiscard]] FixedBacklogPolicy getFixedBacklogPolicy() const { return _backlogPolicy; }
    [[nodiscard]] double getClampedSimulationSeconds() const { return _clampedSimulationSeconds; }

    // Game time actually accepted this frame. It equals rawDelta*timeScale for
    // Drop/Preserve and may be smaller for ClampScale.
    [[nodiscard]] float getScaledDeltaTime() const { return _gameDeltaTime; }

    // 精确睡眠 + 忙等待限制帧率
    void limitFrameRate();

    void setSleepMode(bool precise) { _preciseSleep = precise; }
    [[nodiscard]] bool isPreciseSleep() const { return _preciseSleep; }

    void setBusyWaitThreshold(float seconds);
    [[nodiscard]] float getBusyWaitThreshold() const { return _busyWaitThreshold; }

    SmoothDeltaTime& getSmoothDeltaTime() { return _smoothDeltaTime; }
    const SmoothDeltaTime& getSmoothDeltaTime() const { return _smoothDeltaTime; }

    void setDeltaTimeSmoothing(float factor) { _smoothDeltaTime.setSmoothingFactor(factor); }
    [[nodiscard]] float getDeltaTimeSmoothing() const { return _smoothDeltaTime.getSmoothingFactor(); }

    void setNoLimitFPS(bool noLimit);
    [[nodiscard]] bool isNoLimitFPS() const { return _noLimitFPS; }

    void reset();

    const FrameStats& getFrameStats() const { return _frameStats.getStats(); }
    FrameStatsCollector& getFrameStats() { return _frameStats; }

private:
    void accumulateGameTime(double elapsedSeconds,
                            double requestedGameDelta,
                            bool advanceGameClock);

    float _targetFPS = 60.0f;
    FixedTimestep _fixedStep;
    float _fixedDt = _fixedStep.nativeDeltaTime();
    float _timeScale = 1.0f;

    float _elapsedTime = 0.0f;
    double _gameElapsedTime = 0.0;

    uint64_t _frameCount = 0;

    ayt::time::IMonotonicClock* _clock = nullptr;
    uint64_t _sampleTimeUs = 0;
    double _nextFrameDeadlineUs = 0.0;
    double _accumulator = 0.0;
    double _frameIntervalUs = 0.0;
    uint32_t _maxFixedUpdatesPerFrame = 8;
    uint64_t _droppedSimTicks = 0;
    double _droppedSimulationSeconds = 0.0;
    double _clampedSimulationSeconds = 0.0;
    FixedBacklogPolicy _backlogPolicy = FixedBacklogPolicy::Drop;

    // Clamped unscaled delta plus the uncapped monotonic wall delta.
    float _rawDeltaTime = 0.0f;
    float _realWallDeltaTime = 0.0f;
    float _gameDeltaTime = 0.0f;
    float _effectiveTimeScale = 1.0f;

    bool _noLimitFPS = false;

    // Precise sleep control
    bool _preciseSleep = true;
    float _busyWaitThreshold = 0.001f;  // 1ms 以下使用忙等待

    SmoothDeltaTime _smoothDeltaTime;
    FrameStatsCollector _frameStats;
};

} // namespace ayt::game
