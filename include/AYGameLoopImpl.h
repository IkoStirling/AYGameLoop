#pragma once
// AYGameLoopImpl.h - GameLoop 主类实现
//
// Threading contract (see IGameLoop for the public side):
//   - All member functions are main-thread only EXCEPT:
//       * stop() / pause() / resume()        (any thread; sets atomic + emits event)
//       * onUpdate() / offUpdate()           (mutex-protected; any thread)
//       * registerSubSystem() / unregister   (mutex-protected; any thread)
//   - _running / _shouldExit / _paused are std::atomic; all other state
//     is main-thread only (no synchronization needed).

#include <IAYGameLoop.h>
#include <AYSubSystemRegistry.h>
#include <AYFrameManager.h>
#include <AYRenderThreadSync.h>
#include <AYJobScheduler.h>
#include <AYGameLoopEvents.h>
#include <ayevent/EventBus.h>
#include <vector>
#include <atomic>
#include <functional>

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

    [[nodiscard]] float getDeltaTime() const override { return _frameManager.getDeltaTime(); }
    [[nodiscard]] float getUnscaledDeltaTime() const override { return _frameManager.getUnscaledDeltaTime(); }
    [[nodiscard]] float getRealDeltaTime() const override { return _frameManager.getRealDeltaTime(); }
    [[nodiscard]] float getInterpolationFactor() const override { return _frameManager.getInterpolationFactor(); }
    [[nodiscard]] float getElapsedTime() const override { return _frameManager.getElapsedTime(); }
    [[nodiscard]] uint64_t getFrameCount() const override { return _frameManager.getFrameCount(); }
    [[nodiscard]] uint64_t getSimTick() const override { return _simTick; }

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
    void processVariableUpdates();
    void submitRenderCommands();

    // Layer-by-layer DAG build for parallel subsystem updates.
    // Each layer's subsystems run concurrently; a subsystem in layer N+1
    // depends on every subsystem in layer N (its declared dependencies).
    std::vector<std::vector<ISubSystem*>> buildExecutionLayers() const;
    void runVariableUpdatesParallel(const std::vector<std::vector<ISubSystem*>>& layers, float dt);

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
    uint64_t _simTick = 0;

    std::atomic<bool> _running{false};
    std::atomic<bool> _paused{false};
    std::atomic<bool> _shouldExit{false};
    std::atomic<bool> _playSessionActive{false};

    // Shutdown idempotency guard (atomic compare-exchange).
    std::atomic<bool> _shutdownInProgress{false};
    std::atomic<bool> _shutdownComplete{false};
    std::atomic<bool> _subSystemsRunning{false};

    std::atomic<bool> _parallelEnabled{false};
    JobScheduler _jobScheduler;

    std::atomic<bool> _noLimitFPS{false};
};

} // namespace ayt::game