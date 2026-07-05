#pragma once
// AYRenderThreadSync.h - 渲染线程同步

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
    bool isRenderThreadEnabled() const { return _renderThreadEnabled; }

    // 启动/停止渲染线程
    void startRenderThread();
    void stopRenderThread();

    // 渲染线程是否在运行
    bool isRenderThreadRunning() const { return _renderThreadRunning.load(); }

    using RenderThreadCallback = std::function<void()>;
    void setRenderCallback(RenderThreadCallback callback);
    void invokeRenderCallback();

private:
    void renderThreadMain();

    bool _renderThreadEnabled = false;
    ayt::AtomicBool _renderThreadRunning{false};
    std::unique_ptr<ayt::Thread> _renderThread;
    RenderThreadCallback _renderCallback;

    // 信号量替代品（使用条件变量模拟）
    ayt::Mutex _logicMutex;
    ayt::Mutex _renderMutex;
    ayt::ConditionVariable _logicCond;
    ayt::ConditionVariable _renderCond;
    ayt::AtomicInt _logicSignal{0};
    ayt::AtomicInt _renderSignal{0};
};

} // namespace ayt::game