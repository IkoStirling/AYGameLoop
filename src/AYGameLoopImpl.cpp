// AYGameLoopImpl.cpp - GameLoop 主类实现

#include <AYGameLoop/GameLoopImpl.h>
#include <AYGameLoop/GameLoopEvents.h>
#include <AYEventSystem/EventBus.h>
#include <AYTask.h>
#include <AYLog.h>
#include <AYTime/Clock.h>
#include <cstdio>
#include <unordered_set>

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
    _frameManager.setTimeScale(1.0f);
    // A new session starts with no wall-clock debt from the previous World.
    // Numeric fixed-step configuration is retained; simTick remains monotonic.
    _frameManager.reset();

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
    SubSystemRegistry::instance().clearAll();
    {
        AY_LOCK_GUARD(_subSystemMutationMutex);
        _pendingSubSystemMutations.clear();
    }

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
    // From this point until shutdown, cross-thread mutations are deferred to
    // frame boundaries. This closes the initialization window where a direct
    // registry edit could invalidate the order being traversed below.
    _deferSubSystemMutations.store(true, std::memory_order_release);
    auto systems = SubSystemRegistry::instance().getExecutionOrder();

    if (systems.empty() && SubSystemRegistry::instance().getCount() != 0) {
        ayt::log::error("[GameLoop] Refusing to initialize an invalid subsystem graph");
        _deferSubSystemMutations.store(false, std::memory_order_release);
        return false;
    }

    // Validate lifecycle and per-phase DAGs before causing subsystem side
    // effects. A missing strict dependency or phase cycle is an init error,
    // not an empty phase that can be silently skipped at runtime.
    _phasePlansDirty.store(true);
    if (!rebuildPhasePlans()) {
        ayt::log::error("[GameLoop] Refusing to initialize an invalid frame phase graph");
        _deferSubSystemMutations.store(false, std::memory_order_release);
        return false;
    }

    std::vector<ISubSystem*> initialized;

    for (ISubSystem* system : systems) {
        ayt::log::info("[GameLoop] Initializing subsystem: %s", system->getName());
        if (!system->initialize()) {
            ayt::log::error("[GameLoop] Failed to initialize subsystem: %s", system->getName());
            for (auto it = initialized.rbegin(); it != initialized.rend(); ++it) {
                (*it)->shutdown();
            }
            _deferSubSystemMutations.store(false, std::memory_order_release);
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
    _deferSubSystemMutations.store(false, std::memory_order_release);
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

    if (_presentationOwnership == PresentationOwnership::GameLoop
        && _sync.isRenderThreadEnabled()) {
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
        tickOnceFrame();
    }
}

void GameLoop::tickOnceFrame(bool fixedStepDelta,
                             const HostedFrameContext* hostedFrame) {
    if (_frameInProgress) return;
    if (_pendingNativeFixedTimestep) {
        _frameManager.setFixedTimestep(*_pendingNativeFixedTimestep);
        _pendingNativeFixedTimestep.reset();
    }
    struct FrameGuard {
        bool& active;
        explicit FrameGuard(bool& value) : active(value) { active = true; }
        ~FrameGuard() { active = false; }
    } guard(_frameInProgress);
    if (_presentationOwnership == PresentationOwnership::GameLoop
        && _sync.isRenderThreadEnabled()
        && _renderFrameInFlight) {
        _sync.waitForRenderComplete();
        _renderFrameInFlight = false;
    }

    // The previous frame (including parallel jobs/render consumption) is now
    // quiescent. This is the sole mutation point for the active registry and
    // cached phase plans.
    applyPendingSubSystemMutations();
    if (_shouldExit.load()) return;

    // Drain deferred events posted by the render thread (RenderFrameStart/End).
    // Per AYEventSystem design §2.2, post is any-thread; pump must run on main.
    ayt::event::EventBus::instance().pump();

    _hostFrameIndex = hostedFrame != nullptr ? hostedFrame->hostFrameIndex : 0;
    _inputFrameIndex = hostedFrame != nullptr ? hostedFrame->inputFrameIndex : 0;

    const bool paused = _paused.load();
    if (fixedStepDelta) {
        _frameManager.beginFrameWithDelta(_frameManager.getFixedTimestep(), false, !paused);
    } else if (hostedFrame != nullptr) {
        // The editor sampled this duration for the same OS/input boundary.
        // Apply normal time scale here; only stepOnce intentionally bypasses it.
        _frameManager.beginFrameWithDelta(hostedFrame->realWallDeltaTime,
                                          true,
                                          !paused);
    } else {
        _frameManager.beginFrame(!paused);
    }

    float deltaTime = paused ? 0.0f : _frameManager.getScaledDeltaTime();
    float unscaledDeltaTime = _frameManager.getUnscaledDeltaTime();
    uint64_t frameCount = _frameManager.getFrameCount();

    LogicFrameStartEvent startEvent(frameCount, unscaledDeltaTime);
    ayt::event::EventBus::instance().emit(startEvent);

    if (!runPhase(FramePhase::Platform, paused)) return;

    // Platform producers post input/window edges. Deliver them before
    // Ingress so every same-frame consumer sees a stable input snapshot.
    ayt::event::EventBus::instance().pump();
    if (!runPhase(FramePhase::Ingress, paused)) return;

    _fixedTicksLastFrame = 0;
    if (!paused) {
        processFixedUpdates();
    }
    if (_shouldExit.load()) return;
    processVariableUpdates(paused);
    if (_shouldExit.load()) return;

    // Presentation and egress notifications are delivered at the final
    // main-thread checkpoint before the immutable render packet is submitted.
    ayt::event::EventBus::instance().pump();

    LogicFrameEndEvent endEvent(frameCount, deltaTime);
    ayt::event::EventBus::instance().emit(endEvent);

    submitRenderCommands();
    if (_presentationOwnership == PresentationOwnership::GameLoop
        && _sync.isRenderThreadEnabled()) {
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
    _presentationOwnership = PresentationOwnership::GameLoop;
    ayt::log::info("[GameLoop] Editor play session ended");
}

bool GameLoop::prepareHostedSession() {
    return preparePlaySession();
}

void GameLoop::endHostedSession() {
    endPlaySession();
}

void GameLoop::tickOnce() {
    if (!_playSessionActive.load()) {
        return;
    }

    tickOnceFrame();
}

void GameLoop::tickHostedFrame(const HostedFrameContext& context) {
    if (!_playSessionActive.load()) {
        return;
    }

    tickOnceFrame(false, &context);
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
    int fixedUpdateCount = _frameManager.getFixedUpdateCount();
    float fixedDt = _frameManager.getFixedTimestep();
    const uint64_t frameCount = _frameManager.getFrameCount();
    int completedCount = 0;
    _fixedTicksLastFrame = 0;

    for (int i = 0; i < fixedUpdateCount; ++i) {
        _pendingSimTick = _simTick + 1;
        if (!runPhase(FramePhase::FixedPrePhysics, false)
            || !runPhase(FramePhase::FixedPhysics, false)
            || !runPhase(FramePhase::FixedPostPhysics, false)) {
            _pendingSimTick = 0;
            break;
        }

        _simTick = _pendingSimTick;
        _pendingSimTick = 0;
        ++completedCount;

        SimTickEvent tickEvent(_simTick, fixedDt, frameCount);
        tickEvent.fixedStep = _frameManager.getFixedStep();
        ayt::event::EventBus::instance().emit(tickEvent);
        ayt::event::EventBus::instance().pump();
    }
    _fixedTicksLastFrame = static_cast<uint32_t>(completedCount);
    _frameManager.consumeFixedUpdates(completedCount);
}

void GameLoop::processVariableUpdates(bool paused) {
    float deltaTime = _frameManager.getScaledDeltaTime();

    if (!paused) {
        if (!runPhase(FramePhase::Gameplay, false)) return;

        std::vector<UpdateListener> listeners;
        {
            AY_LOCK_GUARD(_listenerMutex);
            listeners = _updateListeners;
        }
        for (const auto& listener : listeners) {
            listener.callback(deltaTime);
        }

        if (!runPhase(FramePhase::World, false)) return;
    }

    if (!runPhase(FramePhase::Presentation, paused)) return;
    (void)runPhase(FramePhase::Egress, paused);
}

PhaseMask GameLoop::effectivePhases(const ISubSystem& system) const {
    const PhaseMask explicitMask = system.getDescriptor().phases;
    if (explicitMask != 0) return explicitMask;
    return phaseBit(FramePhase::FixedPrePhysics) | phaseBit(FramePhase::Gameplay);
}

FrameContext GameLoop::frameContextFor(FramePhase phase,
                                       const ISubSystem& system,
                                       bool paused) const {
    FrameContext context;
    context.phase = phase;
    context.frameCount = _frameManager.getFrameCount();
    context.simTick = _pendingSimTick != 0 ? _pendingSimTick : _simTick;
    context.hostFrameIndex = _hostFrameIndex;
    context.inputFrameIndex = _inputFrameIndex;
    context.fixedDeltaTime = _frameManager.getFixedTimestep();
    context.fixedStep = _frameManager.getFixedStep();
    context.gameDeltaTime = paused ? 0.0f : _frameManager.getScaledDeltaTime();
    context.unscaledDeltaTime = _frameManager.getUnscaledDeltaTime();
    context.realWallDeltaTime = _frameManager.getRealDeltaTime();
    context.interpolationAlpha = _frameManager.getInterpolationFactor();
    context.paused = paused;
    context.deltaTime = isFixedPhase(phase)
        ? context.fixedDeltaTime
        : deltaTimeFor(system, paused);
    return context;
}

bool GameLoop::runPhase(FramePhase phase, bool paused) {
    if (_phasePlansDirty.load() && !rebuildPhasePlans()) {
        ayt::log::error("[GameLoop] Fatal: frame phase graph became invalid at phase %u",
                        static_cast<unsigned>(phase));
        _shouldExit.store(true);
        return false;
    }
    const auto index = static_cast<size_t>(phase);
    const uint64_t startUs = ayt::time::Clock::performanceNowUs();
    const bool succeeded = runPhaseLayers(phase, _phasePlans[index], paused);
    const uint64_t endUs = ayt::time::Clock::performanceNowUs();
    const uint64_t elapsedUs = endUs >= startUs ? endUs - startUs : 0;
    PhaseTimingStats& timing = _phaseTiming[index];
    ++timing.invocationCount;
    timing.totalTimeUs += elapsedUs;
    timing.lastTimeUs = elapsedUs;
    timing.maxTimeUs = std::max(timing.maxTimeUs, elapsedUs);
    if (!succeeded) {
        ayt::log::error("[GameLoop] Fatal: subsystem failed in phase %u",
                        static_cast<unsigned>(phase));
        _shouldExit.store(true);
    }
    return succeeded;
}

bool GameLoop::rebuildPhasePlans() {
    // Clear the flag before rebuilding so a concurrent registration that
    // happens during this pass leaves it set for the next phase boundary.
    _phasePlansDirty.store(false);
    std::array<std::vector<std::vector<ISubSystem*>>,
               static_cast<size_t>(FramePhase::Count)> newPlans;
    const auto allSystems = SubSystemRegistry::instance().getExecutionOrder();

    for (size_t i = 0; i < static_cast<size_t>(FramePhase::Count); ++i) {
        const auto phase = static_cast<FramePhase>(i);
        size_t expected = 0;
        for (ISubSystem* system : allSystems) {
            if (hasPhase(effectivePhases(*system), phase)) ++expected;
        }

        newPlans[i] = buildExecutionLayers(phase);
        size_t scheduled = 0;
        for (const auto& layer : newPlans[i]) scheduled += layer.size();
        if (scheduled != expected) {
            _phasePlansDirty.store(true);
            return false;
        }
    }

    _phasePlans = std::move(newPlans);
    return true;
}

float GameLoop::deltaTimeFor(const ISubSystem& system, bool paused) const {
    const auto& descriptor = system.getDescriptor();
    ClockDomain clock = descriptor.clock;
    if (clock == ClockDomain::Legacy) {
        switch (descriptor.timeType) {
        case SubSystemDescriptor::TimeType::Scaled:   clock = ClockDomain::Game; break;
        case SubSystemDescriptor::TimeType::Unscaled: clock = ClockDomain::Unscaled; break;
        case SubSystemDescriptor::TimeType::Real:     clock = ClockDomain::RealWall; break;
        }
    }

    switch (clock) {
    case ClockDomain::Game:
        return paused ? 0.0f : _frameManager.getScaledDeltaTime();
    case ClockDomain::Unscaled:
        return _frameManager.getUnscaledDeltaTime();
    case ClockDomain::RealWall:
        return _frameManager.getRealDeltaTime();
    case ClockDomain::Legacy:
        break;
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
std::vector<std::vector<ISubSystem*>> GameLoop::buildExecutionLayers(FramePhase phase) const {
    auto allSystems = SubSystemRegistry::instance().getExecutionOrder();
    std::vector<ISubSystem*> systems;
    for (ISubSystem* system : allSystems) {
        if (hasPhase(effectivePhases(*system), phase)) systems.push_back(system);
    }

    // Effective descriptor for each registered system: override wins over getDescriptor().
    std::unordered_map<std::string, std::vector<std::string>> deps;
    for (ISubSystem* s : systems) {
        std::vector<std::string> merged;
        const auto& descriptor = s->getDescriptor();
        const auto& phaseDeps = descriptor.phases == 0
            ? descriptor.dependencies
            : descriptor.runsAfter;
        for (const char* d : phaseDeps) {
            if (d) merged.emplace_back(d);
        }
        // Registration-time overrides apply to legacy systems only.
        auto overrideList = SubSystemRegistry::instance().getDependencyOverride(s->getName());
        if (descriptor.phases == 0 && overrideList) {
            merged.clear();
            for (const std::string& d : *overrideList) {
                merged.push_back(d);
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
            if (it == inDegree.end()) {
                if (s->getDescriptor().phases == 0) continue;

                ISubSystem* dependency = nullptr;
                for (ISubSystem* candidate : allSystems) {
                    if (d == candidate->getName()) {
                        dependency = candidate;
                        break;
                    }
                }
                if (!dependency) {
                    ayt::log::error("[GameLoop] phase dependency '%s' for '%s' is not registered",
                                    d.c_str(), name.c_str());
                    return {};
                }

                bool satisfiedByEarlierPhase = false;
                for (uint8_t p = 0; p < static_cast<uint8_t>(phase); ++p) {
                    if (hasPhase(effectivePhases(*dependency), static_cast<FramePhase>(p))) {
                        satisfiedByEarlierPhase = true;
                        break;
                    }
                }
                if (!satisfiedByEarlierPhase) {
                    ayt::log::error("[GameLoop] phase dependency '%s' for '%s' points to a later phase",
                                    d.c_str(), name.c_str());
                    return {};
                }
                continue;
            }
            adj[d].push_back(name);
            inDegree[name]++;
        }
    }

    std::vector<std::vector<ISubSystem*>> layers;

    auto zeroDegree = [&](const std::unordered_map<std::string, int>& deg) {
        std::vector<ISubSystem*> layer;
        for (ISubSystem* s : systems) {
            if (deg.at(s->getName()) == 0) layer.push_back(s);
        }
        return layer;
    };

    auto current = zeroDegree(inDegree);
    while (!current.empty()) {
        // Priority is only a deterministic tie-breaker inside this phase.
        std::sort(current.begin(), current.end(), [](ISubSystem* a, ISubSystem* b) {
            const auto& ad = a->getDescriptor();
            const auto& bd = b->getDescriptor();
            const int32_t ap = ad.phases == 0 ? ad.basePriority : ad.phasePriority;
            const int32_t bp = bd.phases == 0 ? bd.basePriority : bd.phasePriority;
            if (ap != bp) return ap > bp;
            return std::string(a->getName()) < std::string(b->getName());
        });
        // Split this dependency layer into hazard-free batches. This keeps
        // independent systems parallel while serializing write/write and
        // read/write access without inventing fragile name dependencies.
        std::vector<std::vector<ISubSystem*>> hazardBatches;
        const auto conflicts = [](const ISubSystem& a, const ISubSystem& b) {
            const auto& ad = a.getDescriptor();
            const auto& bd = b.getDescriptor();
            const auto intersects = [](const std::vector<const char*>& lhs,
                                       const std::vector<const char*>& rhs) {
                for (const char* l : lhs) {
                    if (l == nullptr) continue;
                    for (const char* r : rhs) {
                        if (r != nullptr && std::string(l) == r) return true;
                    }
                }
                return false;
            };
            return intersects(ad.writes, bd.writes)
                || intersects(ad.writes, bd.reads)
                || intersects(ad.reads, bd.writes);
        };
        for (ISubSystem* system : current) {
            // A resource conflict is an ordering edge in the already-sorted
            // priority/name order. Never place a later conflicting system in
            // an earlier batch merely because that batch has spare capacity.
            size_t earliestBatch = 0;
            for (size_t i = 0; i < hazardBatches.size(); ++i) {
                if (std::any_of(hazardBatches[i].begin(), hazardBatches[i].end(),
                        [&](ISubSystem* other) { return conflicts(*system, *other); })) {
                    earliestBatch = i + 1;
                }
            }

            bool placed = false;
            for (size_t i = earliestBatch; i < hazardBatches.size(); ++i) {
                const bool blocked = std::any_of(
                    hazardBatches[i].begin(), hazardBatches[i].end(),
                    [&](ISubSystem* other) { return conflicts(*system, *other); });
                if (!blocked) {
                    hazardBatches[i].push_back(system);
                    placed = true;
                    break;
                }
            }
            if (!placed) hazardBatches.push_back({system});
        }
        for (auto& batch : hazardBatches) layers.push_back(std::move(batch));

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

    size_t scheduled = 0;
    for (const auto& layer : layers) scheduled += layer.size();
    if (scheduled != systems.size()) {
        ayt::log::error("[GameLoop] cycle detected in frame phase %u",
                        static_cast<unsigned>(phase));
        return {};
    }
    return layers;
}

// Run layers in series; within each layer, fan out subsystems to a TaskGraph
// that runs them in parallel (no intra-layer dependencies).
bool GameLoop::runPhaseLayers(
    FramePhase phase,
    const std::vector<std::vector<ISubSystem*>>& layers,
    bool paused) {
    for (const auto& layer : layers) {
        if (layer.empty()) continue;

        if (!_parallelEnabled.load() || !_jobScheduler.isInitialized()) {
            for (ISubSystem* system : layer) {
                if (!system->tickChecked(
                        phase, frameContextFor(phase, *system, paused))) {
                    ayt::log::error("[GameLoop] Subsystem '%s' failed",
                                    system->getName());
                    return false;
                }
            }
            continue;
        }

        auto* graph = _jobScheduler.createGraph();
        if (!graph) {
            for (ISubSystem* system : layer) {
                if (!system->tickChecked(
                        phase, frameContextFor(phase, *system, paused))) {
                    ayt::log::error("[GameLoop] Subsystem '%s' failed",
                                    system->getName());
                    return false;
                }
            }
            continue;
        }

        std::atomic<bool> layerSucceeded{true};
        // Emplace one node per subsystem, NO intra-layer dependencies → all parallel.
        for (ISubSystem* s : layer) {
            const FrameContext context = frameContextFor(phase, *s, paused);
            auto& node = graph->emplace(
                ayt::task::makeTask([s, phase, context, &layerSucceeded]() {
                    if (!s->tickChecked(phase, context)) {
                        layerSucceeded.store(false, std::memory_order_release);
                    }
                }),
                s->getName()
            );
            (void)node;
        }

        _jobScheduler.executeAndWait(graph);
        delete graph;
        if (!layerSucceeded.load(std::memory_order_acquire)) return false;
    }
    return true;
}

void GameLoop::submitRenderCommands() {
    if (_presentationOwnership != PresentationOwnership::GameLoop) {
        return;
    }

    if (!_sync.isRenderThreadEnabled()) {
        _sync.invokeRenderCallback();
    }
}

bool GameLoop::hasStrictDependent(const std::string& name) const {
    auto& registry = SubSystemRegistry::instance();
    const auto systems = registry.getExecutionOrder();
    for (const ISubSystem* system : systems) {
        if (system == nullptr || name == system->getName()) continue;
        const auto& descriptor = system->getDescriptor();
        const auto mentions = [&name](const std::vector<const char*>& dependencies) {
            return std::any_of(dependencies.begin(), dependencies.end(),
                [&name](const char* dependency) {
                    return dependency != nullptr && name == dependency;
                });
        };
        const auto overrideDependencies =
            registry.getDependencyOverride(system->getName());
        const bool overrideMentions = overrideDependencies
            && std::find(overrideDependencies->begin(), overrideDependencies->end(), name)
                != overrideDependencies->end();
        if (overrideMentions || mentions(descriptor.initializeAfter)
            || (descriptor.phases != 0 && mentions(descriptor.runsAfter))) {
            return true;
        }
    }
    return false;
}

void GameLoop::applyPendingSubSystemMutations() {
    std::vector<PendingSubSystemMutation> mutations;
    {
        AY_LOCK_GUARD(_subSystemMutationMutex);
        if (_pendingSubSystemMutations.empty()) return;
        mutations.swap(_pendingSubSystemMutations);
    }

    auto& registry = SubSystemRegistry::instance();

    // Unpublish removals first. No phase/job is running at this boundary, and
    // the plan is rebuilt before shutdown/destruction.
    std::unordered_set<std::string> pendingRemovals;
    for (auto& mutation : mutations) {
        if (mutation.kind != PendingSubSystemMutation::Kind::Remove) continue;
        if (!mutation.name.empty()) pendingRemovals.insert(mutation.name);
    }

    std::vector<std::unique_ptr<ISubSystem>> removed;
    bool madeProgress = true;
    while (madeProgress && !pendingRemovals.empty()) {
        madeProgress = false;
        const auto executionOrder = registry.getExecutionOrder();
        for (auto it = executionOrder.rbegin(); it != executionOrder.rend(); ++it) {
            const std::string name = (*it)->getName();
            auto pending = pendingRemovals.find(name);
            if (pending == pendingRemovals.end() || hasStrictDependent(name)) continue;
            if (auto system = registry.takeSubSystem(name.c_str())) {
                removed.push_back(std::move(system));
                pendingRemovals.erase(pending);
                madeProgress = true;
            }
        }
    }
    for (const std::string& name : pendingRemovals) {
        if (registry.findSubSystem(name.c_str()) != nullptr) {
            ayt::log::error(
                "[GameLoop] Cannot remove subsystem '%s': strict dependent remains active",
                name.c_str());
        }
    }

    if (!removed.empty()) {
        _phasePlansDirty.store(true);
        if (!rebuildPhasePlans()) {
            // This should be unreachable because strict dependents were
            // rejected above. Stop instead of executing stale raw pointers.
            ayt::log::error("[GameLoop] Fatal phase-plan failure after subsystem removal");
            _shouldExit.store(true);
            for (auto& system : removed) system->shutdown();
            return;
        }
        // Leaf dependents were removed first, so this is already reverse
        // dependency order for shutdown.
        for (auto& system : removed) {
            ayt::log::info("[GameLoop] Shutting down removed subsystem: %s",
                           system->getName());
            system->shutdown();
        }
    }

    // Stage every addition as one batch, allowing two systems queued in the
    // same frame to satisfy each other's declared ordering requirements.
    std::vector<std::string> addedNames;
    for (auto& mutation : mutations) {
        if (mutation.kind != PendingSubSystemMutation::Kind::Add
            || !mutation.system) {
            continue;
        }

        std::vector<const char*> dependencies;
        if (mutation.dependencyOverride) {
            dependencies.reserve(mutation.dependencyOverride->size());
            for (const std::string& dependency : *mutation.dependencyOverride) {
                dependencies.push_back(dependency.c_str());
            }
        }

        ISubSystem* raw = mutation.system.release();
        const bool registered = mutation.dependencyOverride
            ? registry.registerSubSystem(raw, std::move(dependencies),
                                         mutation.priorityOverride)
            : registry.registerSubSystem(raw);
        if (registered) addedNames.push_back(mutation.name);
    }

    if (addedNames.empty()) return;

    const auto executionOrder = registry.getExecutionOrder();
    const bool lifecycleValid = !executionOrder.empty() || registry.getCount() == 0;
    _phasePlansDirty.store(true);
    const bool phasesValid = lifecycleValid && rebuildPhasePlans();
    if (!phasesValid) {
        ayt::log::error("[GameLoop] Rejecting subsystem transaction: invalid lifecycle/phase graph");
        for (const std::string& name : addedNames) {
            (void)registry.takeSubSystem(name.c_str());
        }
        _phasePlansDirty.store(true);
        if (!rebuildPhasePlans()) {
            ayt::log::error("[GameLoop] Failed to restore phase plan after rejected transaction");
            _shouldExit.store(true);
        }
        return;
    }

    std::vector<ISubSystem*> initialized;
    for (ISubSystem* system : executionOrder) {
        if (std::find(addedNames.begin(), addedNames.end(), system->getName())
            == addedNames.end()) {
            continue;
        }
        ayt::log::info("[GameLoop] Initializing added subsystem: %s", system->getName());
        if (!system->initialize()) {
            ayt::log::error("[GameLoop] Rejecting subsystem transaction: '%s' failed to initialize",
                            system->getName());
            // initialize() may have acquired a subset of its resources before
            // reporting failure, so include the failing system in rollback.
            system->shutdown();
            for (auto it = initialized.rbegin(); it != initialized.rend(); ++it) {
                (*it)->shutdown();
            }
            for (const std::string& name : addedNames) {
                (void)registry.takeSubSystem(name.c_str());
            }
            _phasePlansDirty.store(true);
            if (!rebuildPhasePlans()) {
                ayt::log::error("[GameLoop] Failed to restore phase plan after init rollback");
                _shouldExit.store(true);
            }
            return;
        }
        initialized.push_back(system);
    }
}

void GameLoop::setTargetFPS(float fps) {
    _frameManager.setTargetFPS(fps);
}

void GameLoop::setFixedTimestep(float dt) {
    if (_frameInProgress) {
        _pendingNativeFixedTimestep = dt;
        return;
    }
    _pendingNativeFixedTimestep.reset();
    _frameManager.setFixedTimestep(dt);
}

bool GameLoop::setFixedTimestepRatio(uint32_t numerator, uint32_t denominator) {
    if (_frameInProgress) return false;
    if (!_frameManager.setFixedTimestepRatio(numerator, denominator)) return false;
    _pendingNativeFixedTimestep.reset();
    return true;
}

void GameLoop::setTimeScale(float scale) {
    _frameManager.setTimeScale(scale);
}

void GameLoop::setNoLimitFPS(bool noLimit) {
    _noLimitFPS.store(noLimit);
    _frameManager.setNoLimitFPS(noLimit);
}

void GameLoop::registerSubSystem(ISubSystem* system) {
    std::unique_ptr<ISubSystem> owner(system);
    if (!owner) return;

    AY_LOCK_GUARD(_subSystemMutationMutex);
    if (!_deferSubSystemMutations.load(std::memory_order_acquire)) {
        if (SubSystemRegistry::instance().registerSubSystem(owner.release())) {
            _phasePlansDirty.store(true);
        }
        return;
    }

    PendingSubSystemMutation mutation;
    mutation.kind = PendingSubSystemMutation::Kind::Add;
    mutation.name = owner->getName() != nullptr ? owner->getName() : "";
    mutation.system = std::move(owner);
    _pendingSubSystemMutations.push_back(std::move(mutation));
}

void GameLoop::registerSubSystem(ISubSystem* system,
                                 std::vector<const char*> dependencies,
                                 int32_t priority) {
    std::unique_ptr<ISubSystem> owner(system);
    if (!owner) return;

    AY_LOCK_GUARD(_subSystemMutationMutex);
    if (!_deferSubSystemMutations.load(std::memory_order_acquire)) {
        if (SubSystemRegistry::instance().registerSubSystem(
                owner.release(), std::move(dependencies), priority)) {
            _phasePlansDirty.store(true);
        }
        return;
    }

    PendingSubSystemMutation mutation;
    mutation.kind = PendingSubSystemMutation::Kind::Add;
    mutation.name = owner->getName() != nullptr ? owner->getName() : "";
    std::vector<std::string> ownedDependencies;
    ownedDependencies.reserve(dependencies.size());
    for (const char* dependency : dependencies) {
        if (dependency != nullptr) ownedDependencies.emplace_back(dependency);
    }
    mutation.dependencyOverride = std::move(ownedDependencies);
    mutation.priorityOverride = priority;
    mutation.system = std::move(owner);
    _pendingSubSystemMutations.push_back(std::move(mutation));
}

void GameLoop::unregisterSubSystem(const char* name) {
    if (name == nullptr) return;

    AY_LOCK_GUARD(_subSystemMutationMutex);
    if (!_deferSubSystemMutations.load(std::memory_order_acquire)) {
        SubSystemRegistry::instance().unregisterSubSystem(name);
        _phasePlansDirty.store(true);
        return;
    }

    PendingSubSystemMutation mutation;
    mutation.kind = PendingSubSystemMutation::Kind::Remove;
    mutation.name = name;
    _pendingSubSystemMutations.push_back(std::move(mutation));
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

void GameLoop::setPresentationOwnership(PresentationOwnership ownership) {
    if (_presentationOwnership == ownership) {
        return;
    }

    // Switching ownership while a render frame is in flight would leave the
    // single render packet's consumer ambiguous. This setter is main-thread
    // only; complete the previous GameLoop-owned consumption first.
    if (_presentationOwnership == PresentationOwnership::GameLoop
        && _sync.isRenderThreadEnabled()
        && _renderFrameInFlight) {
        _sync.waitForRenderComplete();
        _renderFrameInFlight = false;
    }

    if (ownership == PresentationOwnership::ExternalHost) {
        _sync.stopRenderThread();
    }

    _presentationOwnership = ownership;

    if (ownership == PresentationOwnership::GameLoop
        && _playSessionActive.load()
        && _sync.isRenderThreadEnabled()) {
        _sync.startRenderThread();
    }
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
