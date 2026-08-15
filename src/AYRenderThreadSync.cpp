// RenderThreadSync.cpp - 渲染线程同步实现

#include <AYGameLoop/RenderThreadSync.h>
#include <AYGameLoop/GameLoopEvents.h>
#include <AYEventSystem/EventBus.h>
#include <AYLog.h>
#include <cstdio>

namespace ayt::game
{

RenderThreadSync::RenderThreadSync() {
}

RenderThreadSync::~RenderThreadSync() {
    stopRenderThread();
}

void RenderThreadSync::waitForRenderComplete() {
    if (!_renderThreadEnabled) return;

    std::unique_lock<ayt::Mutex> lock(_renderMutex);
    int currentSignal = _renderSignal.load();
    _renderCond.wait(lock, [this, currentSignal]() {
        return _renderSignal.load() != currentSignal;
    });
}

void RenderThreadSync::signalRenderComplete() {
    if (!_renderThreadEnabled) return;

    // Hold the mutex while bumping the signal AND notifying, so the waiter
    // cannot miss the wake-up between releasing the mutex inside wait()
    // and entering the wait queue.
    {
        std::lock_guard<ayt::Mutex> lock(_renderMutex);
        _renderSignal.fetchAdd(1);
    }
    _renderCond.notifyAll();
}

void RenderThreadSync::signalLogicComplete() {
    if (!_renderThreadEnabled) return;

    {
        std::lock_guard<ayt::Mutex> lock(_logicMutex);
        _logicSignal.fetchAdd(1);
    }
    _logicCond.notifyAll();
}

void RenderThreadSync::waitForLogicComplete() {
    if (!_renderThreadEnabled) return;

    std::unique_lock<ayt::Mutex> lock(_logicMutex);
    int currentSignal = _logicSignal.load();
    _logicCond.wait(lock, [this, currentSignal]() {
        return _logicSignal.load() != currentSignal;
    });
}

void RenderThreadSync::setRenderThreadEnabled(bool enabled) {
    _renderThreadEnabled = enabled;
    if (!enabled) {
        stopRenderThread();
    }
}

void RenderThreadSync::startRenderThread() {
    if (!_renderThreadEnabled || _renderThreadRunning.load()) return;

    _renderThreadRunning.store(true);
    _renderThread = std::make_unique<ayt::Thread>(
        [this]() { renderThreadMain(); },
        "RenderThread"
    );
    ayt::log::info("[RenderThreadSync] Render thread started");
}

void RenderThreadSync::stopRenderThread() {
    if (!_renderThreadRunning.load()) return;

    _renderThreadRunning.store(false);

    // Wake the render thread if it's parked in waitForLogicComplete.
    signalLogicComplete();

    if (_renderThread && _renderThread->isJoinable()) {
        _renderThread->join();
    }
    ayt::log::info("[RenderThreadSync] Render thread stopped");
}

void RenderThreadSync::setRenderCallback(RenderThreadCallback callback) {
    _renderCallback = std::move(callback);
}

void RenderThreadSync::invokeRenderCallback() {
    if (_renderCallback) {
        _renderCallback();
    }
}

void RenderThreadSync::renderThreadMain() {
    ayt::log::info("[RenderThreadSync] Render thread enter");

    uint64_t renderFrameCount = 0;

    while (_renderThreadRunning.load()) {
        waitForLogicComplete();

        if (!_renderThreadRunning.load()) break;

        // Post events (render thread is not the EventBus main thread; per
        // AYEventSystem design §2.2, post() is any-thread-safe).
        RenderFrameStartEvent startEvent(renderFrameCount);
        ayt::event::EventBus::instance().post(startEvent);

        if (_renderCallback) {
            _renderCallback();
        }

        RenderFrameEndEvent endEvent(renderFrameCount);
        ayt::event::EventBus::instance().post(endEvent);

        signalRenderComplete();
        renderFrameCount++;
    }

    ayt::log::info("[RenderThreadSync] Render thread exit");
}

} // namespace ayt::game