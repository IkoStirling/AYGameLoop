#pragma once
// AYGameLoopImpl.h - GameLoop 主类实现

#include <IAYGameLoop.h>
#include <AYSubSystemRegistry.h>
#include <AYFrameManager.h>
#include <AYRenderThreadSync.h>
#include <AYGameLoopEvents.h>
#include <AYGameLoopEventBus.h>
#include <AYJobScheduler.h>
#include <vector>
#include <atomic>
#include <functional>
#include <chrono>

namespace ayt::game
{

class GameLoop : public IGameLoop {
public:
    static GameLoop& instance();

    bool initialize() override;
    void shutdown() override;

    void run() override;
    void stop() override;
    void pause() override;
    void resume() override;

    bool preparePlaySession() override;
    void endPlaySession() override;
    void tickOnce() override;
    void stepOnce() override;
    bool isPlaySessionActive() const override { return _playSessionActive; }

    void setTargetFPS(float fps) override;
    void setFixedTimestep(float dt) override;
    void setTimeScale(float scale) override;
    void setNoLimitFPS(bool noLimit) override;

    float getDeltaTime() const override { return _frameManager.getDeltaTime(); }
    float getUnscaledDeltaTime() const override { return _frameManager.getUnscaledDeltaTime(); }
    float getRealDeltaTime() const override { return _frameManager.getRealDeltaTime(); }
    float getInterpolationFactor() const override { return _frameManager.getInterpolationFactor(); }
    float getElapsedTime() const override { return _frameManager.getElapsedTime(); }
    uint64_t getFrameCount() const override { return _frameManager.getFrameCount(); }

    float getCurrentFPS() const override { return _frameManager.getFrameStats().fps; }
    float getFrameTimeMS() const override { return _frameManager.getFrameStats().avgFrameTimeMs; }

    void registerSubSystem(ISubSystem* system) override;
    void unregisterSubSystem(const char* name) override;

    uint64_t onUpdate(UpdateCallback callback) override;
    void offUpdate(uint64_t listenerId) override;

    // 事件订阅
    template<typename EventType>
    uint64_t onGameEvent(std::function<void(EventType&)> listener) {
        return GAME_LOOP_EVENT_BUS.onEvent(listener);
    }

    // 渲染线程相关
    void setRenderThreadEnabled(bool enabled);
    void setRenderCallback(RenderThreadSync::RenderThreadCallback callback);

    // 并行执行相关
    void setParallelEnabled(bool enabled);
    bool isParallelEnabled() const { return _parallelEnabled; }

private:
    GameLoop();
    ~GameLoop();

    void mainLoop();
    void tickOnceFrame(bool fixedStepDelta = false);
    void processFixedUpdates();
    void processVariableUpdates();
    void submitRenderCommands();

    // 初始化所有子系统
    bool initializeSubSystems();
    // 关闭所有子系统
    void shutdownSubSystems();

    // 更新回调管理
    struct UpdateListener {
        uint64_t id;
        UpdateCallback callback;
    };
    uint64_t _nextListenerId = 1;
    std::vector<UpdateListener> _updateListeners;
    ayt::Mutex _listenerMutex;

    FrameManager _frameManager;
    RenderThreadSync _sync;
    std::vector<std::unique_ptr<ISubSystem>> _ownedSystems;

    std::atomic<bool> _running{false};
    std::atomic<bool> _paused{false};
    std::atomic<bool> _shouldExit{false};
    bool _playSessionActive = false;

    // 渲染帧计数（渲染线程独立计数）
    std::atomic<uint64_t> _renderFrameCount{0};

    // 并行执行
    bool _parallelEnabled = false;
    JobScheduler _jobScheduler;

    // 无限制帧率
    bool _noLimitFPS = false;

    bool _shutdownComplete = false;
    bool _subSystemsRunning = false;
};

} // namespace ayt::game