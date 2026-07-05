// FrameManager.cpp - 帧率/时间管理实现

#include <AYFrameManager.h>
#include <AYCoreTime.h>
#include <cmath>
#include <thread>
#include <cstdio>

namespace ayt::game
{

// 获取高精度时间（微秒精度）
static double getCurrentTimeUs() {
    return static_cast<double>(ayt::performanceNowUs());
}

// 获取高精度时间（秒）
static double getCurrentTime() {
    return getCurrentTimeUs() * 1e-6;
}

// 高精度睡眠
#if defined(_WIN32)
static void sleepHighPrecision(double seconds) {
    if (seconds <= 0.0) return;

    // 转换秒为百纳秒
    LARGE_INTEGER dueTime;
    dueTime.QuadPart = static_cast<LONGLONG>(seconds * 10000000.0);

    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer) {
        SetWaitableTimerEx(timer, &dueTime, 0, nullptr, nullptr, nullptr, 0);
        WaitForSingleObject(timer, INFINITE);
        CloseHandle(timer);
    } else {
        // 回退到普通 sleep
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
    , _renderFrameCount(0)
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

    // 限制最大 delta time，防止卡顿后的跳帧
    if (elapsed > 0.25) {
        elapsed = 0.25;
    }

    // 添加到平滑 deltaTime 计算器
    float elapsedSec = static_cast<float>(elapsed);
    _rawDeltaTime = elapsedSec;
    _smoothDeltaTime.add(elapsedSec);

    _elapsedTime += elapsedSec;
    _accumulator += elapsed;

    // 更新性能统计
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
    // 插值因子：accumulator / fixedDt 的余数部分
    // 表示当前固定时间步内已过去的时间比例
    double fixedDtSeconds = _fixedDt;
    double t = _accumulator / fixedDtSeconds;
    return static_cast<float>(t - std::floor(t));
}

int FrameManager::getFixedUpdateCount() const {
    // 计算累积时间内需要执行的固定更新次数
    double fixedDtSeconds = _fixedDt;
    int count = static_cast<int>(std::floor(_accumulator / fixedDtSeconds));

    // 限制每次循环最多执行的固定更新次数，防止死锁
    return count > 8 ? 8 : count;
}

bool FrameManager::shouldRender() const {
    // 简单策略：每帧都渲染
    // 可以在低帧率时实现帧跳过
    return true;
}

void FrameManager::limitFrameRate() {
    if (_noLimitFPS) return;

    double targetTime = _lastTime + _frameInterval;
    double currentTime = getCurrentTime();

    double sleepTime = targetTime - currentTime;

    if (sleepTime <= 0.0) {
        // 已经落后，跟踪调整
        _lastTime = currentTime;
        return;
    }

    if (_preciseSleep) {
        // 精确睡眠 + 忙等待
        if (sleepTime > _busyWaitThreshold) {
            // 超过阈值，先精确睡眠大部分时间
            double sleepSeconds = sleepTime - _busyWaitThreshold;
            sleepHighPrecision(sleepSeconds);
            currentTime = getCurrentTime();
            sleepTime = targetTime - currentTime;
        }

        // 忙等待剩余时间（微秒级精度）
        while (sleepTime > 0.0) {
            currentTime = getCurrentTime();
            sleepTime = targetTime - currentTime;
        }
    } else {
        // 普通睡眠
        if (sleepTime > 0.0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(sleepTime));
        }
    }

    // 重新同步
    currentTime = getCurrentTime();
    if (currentTime > targetTime + _frameInterval) {
        // 严重落后，重新同步
        _lastTime = currentTime;
    } else {
        _lastTime = targetTime;
    }
}

void FrameManager::reset() {
    _frameCount = 0;
    _renderFrameCount = 0;
    _elapsedTime = 0.0f;
    _accumulator = 0.0;
    _lastTime = getCurrentTime();
    _frameStats.reset();
}

} // namespace ayt::game