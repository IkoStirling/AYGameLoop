#pragma once
// AYGameLoop/RenderThreadSync.h - 渲染线程同步

#include <AYCore.h>
#include <functional>
#include <memory>

namespace ayt::game
{

class RenderThreadSync {
public:
    RenderThreadSync();
    ~RenderThreadSync();

    // 主线程：等待上一帧渲染完成
    void waitForRenderComplete();

    // 渲染线程：通知主线程渲染完成
    void signalRenderComplete();

    // 主线程：通知渲染线程逻辑帧完成
    void signalLogicComplete();

    // 渲染线程：等待逻辑帧完成
    void waitForLogicComplete();

    // 是否启用渲染线程
    void setRenderThreadEnabled(bool enabled);
    [[nodiscard]] bool isRenderThreadEnabled() const { return _renderThreadEnabled; }

    // 启动/停止渲染线程
    void startRenderThread();
    void stopRenderThread();

    [[nodiscard]] bool isRenderThreadRunning() const { return _renderThreadRunning.load(); }

    using RenderThreadCallback = std::function<void()>;
    void setRenderCallback(RenderThreadCallback callback);
    void invokeRenderCallback();

private:
    void renderThreadMain();

    bool _renderThreadEnabled = false;
    ayt::AtomicBool _renderThreadRunning{false};
    std::unique_ptr<ayt::Thread> _renderThread;
    RenderThreadCallback _renderCallback;

    // Semaphore-via-condvar. POSIX requires notify under the same mutex
    // as the wait predicate, otherwise the waiter may miss the signal
    // during the window between releasing the mutex inside wait() and
    // entering the wait queue. Both signalLogicComplete and
    // signalRenderComplete therefore take the corresponding mutex.
    ayt::Mutex _logicMutex;
    ayt::Mutex _renderMutex;
    ayt::ConditionVariable _logicCond;
    ayt::ConditionVariable _renderCond;
    // Counting-semaphore style handshake. A completion/request remains
    // observable even if its waiter arrives after the notification.
    int _logicRequested = 0;
    int _logicConsumed = 0;
    int _renderCompleted = 0;
    int _renderConsumed = 0;
};

} // namespace ayt::game
