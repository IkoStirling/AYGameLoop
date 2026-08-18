#pragma once
// AYGameLoop/GameLoopImpl.h - GameLoop 主类实现
//
// Threading contract (see IGameLoop for the public side):
//   - All member functions are main-thread only EXCEPT:
//       * stop() / pause() / resume()        (any thread; sets atomic + posts event)
//       * onUpdate() / offUpdate()           (mutex-protected; any thread)
//       * registerSubSystem() / unregister   (mutex-protected; any thread)
//   - _running / _shouldExit / _paused are std::atomic; all other state
//     is main-thread only (no synchronization needed).

#include <AYGameLoop/IGameLoop.h>
#include <AYGameLoop/SubSystemRegistry.h>
#include <AYGameLoop/FrameManager.h>
#include <AYGameLoop/RenderThreadSync.h>
#include <AYGameLoop/JobScheduler.h>
#include <AYGameLoop/GameLoopEvents.h>
#include <AYEventSystem/EventBus.h>
#include <vector>
#include <atomic>
#include <functional>
#include <array>
#include <memory>
#include <optional>
#include <string>

namespace ayt::game
{

class GameLoop : public IGameLoop {
public:
    GameLoop(const GameLoop&) = delete;
    GameLoop& operator=(const GameLoop&) = delete;
    GameLoop(GameLoop&&) = delete;
    GameLoop& operator=(GameLoop&&) = delete;

    [[nodiscard]] static GameLoop& instance();

    bool initialize() override;
    void shutdown() override;

    void run() override;
    void stop() override;
    void pause() override;
    void resume() override;

    [[nodiscard]] bool preparePlaySession() override;
    void endPlaySession() override;
    void tickOnce() override;
    void stepOnce() override;
    [[nodiscard]] bool isPlaySessionActive() const override { return _playSessionActive.load(); }

    void setTargetFPS(float fps) override;
    void setFixedTimestep(float dt) override;
    void setTimeScale(float scale) override;
    void setNoLimitFPS(bool noLimit) override;
    void setMaxFixedUpdatesPerFrame(uint32_t count) override {
        _frameManager.setMaxFixedUpdatesPerFrame(count);
    }
    void setFixedBacklogPolicy(FixedBacklogPolicy policy) override {
        _frameManager.setFixedBacklogPolicy(policy);
    }

    [[nodiscard]] float getDeltaTime() const override { return _frameManager.getDeltaTime(); }
    [[nodiscard]] float getUnscaledDeltaTime() const override { return _frameManager.getUnscaledDeltaTime(); }
    [[nodiscard]] float getRealDeltaTime() const override { return _frameManager.getRealDeltaTime(); }
    [[nodiscard]] float getInterpolationFactor() const override { return _frameManager.getInterpolationFactor(); }
    [[nodiscard]] float getElapsedTime() const override { return _frameManager.getElapsedTime(); }
    [[nodiscard]] double getGameElapsedTime() const override {
        return _frameManager.getGameElapsedTime();
    }
    [[nodiscard]] float getEffectiveTimeScale() const override {
        return _frameManager.getEffectiveTimeScale();
    }
    [[nodiscard]] FixedBacklogPolicy getFixedBacklogPolicy() const override {
        return _frameManager.getFixedBacklogPolicy();
    }
    [[nodiscard]] uint64_t getFrameCount() const override { return _frameManager.getFrameCount(); }
    [[nodiscard]] uint64_t getSimTick() const override { return _simTick; }
    [[nodiscard]] uint64_t getDroppedSimTickCount() const override {
        return _frameManager.getDroppedSimTickCount();
    }
    [[nodiscard]] double getDroppedSimulationSeconds() const override {
        return _frameManager.getDroppedSimulationSeconds();
    }
    [[nodiscard]] double getClampedSimulationSeconds() const override {
        return _frameManager.getClampedSimulationSeconds();
    }
    [[nodiscard]] PhaseTimingStats getPhaseTiming(FramePhase phase) const override {
        const size_t index = static_cast<size_t>(phase);
        return index < _phaseTiming.size() ? _phaseTiming[index] : PhaseTimingStats{};
    }
    [[nodiscard]] uint32_t getFixedTicksLastFrame() const override {
        return _fixedTicksLastFrame;
    }

    [[nodiscard]] float getCurrentFPS() const override { return _frameManager.getFrameStats().fps; }
    [[nodiscard]] float getFrameTimeMS() const override { return _frameManager.getFrameStats().avgFrameTimeMs; }

    void registerSubSystem(ISubSystem* system) override;
    void registerSubSystem(ISubSystem* system,
                           std::vector<const char*> dependencies,
                           int32_t priority) override;
    void unregisterSubSystem(const char* name) override;

    [[nodiscard]] uint64_t onUpdate(UpdateCallback callback) override;
    void offUpdate(uint64_t listenerId) override;

    // 事件订阅（转给 AYEventSystem 主单例，返回 ConnectionId ≡ uint64_t）
    template<typename EventType>
    uint64_t onGameEvent(std::function<void(EventType&)> listener) {
        // Wrap a void(const EventType&) signature for the EventBus; the
        // const_cast strips the const the EventBus adds because the
        // listener contract wants a mutable reference.
        return ayt::event::EventBus::instance().subscribe<EventType>(
            [l = std::move(listener)](const EventType& e) {
                l(const_cast<EventType&>(e));
            }
        );
    }

    // 渲染线程相关
    void setRenderThreadEnabled(bool enabled) override;
    void setRenderCallback(RenderThreadSync::RenderThreadCallback callback) override;

    // 并行执行相关
    void setParallelEnabled(bool enabled) override;
    [[nodiscard]] bool isParallelEnabled() const override { return _parallelEnabled.load(); }

private:
    GameLoop();
    ~GameLoop();

    void mainLoop();
    void tickOnceFrame(bool fixedStepDelta = false);
    void processFixedUpdates();
    void processVariableUpdates(bool paused);
    [[nodiscard]] bool runPhase(FramePhase phase, bool paused);
    [[nodiscard]] bool rebuildPhasePlans();
    void applyPendingSubSystemMutations();
    [[nodiscard]] bool hasStrictDependent(const std::string& name) const;
    void submitRenderCommands();

    // Layer-by-layer DAG build for parallel subsystem updates.
    // Each layer's subsystems run concurrently; a subsystem in layer N+1
    // depends on every subsystem in layer N (its declared dependencies).
    std::vector<std::vector<ISubSystem*>> buildExecutionLayers(FramePhase phase) const;
    [[nodiscard]] bool runPhaseLayers(
        FramePhase phase,
        const std::vector<std::vector<ISubSystem*>>& layers,
        bool paused);
    [[nodiscard]] FrameContext frameContextFor(FramePhase phase,
                                               const ISubSystem& system,
                                               bool paused) const;
    [[nodiscard]] PhaseMask effectivePhases(const ISubSystem& system) const;
    [[nodiscard]] float deltaTimeFor(const ISubSystem& system, bool paused) const;

    bool initializeSubSystems();
    void shutdownSubSystems();

    struct UpdateListener {
        uint64_t id;
        UpdateCallback callback;
    };
    uint64_t _nextListenerId = 1;
    std::vector<UpdateListener> _updateListeners;
    ayt::Mutex _listenerMutex;

    FrameManager _frameManager;
    RenderThreadSync _sync;
    bool _renderFrameInFlight = false;
    uint64_t _simTick = 0;
    uint64_t _pendingSimTick = 0;
    uint32_t _fixedTicksLastFrame = 0;

    std::atomic<bool> _running{false};
    std::atomic<bool> _paused{false};
    std::atomic<bool> _shouldExit{false};
    std::atomic<bool> _playSessionActive{false};

    // Shutdown idempotency guard (atomic compare-exchange).
    std::atomic<bool> _shutdownInProgress{false};
    std::atomic<bool> _shutdownComplete{false};
    std::atomic<bool> _subSystemsRunning{false};
    std::atomic<bool> _deferSubSystemMutations{false};

    struct PendingSubSystemMutation {
        enum class Kind : uint8_t { Add, Remove };
        Kind kind = Kind::Add;
        std::unique_ptr<ISubSystem> system;
        std::string name;
        std::optional<std::vector<std::string>> dependencyOverride;
        int32_t priorityOverride = 0;
    };
    ayt::Mutex _subSystemMutationMutex;
    std::vector<PendingSubSystemMutation> _pendingSubSystemMutations;

    std::atomic<bool> _parallelEnabled{false};
    JobScheduler _jobScheduler;

    std::array<std::vector<std::vector<ISubSystem*>>,
               static_cast<size_t>(FramePhase::Count)> _phasePlans;
    std::array<PhaseTimingStats,
               static_cast<size_t>(FramePhase::Count)> _phaseTiming{};
    std::atomic<bool> _phasePlansDirty{true};

    std::atomic<bool> _noLimitFPS{false};
};

} // namespace ayt::game
