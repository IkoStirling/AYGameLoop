// FrameManager.cpp - 帧率/时间管理实现

#include <AYGameLoop/FrameManager.h>
#include <algorithm>
#include <cmath>

namespace ayt::game
{

FrameManager::FrameManager(ayt::time::IMonotonicClock* clock)
    : _targetFPS(60.0f)
    , _fixedDt(1.0f / 60.0f)
    , _timeScale(1.0f)
    , _elapsedTime(0.0f)
    , _frameCount(0)
    , _clock(clock != nullptr ? clock : &ayt::time::IMonotonicClock::system())
    , _sampleTimeUs(0)
    , _nextFrameDeadlineUs(0.0)
    , _accumulator(0.0)
    , _frameIntervalUs(1000000.0 / 60.0)
    , _rawDeltaTime(0.0f)
    , _realWallDeltaTime(0.0f)
    , _noLimitFPS(false)
    , _preciseSleep(true)
    , _busyWaitThreshold(0.001f)
{
    _sampleTimeUs = _clock->nowUs();
    _nextFrameDeadlineUs = static_cast<double>(_sampleTimeUs) + _frameIntervalUs;
}

void FrameManager::setTargetFPS(float fps) {
    _targetFPS = std::isfinite(fps) && fps > 0.0f
        ? std::clamp(fps, 1.0f, 10000.0f)
        : 60.0f;
    _frameIntervalUs = 1000000.0 / static_cast<double>(_targetFPS);
    _nextFrameDeadlineUs = static_cast<double>(_clock->nowUs()) + _frameIntervalUs;
}

void FrameManager::setFixedTimestep(float dt) {
    _fixedDt = std::isfinite(dt) && dt > 0.0f ? dt : 1.0f / 60.0f;
}

void FrameManager::setTimeScale(float scale) {
    _timeScale = std::isfinite(scale) && scale >= 0.0f ? scale : 1.0f;
}

void FrameManager::setBusyWaitThreshold(float seconds) {
    _busyWaitThreshold = std::isfinite(seconds) && seconds > 0.0f
        ? seconds
        : 0.0f;
}

void FrameManager::accumulateGameTime(double elapsedSeconds,
                                      double requestedGameDelta,
                                      bool advanceGameClock) {
    _gameDeltaTime = 0.0f;
    _effectiveTimeScale = 0.0f;
    if (!advanceGameClock) return;

    double acceptedGameDelta = std::max(0.0, requestedGameDelta);
    if (_backlogPolicy == FixedBacklogPolicy::ClampScale) {
        const double frameCapacity = static_cast<double>(_fixedDt)
            * static_cast<double>(_maxFixedUpdatesPerFrame);
        const double available = std::max(0.0, frameCapacity - _accumulator);
        if (acceptedGameDelta > available) {
            _clampedSimulationSeconds += acceptedGameDelta - available;
            acceptedGameDelta = available;
        }
    }

    _accumulator += acceptedGameDelta;
    _gameElapsedTime += acceptedGameDelta;
    _gameDeltaTime = static_cast<float>(acceptedGameDelta);
    if (elapsedSeconds > 0.0) {
        _effectiveTimeScale = static_cast<float>(acceptedGameDelta / elapsedSeconds);
    }
}

void FrameManager::beginFrame(bool advanceGameClock) {
    const uint64_t currentTimeUs = _clock->nowUs();
    const uint64_t elapsedUs = currentTimeUs >= _sampleTimeUs
        ? currentTimeUs - _sampleTimeUs
        : 0;
    _sampleTimeUs = currentTimeUs;
    double elapsed = static_cast<double>(elapsedUs) * 1e-6;

    _realWallDeltaTime = static_cast<float>(elapsed);

    // Cap to prevent tunnelling after long stalls. _lastTime was already
    // advanced above, so this only affects the reported value, not the
    // timeline (next frame sees real wall-clock delta).
    if (elapsed > 0.25) {
        elapsed = 0.25;
    }

    float elapsedSec = static_cast<float>(elapsed);
    _rawDeltaTime = elapsedSec;
    _smoothDeltaTime.add(elapsedSec);

    _elapsedTime += elapsedSec;
    accumulateGameTime(elapsed,
                       elapsed * static_cast<double>(_timeScale),
                       advanceGameClock);

    float frameTimeMs = elapsedSec * 1000.0f;
    _frameStats.update(frameTimeMs);

    _frameCount++;
}

void FrameManager::beginFrameWithDelta(float deltaSeconds,
                                       bool applyTimeScale,
                                       bool advanceGameClock) {
    // A host's first frame legitimately has zero elapsed time. Preserve that
    // instead of manufacturing a fixed step; stepOnce passes _fixedDt itself.
    float elapsedSec = std::isfinite(deltaSeconds) && deltaSeconds > 0.0f
        ? deltaSeconds
        : 0.0f;
    if (elapsedSec > 0.25f) {
        elapsedSec = 0.25f;
    }

    _rawDeltaTime = elapsedSec;
    _realWallDeltaTime = elapsedSec;
    _smoothDeltaTime.add(elapsedSec);
    _elapsedTime += elapsedSec;
    const double accumulatorDelta = static_cast<double>(elapsedSec)
        * (applyTimeScale ? static_cast<double>(_timeScale) : 1.0);
    accumulateGameTime(static_cast<double>(elapsedSec),
                       accumulatorDelta,
                       advanceGameClock);

    _frameStats.update(elapsedSec * 1000.0f);
    _frameCount++;
}

float FrameManager::getInterpolationFactor() const {
    // Accumulator remainder within the current fixed step = how far into
    // the step we are. Clamped to [0, 1).
    double fixedDtSeconds = _fixedDt;
    double t = _accumulator / fixedDtSeconds;
    return static_cast<float>(t - std::floor(t));
}

int FrameManager::getFixedUpdateCount() const {
    double fixedDtSeconds = _fixedDt;
    int count = static_cast<int>(std::floor(_accumulator / fixedDtSeconds));

    // Cap per-frame fixed steps to avoid death-spiral after stalls.
    const int maximum = static_cast<int>(_maxFixedUpdatesPerFrame);
    return count > maximum ? maximum : count;
}

void FrameManager::setMaxFixedUpdatesPerFrame(uint32_t count) {
    _maxFixedUpdatesPerFrame = std::clamp(count, 1u, 64u);
}

void FrameManager::consumeFixedUpdates(int count) {
    if (count <= 0) return;

    const double fixedDtSeconds = static_cast<double>(_fixedDt);
    _accumulator = std::max(0.0, _accumulator - fixedDtSeconds * count);

    // Drop intentionally skips only complete overdue steps and retains the
    // interpolation remainder. Preserve carries debt; ClampScale prevents new
    // debt at accumulation time (and drains pre-existing debt after a switch).
    if (_backlogPolicy == FixedBacklogPolicy::Drop
        && count == static_cast<int>(_maxFixedUpdatesPerFrame)
        && _accumulator >= fixedDtSeconds) {
        const uint64_t dropped = static_cast<uint64_t>(
            std::floor(_accumulator / fixedDtSeconds));
        _droppedSimTicks += dropped;
        _droppedSimulationSeconds += static_cast<double>(dropped) * fixedDtSeconds;
        _accumulator = std::fmod(_accumulator, fixedDtSeconds);
    }
}

void FrameManager::limitFrameRate() {
    if (_noLimitFPS) return;

    double currentTimeUs = static_cast<double>(_clock->nowUs());
    double sleepTimeUs = _nextFrameDeadlineUs - currentTimeUs;

    if (sleepTimeUs <= 0.0) {
        // Already behind. Preserve a stable cadence for small overruns, but
        // resync after a full missed interval to avoid runaway catch-up.
        if (currentTimeUs > _nextFrameDeadlineUs + _frameIntervalUs) {
            _nextFrameDeadlineUs = currentTimeUs + _frameIntervalUs;
        } else {
            _nextFrameDeadlineUs += _frameIntervalUs;
        }
        return;
    }

    if (_preciseSleep) {
        const double busyWaitThresholdUs =
            static_cast<double>(_busyWaitThreshold) * 1000000.0;
        if (sleepTimeUs > busyWaitThresholdUs) {
            const int64_t coarseSleepUs = static_cast<int64_t>(
                sleepTimeUs - busyWaitThresholdUs);
            _clock->sleepFor(ayt::time::Duration::fromUs(coarseSleepUs));
            currentTimeUs = static_cast<double>(_clock->nowUs());
            sleepTimeUs = _nextFrameDeadlineUs - currentTimeUs;
        }

        // Busy-wait remainder.
        while (sleepTimeUs > 0.0) {
            currentTimeUs = static_cast<double>(_clock->nowUs());
            sleepTimeUs = _nextFrameDeadlineUs - currentTimeUs;
        }
    } else {
        _clock->sleepFor(ayt::time::Duration::fromUs(
            static_cast<int64_t>(sleepTimeUs)));
    }

    currentTimeUs = static_cast<double>(_clock->nowUs());
    if (currentTimeUs > _nextFrameDeadlineUs + _frameIntervalUs) {
        _nextFrameDeadlineUs = currentTimeUs + _frameIntervalUs;
    } else {
        _nextFrameDeadlineUs += _frameIntervalUs;
    }
}

void FrameManager::setNoLimitFPS(bool noLimit) {
    if (_noLimitFPS == noLimit) return;
    _noLimitFPS = noLimit;
    _nextFrameDeadlineUs = static_cast<double>(_clock->nowUs()) + _frameIntervalUs;
}

void FrameManager::reset() {
    _frameCount = 0;
    _elapsedTime = 0.0f;
    _gameElapsedTime = 0.0;
    _accumulator = 0.0;
    _sampleTimeUs = _clock->nowUs();
    _nextFrameDeadlineUs = static_cast<double>(_sampleTimeUs) + _frameIntervalUs;
    _rawDeltaTime = 0.0f;
    _realWallDeltaTime = 0.0f;
    _droppedSimTicks = 0;
    _droppedSimulationSeconds = 0.0;
    _clampedSimulationSeconds = 0.0;
    _gameDeltaTime = 0.0f;
    _effectiveTimeScale = 1.0f;
    _frameStats.reset();
}

} // namespace ayt::game
