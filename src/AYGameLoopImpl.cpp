// AYGameLoopImpl.cpp - GameLoop 主类实现

#include <AYGameLoopImpl.h>
#include <AYTask.h>
#include <cstdio>

namespace ayt::game
{

GameLoop::GameLoop()
    : _parallelEnabled(false)
{
}

GameLoop::~GameLoop() {
    shutdown();
}

GameLoop& GameLoop::instance() {
    // Ensure registry outlives GameLoop (registry constructs first, destructs last).
    SubSystemRegistry::instance();
    static GameLoop loop;
    return loop;
}

bool GameLoop::initialize() {
    ::printf("[GameLoop] Initializing...\n");

    // 初始化帧管理器
    // 默认设置
    _frameManager.setTargetFPS(60.0f);
    _frameManager.setFixedTimestep(1.0f / 60.0f);
    _frameManager.setTimeScale(1.0f);

    // 初始化Job调度器
    if (_parallelEnabled) {
        if (!_jobScheduler.initialize()) {
            ::fprintf(stderr, "[GameLoop] Failed to initialize job scheduler\n");
            return false;
        }
    }

    ::printf("[GameLoop] Initialized successfully\n");
    return true;
}

void GameLoop::shutdown() {
    if (_shutdownComplete) {
        return;
    }

    ::printf("[GameLoop] Shutting down...\n");

    // 只在运行状态下调用 stop
    bool expectedRunning = true;
    if (_running.compare_exchange_strong(expectedRunning, false)) {
        _shouldExit.store(true);
    }

    if (_playSessionActive) {
        _playSessionActive = false;
        _frameManager.setNoLimitFPS(false);
    }

    // 关闭所有子系统
    shutdownSubSystems();
    SubSystemRegistry::instance().clearAll();

    // 关闭Job调度器
    if (_parallelEnabled) {
        _jobScheduler.shutdown();
    }

    // 清空事件监听器
    GAME_LOOP_EVENT_BUS.clear();

    _shutdownComplete = true;
    ::printf("[GameLoop] Shutdown complete\n");
}

bool GameLoop::initializeSubSystems() {
    auto systems = SubSystemRegistry::instance().getExecutionOrder();

    for (ISubSystem* system : systems) {
        ::printf("[GameLoop] Initializing subsystem: %s\n", system->getName());
        if (!system->initialize()) {
            ::fprintf(stderr, "[GameLoop] Failed to initialize subsystem: %s\n", system->getName());
            return false;
        }
    }

    _subSystemsRunning = true;
    return true;
}

void GameLoop::shutdownSubSystems() {
    if (!_subSystemsRunning) {
        return;
    }

    auto systems = SubSystemRegistry::instance().getExecutionOrder();

    // 逆序关闭
    for (auto it = systems.rbegin(); it != systems.rend(); ++it) {
        ISubSystem* system = *it;
        ::printf("[GameLoop] Shutting down subsystem: %s\n", system->getName());
        system->shutdown();
    }

    _subSystemsRunning = false;
}

void GameLoop::run() {
    if (_playSessionActive) {
        ::fprintf(stderr, "[GameLoop] run() blocked while editor play session is active\n");
        return;
    }

    if (_running.load()) {
        ::fprintf(stderr, "[GameLoop] Already running\n");
        return;
    }

    if (!initialize()) {
        ::fprintf(stderr, "[GameLoop] Failed to initialize\n");
        return;
    }

    if (!initializeSubSystems()) {
        ::fprintf(stderr, "[GameLoop] Failed to initialize subsystems\n");
        return;
    }

    _running.store(true);
    _shouldExit.store(false);
    _paused.store(false);

    // 如果启用了渲染线程，启动它
    if (_sync.isRenderThreadEnabled()) {
        _sync.startRenderThread();
        // 等待渲染线程准备好（进入等待状态）
        while (!_sync.isRenderThreadRunning()) {
            ayt::Thread::sleep(0.001f);  // 1ms
        }
    }

    ::printf("[GameLoop] Game loop started\n");

    mainLoop();

    // 等待渲染线程结束
    _sync.stopRenderThread();

    _running.store(false);
    ::printf("[GameLoop] Game loop exited\n");
}

void GameLoop::stop() {
    _shouldExit.store(true);
    _running.store(false);
    ::printf("[GameLoop] Stop requested\n");
}

void GameLoop::pause() {
    _paused.store(true);
    GamePausedEvent event(_frameManager.getFrameCount());
    GAME_LOOP_EVENT_BUS.emit(event);
    ::printf("[GameLoop] Paused\n");
}

void GameLoop::resume() {
    _paused.store(false);
    GameResumedEvent event(_frameManager.getFrameCount());
    GAME_LOOP_EVENT_BUS.emit(event);
    ::printf("[GameLoop] Resumed\n");
}

void GameLoop::mainLoop() {
    while (_running.load() && !_shouldExit.load()) {
        if (_paused.load()) {
            ayt::Thread::sleep(0.01f);
            continue;
        }

        tickOnceFrame();
    }
}

void GameLoop::tickOnceFrame(bool fixedStepDelta) {
    if (_sync.isRenderThreadEnabled()) {
        _sync.waitForRenderComplete();
    }

    if (fixedStepDelta) {
        _frameManager.beginFrameWithDelta(_frameManager.getFixedTimestep());
    } else {
        _frameManager.beginFrame();
    }

    float deltaTime = _frameManager.getDeltaTime();
    float unscaledDeltaTime = _frameManager.getUnscaledDeltaTime();
    uint64_t frameCount = _frameManager.getFrameCount();

    LogicFrameStartEvent startEvent(frameCount, unscaledDeltaTime);
    GAME_LOOP_EVENT_BUS.emit(startEvent);

    processFixedUpdates();
    processVariableUpdates();

    LogicFrameEndEvent endEvent(frameCount, deltaTime);
    GAME_LOOP_EVENT_BUS.emit(endEvent);

    submitRenderCommands();
    if (_sync.isRenderThreadEnabled()) {
        _sync.signalLogicComplete();
    }

    if (!_playSessionActive) {
        _frameManager.limitFrameRate();
    }
}

bool GameLoop::preparePlaySession() {
    if (_playSessionActive) {
        return true;
    }

    if (_running.load()) {
        ::fprintf(stderr, "[GameLoop] preparePlaySession blocked while run() is active\n");
        return false;
    }

    if (!initialize()) {
        ::fprintf(stderr, "[GameLoop] preparePlaySession initialize failed\n");
        return false;
    }

    if (!initializeSubSystems()) {
        ::fprintf(stderr, "[GameLoop] preparePlaySession subsystem init failed\n");
        return false;
    }

    _playSessionActive = true;
    _running.store(true);
    _shouldExit.store(false);
    _paused.store(false);
    _frameManager.setNoLimitFPS(true);
    ::printf("[GameLoop] Editor play session started\n");
    return true;
}

void GameLoop::endPlaySession() {
    if (!_playSessionActive) {
        return;
    }

    shutdownSubSystems();
    _playSessionActive = false;
    _running.store(false);
    _paused.store(false);
    _frameManager.setNoLimitFPS(false);
    ::printf("[GameLoop] Editor play session ended\n");
}

void GameLoop::tickOnce() {
    if (!_playSessionActive || _paused.load()) {
        return;
    }

    tickOnceFrame();
}

void GameLoop::stepOnce() {
    if (!_playSessionActive) {
        return;
    }

    const bool wasPaused = _paused.load();
    _paused.store(false);
    tickOnceFrame(true);
    _paused.store(wasPaused);
}

void GameLoop::processFixedUpdates() {
    auto systems = SubSystemRegistry::instance().getExecutionOrder();
    int fixedUpdateCount = _frameManager.getFixedUpdateCount();
    float fixedDt = _frameManager.getFixedTimestep();

    for (int i = 0; i < fixedUpdateCount; ++i) {
        for (ISubSystem* system : systems) {
            system->fixedUpdate(fixedDt);
        }
    }
}

void GameLoop::processVariableUpdates() {
    float deltaTime = _frameManager.getDeltaTime();
    auto systems = SubSystemRegistry::instance().getExecutionOrder();

    if (_parallelEnabled && _jobScheduler.isInitialized()) {
        // 并行执行模式：使用TaskGraph
        auto graph = _jobScheduler.createGraph();
        if (graph) {
            ayt::task::ITaskGraph::INode* prevNode = nullptr;

            for (ISubSystem* system : systems) {
                auto& node = graph->emplace(
                    ayt::task::makeTask([system, deltaTime]() { system->update(deltaTime); }),
                    system->getName()
                );

                if (prevNode) {
                    graph->depend(node, *prevNode);
                }
                prevNode = &node;
            }

            _jobScheduler.executeAndWait(graph);
            delete graph;
        } else {
            // 回退到串行执行
            for (ISubSystem* system : systems) {
                system->update(deltaTime);
            }
        }
    } else {
        // 串行执行模式
        for (ISubSystem* system : systems) {
            system->update(deltaTime);
        }
    }

    // 调用注册的回调
    AY_LOCK_GUARD(_listenerMutex);
    for (const auto& listener : _updateListeners) {
        listener.callback(deltaTime);
    }
}

void GameLoop::submitRenderCommands() {
    if (!_sync.isRenderThreadEnabled()) {
        _sync.invokeRenderCallback();
    }
}

void GameLoop::setTargetFPS(float fps) {
    _frameManager.setTargetFPS(fps);
}

void GameLoop::setFixedTimestep(float dt) {
    _frameManager.setFixedTimestep(dt);
}

void GameLoop::setTimeScale(float scale) {
    _frameManager.setTimeScale(scale);
}

void GameLoop::setNoLimitFPS(bool noLimit) {
    _noLimitFPS = noLimit;
    _frameManager.setNoLimitFPS(noLimit);
}

void GameLoop::registerSubSystem(ISubSystem* system) {
    SubSystemRegistry::instance().registerSubSystem(system);
}

void GameLoop::unregisterSubSystem(const char* name) {
    SubSystemRegistry::instance().unregisterSubSystem(name);
}

uint64_t GameLoop::onUpdate(UpdateCallback callback) {
    AY_LOCK_GUARD(_listenerMutex);
    uint64_t id = _nextListenerId++;
    _updateListeners.push_back({id, std::move(callback)});
    return id;
}

void GameLoop::offUpdate(uint64_t listenerId) {
    AY_LOCK_GUARD(_listenerMutex);
    _updateListeners.erase(
        std::remove_if(_updateListeners.begin(), _updateListeners.end(),
            [listenerId](const UpdateListener& l) { return l.id == listenerId; }),
        _updateListeners.end()
    );
}

void GameLoop::setRenderThreadEnabled(bool enabled) {
    _sync.setRenderThreadEnabled(enabled);
}

void GameLoop::setRenderCallback(RenderThreadSync::RenderThreadCallback callback) {
    _sync.setRenderCallback(std::move(callback));
}

void GameLoop::setParallelEnabled(bool enabled) {
    _parallelEnabled = enabled;
}

} // namespace ayt::game