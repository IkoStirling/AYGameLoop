// FrameManager.cpp - 帧率/时间管理实现

#include <AYFrameManager.h>
#include <AYCoreTime.h>
#include <cmath>
#include <thread>
#include <cstdio>

#if defined(_WIN32)
    #include <windows.h>
#endif

namespace ayt::game
{

// High-resolution wall clock (microseconds).
static double getCurrentTimeUs() {
    return static_cast<double>(ayt::performanceNowUs());
}

static double getCurrentTime() {
    return getCurrentTimeUs() * 1e-6;
}

// High-precision sleep. Uses Win32 waitable timer on Windows, clock_nanosleep
// on Linux, std::this_thread::sleep_for as fallback.
#if defined(_WIN32)
static void sleepHighPrecision(double seconds) {
    if (seconds <= 0.0) return;

    LARGE_INTEGER dueTime;
    dueTime.QuadPart = static_cast<LONGLONG>(seconds * 10000000.0);

    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer) {
        SetWaitableTimerEx(timer, &dueTime, 0, nullptr, nullptr, nullptr, 0);
        WaitForSingleObject(timer, INFINITE);
        CloseHandle(timer);
    } else {
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    }
}
#elif defined(__linux__)
static void sleepHighPrecision(double seconds) {
    if (seconds <= 0.0) return;
    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(seconds);
    ts.tv_nsec = static_cast<long>((seconds - ts.tv_sec) * 1e9);
    clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
}
#else
static void sleepHighPrecision(double seconds) {
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}
#endif

FrameManager::FrameManager()
    : _targetFPS(60.0f)
    , _fixedDt(1.0f / 60.0f)
    , _timeScale(1.0f)
    , _elapsedTime(0.0f)
    , _frameCount(0)
    , _lastTime(0.0)
    , _accumulator(0.0)
    , _frameInterval(1.0 / 60.0)
{
    _lastTime = getCurrentTime();
}

void FrameManager::setTargetFPS(float fps) {
    _targetFPS = fps > 0.0f ? fps : 60.0f;
    _frameInterval = 1.0 / static_cast<double>(_targetFPS);
}

void FrameManager::setFixedTimestep(float dt) {
    _fixedDt = dt > 0.0f ? dt : 1.0f / 60.0f;
}

void FrameManager::setTimeScale(float scale) {
    _timeScale = scale >= 0.0f ? scale : 1.0f;
}

void FrameManager::beginFrame() {
    double currentTime = getCurrentTime();
    double elapsed = currentTime - _lastTime;
    _lastTime = currentTime;

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
    _accumulator += elapsed;

    float frameTimeMs = elapsedSec * 1000.0f;
    _frameStats.update(frameTimeMs);

    _frameCount++;
}

void FrameManager::beginFrameWithDelta(float deltaSeconds) {
    float elapsedSec = deltaSeconds > 0.0f ? deltaSeconds : _fixedDt;
    if (elapsedSec > 0.25f) {
        elapsedSec = 0.25f;
    }

    _rawDeltaTime = elapsedSec;
    _smoothDeltaTime.add(elapsedSec);
    _elapsedTime += elapsedSec;
    _accumulator += static_cast<double>(elapsedSec);

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
    return count > 8 ? 8 : count;
}

void FrameManager::limitFrameRate() {
    if (_noLimitFPS) return;

    double targetTime = _lastTime + _frameInterval;
    double currentTime = getCurrentTime();

    double sleepTime = targetTime - currentTime;

    if (sleepTime <= 0.0) {
        // Already behind; resync to wall clock to avoid run-away accumulation.
        _lastTime = currentTime;
        return;
    }

    if (_preciseSleep) {
        if (sleepTime > _busyWaitThreshold) {
            double sleepSeconds = sleepTime - _busyWaitThreshold;
            sleepHighPrecision(sleepSeconds);
            currentTime = getCurrentTime();
            sleepTime = targetTime - currentTime;
        }

        // Busy-wait remainder.
        while (sleepTime > 0.0) {
            currentTime = getCurrentTime();
            sleepTime = targetTime - currentTime;
        }
    } else {
        if (sleepTime > 0.0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(sleepTime));
        }
    }

    currentTime = getCurrentTime();
    if (currentTime > targetTime + _frameInterval) {
        _lastTime = currentTime;
    } else {
        _lastTime = targetTime;
    }
}

void FrameManager::reset() {
    _frameCount = 0;
    _elapsedTime = 0.0f;
    _accumulator = 0.0;
    _lastTime = getCurrentTime();
    _frameStats.reset();
}

} // namespace ayt::game