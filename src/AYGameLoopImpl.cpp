// AYGameLoopImpl.cpp - GameLoop 主类实现

#include <AYGameLoop/GameLoopImpl.h>
#include <AYGameLoop/GameLoopEvents.h>
#include <AYEventSystem/EventBus.h>
#include <AYTask.h>
#include <AYLog.h>
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
    ayt::log::info("[GameLoop] Initializing...");

    _frameManager.setTargetFPS(60.0f);
    _frameManager.setFixedTimestep(1.0f / 60.0f);
    _frameManager.setTimeScale(1.0f);

    if (_parallelEnabled.load()) {
        if (!_jobScheduler.initialize()) {
            ayt::log::error("[GameLoop] Failed to initialize job scheduler");
            return false;
        }
    }

    ayt::log::info("[GameLoop] Initialized successfully");
    return true;
}

void GameLoop::shutdown() {
    if (_shutdownComplete.load(std::memory_order_acquire)) {
        return;
    }

    bool expected = false;
    if (!_shutdownInProgress.compare_exchange_strong(expected, true)) {
        return;
    }

    ayt::log::info("[GameLoop] Shutting down...");

    // Stop request propagates to mainLoop.
    _shouldExit.store(true);

    if (_playSessionActive.load()) {
        _playSessionActive.store(false);
        _frameManager.setNoLimitFPS(false);
    }

    shutdownSubSystems();
    // NOTE: clearAll() does NOT destroy subsystems — their lifetime extends
    // to process exit. The registry only forgets the lookup keys. This
    // prevents use-after-free when static-init registrars (REGISTER_SUBSYSTEM)
    // tear down before GameLoop's static dtor runs.
    SubSystemRegistry::instance().clearAll();

    if (_parallelEnabled.load()) {
        _jobScheduler.shutdown();
    }

    // NOTE: do NOT call EventBus::instance().unsubscribeAll() here.
    // EventBus is a process-wide singleton that outlives GameLoop; its
    // listeners may be owned by other modules. Calling unsubscribeAll()
    // during GameLoop's static-deinit also races the EventBus singleton's
    // own destructor ordering. Cleanup of per-GameLoop-instance listeners
    // is the host application's responsibility — typically via ScopedConnection
    // or an explicit shutdown hook in AYApplication.

    _shutdownComplete.store(true, std::memory_order_release);
    ayt::log::info("[GameLoop] Shutdown complete");
}

bool GameLoop::initializeSubSystems() {
    auto systems = SubSystemRegistry::instance().getExecutionOrder();

    std::vector<ISubSystem*> initialized;

    for (ISubSystem* system : systems) {
        ayt::log::info("[GameLoop] Initializing subsystem: %s", system->getName());
        if (!system->initialize()) {
            ayt::log::error("[GameLoop] Failed to initialize subsystem: %s", system->getName());
            for (auto it = initialized.rbegin(); it != initialized.rend(); ++it) {
                (*it)->shutdown();
            }
            return false;
        }
        initialized.push_back(system);
    }

    _subSystemsRunning.store(true);
    return true;
}

void GameLoop::shutdownSubSystems() {
    if (!_subSystemsRunning.load()) {
        return;
    }

    auto systems = SubSystemRegistry::instance().getExecutionOrder();

    // Reverse-order teardown.
    for (auto it = systems.rbegin(); it != systems.rend(); ++it) {
        ISubSystem* system = *it;
        ayt::log::info("[GameLoop] Shutting down subsystem: %s", system->getName());
        system->shutdown();
    }

    _subSystemsRunning.store(false);
}

void GameLoop::run() {
    if (_playSessionActive.load()) {
        ayt::log::warn("[GameLoop] run() blocked while editor play session is active");
        return;
    }

    if (_running.load()) {
        ayt::log::warn("[GameLoop] Already running");
        return;
    }

    if (!initialize()) {
        ayt::log::error("[GameLoop] Failed to initialize");
        return;
    }

    if (!initializeSubSystems()) {
        ayt::log::error("[GameLoop] Failed to initialize subsystems");
        return;
    }

    _running.store(true);
    _shouldExit.store(false);
    _paused.store(false);
    _renderFrameInFlight = false;

    if (_sync.isRenderThreadEnabled()) {
        _sync.startRenderThread();
        // Spin until render thread is parked on waitForLogicComplete().
        // RenderThreadSync sets _renderThreadRunning BEFORE spawning, so
        // this is a brief spin (typically <1ms).
        while (!_sync.isRenderThreadRunning()) {
            ayt::Thread::sleep(0.001f);
        }
    }

    ayt::log::info("[GameLoop] Game loop started");

    mainLoop();

    _sync.stopRenderThread();
    _renderFrameInFlight = false;

    _running.store(false);
    ayt::log::info("[GameLoop] Game loop exited");
}

void GameLoop::stop() {
    // Set the stop flag observable at the next frame top. Do NOT touch
    // _running — mainLoop owns that transition on exit. Safe from any
    // thread; main thread preferred.
    _shouldExit.store(true);
    GameStoppedEvent event(_frameManager.getFrameCount());
    ayt::event::EventBus::instance().post(event);
    ayt::log::info("[GameLoop] Stop requested");
}

void GameLoop::pause() {
    _paused.store(true);
    GamePausedEvent event(_frameManager.getFrameCount());
    ayt::event::EventBus::instance().post(event);
    ayt::log::info("[GameLoop] Paused");
}

void GameLoop::resume() {
    _paused.store(false);
    GameResumedEvent event(_frameManager.getFrameCount());
    ayt::event::EventBus::instance().post(event);
    ayt::log::info("[GameLoop] Resumed");
}

void GameLoop::mainLoop() {
    while (_running.load() && !_shouldExit.load()) {
        if (_paused.load()) {
            // Drain render-thread-posted events while paused so consumers
            // see RenderFrame{Start,End} even during pause.
            ayt::event::EventBus::instance().pump();
            ayt::Thread::sleep(0.01f);
            continue;
        }

        tickOnceFrame();
    }
}

void GameLoop::tickOnceFrame(bool fixedStepDelta) {
    if (_sync.isRenderThreadEnabled() && _renderFrameInFlight) {
        _sync.waitForRenderComplete();
        _renderFrameInFlight = false;
    }

    // Drain deferred events posted by the render thread (RenderFrameStart/End).
    // Per AYEventSystem design §2.2, post is any-thread; pump must run on main.
    ayt::event::EventBus::instance().pump();

    if (fixedStepDelta) {
        _frameManager.beginFrameWithDelta(_frameManager.getFixedTimestep());
    } else {
        _frameManager.beginFrame();
    }

    float deltaTime = _frameManager.getDeltaTime();
    float unscaledDeltaTime = _frameManager.getUnscaledDeltaTime();
    uint64_t frameCount = _frameManager.getFrameCount();

    LogicFrameStartEvent startEvent(frameCount, unscaledDeltaTime);
    ayt::event::EventBus::instance().emit(startEvent);

    processFixedUpdates();
    processVariableUpdates();

    LogicFrameEndEvent endEvent(frameCount, deltaTime);
    ayt::event::EventBus::instance().emit(endEvent);

    submitRenderCommands();
    if (_sync.isRenderThreadEnabled()) {
        _sync.signalLogicComplete();
        _renderFrameInFlight = true;
    }

    if (!_playSessionActive.load()) {
        _frameManager.limitFrameRate();
    }
}

bool GameLoop::preparePlaySession() {
    if (_playSessionActive.load()) {
        return true;
    }

    if (_running.load()) {
        ayt::log::warn("[GameLoop] preparePlaySession blocked while run() is active");
        return false;
    }

    if (!initialize()) {
        ayt::log::error("[GameLoop] preparePlaySession initialize failed");
        return false;
    }

    if (!initializeSubSystems()) {
        ayt::log::error("[GameLoop] preparePlaySession subsystem init failed");
        return false;
    }

    _playSessionActive.store(true);
    _running.store(true);
    _shouldExit.store(false);
    _paused.store(false);
    _frameManager.setNoLimitFPS(true);
    _renderFrameInFlight = false;
    if (_sync.isRenderThreadEnabled()) {
        _sync.startRenderThread();
    }
    ayt::log::info("[GameLoop] Editor play session started");
    return true;
}

void GameLoop::endPlaySession() {
    if (!_playSessionActive.load()) {
        return;
    }

    _sync.stopRenderThread();
    _renderFrameInFlight = false;
    shutdownSubSystems();
    _playSessionActive.store(false);
    _running.store(false);
    _paused.store(false);
    _frameManager.setNoLimitFPS(false);
    ayt::log::info("[GameLoop] Editor play session ended");
}

void GameLoop::tickOnce() {
    if (!_playSessionActive.load() || _paused.load()) {
        return;
    }

    tickOnceFrame();
}

void GameLoop::stepOnce() {
    if (!_playSessionActive.load()) {
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
    const uint64_t frameCount = _frameManager.getFrameCount();

    for (int i = 0; i < fixedUpdateCount; ++i) {
        ++_simTick;
        SimTickEvent tickEvent(_simTick, fixedDt, frameCount);
        ayt::event::EventBus::instance().emit(tickEvent);

        for (ISubSystem* system : systems) {
            system->fixedUpdate(fixedDt);
        }
    }
    _frameManager.consumeFixedUpdates(fixedUpdateCount);
}

void GameLoop::processVariableUpdates() {
    float deltaTime = _frameManager.getDeltaTime();
    auto systems = SubSystemRegistry::instance().getExecutionOrder();

    if (_parallelEnabled.load() && _jobScheduler.isInitialized()) {
        auto layers = buildExecutionLayers();
        runVariableUpdatesParallel(layers);
    } else {
        for (ISubSystem* system : systems) {
            system->update(deltaTimeFor(*system));
        }
    }

    AY_LOCK_GUARD(_listenerMutex);
    for (const auto& listener : _updateListeners) {
        listener.callback(deltaTime);
    }
}

float GameLoop::deltaTimeFor(const ISubSystem& system) const {
    switch (system.getDescriptor().timeType) {
    case SubSystemDescriptor::TimeType::Scaled:
        return _frameManager.getScaledDeltaTime();
    case SubSystemDescriptor::TimeType::Unscaled:
        return _frameManager.getUnscaledDeltaTime();
    case SubSystemDescriptor::TimeType::Real:
        return _frameManager.getRealDeltaTime();
    }
    return _frameManager.getScaledDeltaTime();
}

// Build layer-by-layer DAG for parallel update.
//
// Layer 0 contains all subsystems with no unresolved dependencies.
// Layer N+1 contains subsystems whose dependencies are all satisfied by
// layers 0..N. Subsystems within a single layer run concurrently;
// layers run serially.
//
// Dependencies come from the override map (set via the descriptor overload
// of registerSubSystem) merged over each subsystem's getDescriptor().
std::vector<std::vector<ISubSystem*>> GameLoop::buildExecutionLayers() const {
    auto systems = SubSystemRegistry::instance().getExecutionOrder();

    // Effective descriptor for each registered system: override wins over getDescriptor().
    std::unordered_map<std::string, std::vector<std::string>> deps;
    for (ISubSystem* s : systems) {
        std::vector<std::string> merged;
        // Start with the subsystem's own dependencies.
        for (const char* d : s->getDescriptor().dependencies) {
            if (d) merged.emplace_back(d);
        }
        // Apply override (if any) — overrides REPLACE the subsystem's own list.
        auto overrideList = SubSystemRegistry::instance().getDependencyOverride(s->getName());
        if (overrideList) {
            merged.clear();
            for (const char* d : *overrideList) {
                if (d) merged.emplace_back(d);
            }
        }
        deps[s->getName()] = std::move(merged);
    }

    std::unordered_map<std::string, int> inDegree;
    std::unordered_map<std::string, std::vector<std::string>> adj;
    for (ISubSystem* s : systems) {
        inDegree[s->getName()] = 0;
    }
    for (ISubSystem* s : systems) {
        const std::string name = s->getName();
        for (const auto& d : deps[name]) {
            auto it = inDegree.find(d);
            if (it == inDegree.end()) continue; // dependency not registered; skip
            adj[d].push_back(name);
            inDegree[name]++;
        }
    }

    std::vector<std::vector<ISubSystem*>> layers;
    std::unordered_map<std::string, ISubSystem*> byName;
    for (ISubSystem* s : systems) byName[s->getName()] = s;

    auto zeroDegree = [&](const std::unordered_map<std::string, int>& deg) {
        std::vector<ISubSystem*> layer;
        for (ISubSystem* s : systems) {
            if (deg.at(s->getName()) == 0) layer.push_back(s);
        }
        return layer;
    };

    auto current = zeroDegree(inDegree);
    while (!current.empty()) {
        // Sort within layer by descending basePriority (higher first).
        std::sort(current.begin(), current.end(), [](ISubSystem* a, ISubSystem* b) {
            return a->getDescriptor().basePriority > b->getDescriptor().basePriority;
        });
        layers.push_back(current);

        std::unordered_map<std::string, int> nextDeg = inDegree;
        for (ISubSystem* s : current) {
            const std::string name = s->getName();
            for (const auto& nb : adj[name]) {
                nextDeg[nb]--;
            }
        }
        // Remove current layer from in-degree.
        for (ISubSystem* s : current) {
            nextDeg[s->getName()] = -1; // marker
        }
        current = zeroDegree(nextDeg);
        inDegree = std::move(nextDeg);
    }

    return layers;
}

// Run layers in series; within each layer, fan out subsystems to a TaskGraph
// that runs them in parallel (no intra-layer dependencies).
void GameLoop::runVariableUpdatesParallel(
    const std::vector<std::vector<ISubSystem*>>& layers) {
    for (const auto& layer : layers) {
        if (layer.empty()) continue;

        auto* graph = _jobScheduler.createGraph();
        if (!graph) {
            // Fallback: serial for this layer.
            for (ISubSystem* s : layer) s->update(deltaTimeFor(*s));
            continue;
        }

        // Emplace one node per subsystem, NO intra-layer dependencies → all parallel.
        std::vector<ayt::task::ITaskGraph::INode*> prevLayerNodes;
        for (ISubSystem* s : layer) {
            auto& node = graph->emplace(
                ayt::task::makeTask([this, s]() { s->update(deltaTimeFor(*s)); }),
                s->getName()
            );
            (void)node;
        }

        _jobScheduler.executeAndWait(graph);
        delete graph;
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
    _noLimitFPS.store(noLimit);
    _frameManager.setNoLimitFPS(noLimit);
}

void GameLoop::registerSubSystem(ISubSystem* system) {
    SubSystemRegistry::instance().registerSubSystem(system);
}

void GameLoop::registerSubSystem(ISubSystem* system,
                                 std::vector<const char*> dependencies,
                                 int32_t priority) {
    SubSystemRegistry::instance().registerSubSystem(system, std::move(dependencies), priority);
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
    _parallelEnabled.store(enabled);
    if (!enabled) {
        _jobScheduler.shutdown();
    } else if (!_jobScheduler.isInitialized()) {
        _jobScheduler.initialize();
    }
}

} // namespace ayt::game
