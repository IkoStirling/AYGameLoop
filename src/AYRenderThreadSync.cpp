// RenderThreadSync.cpp - 渲染线程同步实现

#include <AYRenderThreadSync.h>
#include <AYGameLoopEvents.h>
#include <ayevent/EventBus.h>
#include <cstdio>

namespace ayt::game
{

RenderThreadSync::RenderThreadSync() {
    _logicSignal.store(0);
    _renderSignal.store(0);
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

    _renderSignal.fetchAdd(1);
    _renderCond.notifyAll();
}

void RenderThreadSync::signalLogicComplete() {
    if (!_renderThreadEnabled) return;

    _logicSignal.fetchAdd(1);
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
    ::printf("[RenderThreadSync] Render thread started\n");
}

void RenderThreadSync::stopRenderThread() {
    if (!_renderThreadRunning.load()) return;

    _renderThreadRunning.store(false);

    // 通知渲染线程退出
    signalLogicComplete();

    if (_renderThread && _renderThread->isJoinable()) {
        _renderThread->join();
    }
    ::printf("[RenderThreadSync] Render thread stopped\n");
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
    ::printf("[RenderThreadSync] Render thread enter\n");

    uint64_t renderFrameCount = 0;

    while (_renderThreadRunning.load()) {
        waitForLogicComplete();

        if (!_renderThreadRunning.load()) break;

        // 发布渲染帧开始事件（render thread → post，让 main thread 在 pump 时分发）
        RenderFrameStartEvent startEvent(renderFrameCount);
        ayt::event::EventBus::instance().post(startEvent);

        if (_renderCallback) {
            _renderCallback();
        }

        // 发布渲染帧结束事件
        RenderFrameEndEvent endEvent(renderFrameCount);
        ayt::event::EventBus::instance().post(endEvent);

        signalRenderComplete();
        renderFrameCount++;
    }

    ::printf("[RenderThreadSync] Render thread exit\n");
}

} // namespace ayt::game