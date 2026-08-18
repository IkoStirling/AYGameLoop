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
    _renderCond.wait(lock, [this]() {
        return _renderCompleted > _renderConsumed || !_renderThreadRunning.load();
    });
    if (_renderCompleted > _renderConsumed) ++_renderConsumed;
}

void RenderThreadSync::signalRenderComplete() {
    if (!_renderThreadEnabled) return;

    // Hold the mutex while bumping the signal AND notifying, so the waiter
    // cannot miss the wake-up between releasing the mutex inside wait()
    // and entering the wait queue.
    {
        std::lock_guard<ayt::Mutex> lock(_renderMutex);
        ++_renderCompleted;
    }
    _renderCond.notifyAll();
}

void RenderThreadSync::signalLogicComplete() {
    if (!_renderThreadEnabled) return;

    {
        std::lock_guard<ayt::Mutex> lock(_logicMutex);
        ++_logicRequested;
    }
    _logicCond.notifyAll();
}

void RenderThreadSync::waitForLogicComplete() {
    if (!_renderThreadEnabled) return;

    std::unique_lock<ayt::Mutex> lock(_logicMutex);
    _logicCond.wait(lock, [this]() {
        return _logicRequested > _logicConsumed || !_renderThreadRunning.load();
    });
    if (_logicRequested > _logicConsumed) ++_logicConsumed;
}

void RenderThreadSync::setRenderThreadEnabled(bool enabled) {
    _renderThreadEnabled = enabled;
    if (!enabled) {
        stopRenderThread();
    }
}

void RenderThreadSync::startRenderThread() {
    if (!_renderThreadEnabled || _renderThreadRunning.load()) return;

    {
        std::lock_guard<ayt::Mutex> logicLock(_logicMutex);
        std::lock_guard<ayt::Mutex> renderLock(_renderMutex);
        _logicRequested = _logicConsumed = 0;
        _renderCompleted = _renderConsumed = 0;
    }
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

    // Wake either side if it is parked. Shutdown is a state transition, not
    // a synthetic frame request/completion.
    _logicCond.notifyAll();
    _renderCond.notifyAll();

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
