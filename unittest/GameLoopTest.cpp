#include <AYGameLoop.h>
#include <AYGameLoopEventBus.h>
#include <AYJobScheduler.h>
#include <AYFrameInterpolator.h>
#include <AYCore.h>
#include <AYTest.h>
#include <cstring>

using namespace ayt::game;
using namespace ayt;

TEST_SUITE(GameLoop)

TEST_CASE(Singleton) {
    auto& loop1 = GameLoop::instance();
    auto& loop2 = GameLoop::instance();
    CHECK_TRUE(&loop1 == &loop2);
}

TEST_CASE(SetTargetFPS) {
    auto& loop = GameLoop::instance();
    loop.setTargetFPS(120.0f);
    CHECK_FLOAT_EQ(loop.getDeltaTime(), 0.0f, 0.001f);  // 初始为0
}

TEST_CASE(SetTimeScale) {
    auto& loop = GameLoop::instance();
    loop.setTimeScale(0.5f);
    loop.setTimeScale(2.0f);
    CHECK_TRUE(true);  // 不崩溃即可
}

TEST_CASE(NoLimitFPS) {
    auto& loop = GameLoop::instance();
    loop.setNoLimitFPS(true);
    loop.setNoLimitFPS(false);
    CHECK_TRUE(true);  // 不崩溃即可
}

TEST_CASE(GetCurrentFPS) {
    auto& loop = GameLoop::instance();
    float fps = loop.getCurrentFPS();
    CHECK_TRUE(fps >= 0.0f);  // FPS 应该非负
}

TEST_CASE(GetFrameTimeMS) {
    auto& loop = GameLoop::instance();
    float frameTime = loop.getFrameTimeMS();
    CHECK_TRUE(frameTime >= 0.0f);  // 帧时间应该非负
}

TEST_SUITE_END

TEST_SUITE(SubSystemDescriptor)

TEST_CASE(Default) {
    SubSystemDescriptor desc;
    CHECK_NULL(desc.name);
    CHECK_TRUE(desc.dependencies.empty());
    CHECK_INT_EQ(desc.basePriority, 0);
}

TEST_CASE(WithDependencies) {
    SubSystemDescriptor desc;
    desc.name = "TestSystem";
    desc.dependencies = {"Dep1", "Dep2"};
    desc.basePriority = 100;
    CHECK_TRUE(strcmp(desc.name, "TestSystem") == 0);
    CHECK_INT_EQ(static_cast<int>(desc.dependencies.size()), 2);
    CHECK_INT_EQ(desc.basePriority, 100);
}

TEST_CASE(TimeType) {
    SubSystemDescriptor desc;
    CHECK_INT_EQ(static_cast<int>(desc.timeType), static_cast<int>(SubSystemDescriptor::TimeType::Scaled));

    desc.timeType = SubSystemDescriptor::TimeType::Unscaled;
    CHECK_INT_EQ(static_cast<int>(desc.timeType), static_cast<int>(SubSystemDescriptor::TimeType::Unscaled));

    desc.timeType = SubSystemDescriptor::TimeType::Real;
    CHECK_INT_EQ(static_cast<int>(desc.timeType), static_cast<int>(SubSystemDescriptor::TimeType::Real));
}

TEST_SUITE_END

TEST_SUITE(FrameManager)

TEST_CASE(DefaultValues) {
    FrameManager manager;
    CHECK_FLOAT_EQ(manager.getTargetFPS(), 60.0f, 0.001f);
    CHECK_FLOAT_EQ(manager.getFixedTimestep(), 1.0f / 60.0f, 0.0001f);
    CHECK_FLOAT_EQ(manager.getTimeScale(), 1.0f, 0.001f);
    CHECK_INT_EQ(static_cast<int>(manager.getFrameCount()), 0);
}

TEST_CASE(SetFPS) {
    FrameManager manager;
    manager.setTargetFPS(30.0f);
    CHECK_FLOAT_EQ(manager.getTargetFPS(), 30.0f, 0.001f);
}

TEST_CASE(SetTimeScale) {
    FrameManager manager;
    manager.setTimeScale(2.0f);
    CHECK_FLOAT_EQ(manager.getTimeScale(), 2.0f, 0.001f);
}

TEST_CASE(SetFixedTimestep) {
    FrameManager manager;
    manager.setFixedTimestep(1.0f / 120.0f);
    CHECK_FLOAT_EQ(manager.getFixedTimestep(), 1.0f / 120.0f, 0.0001f);
}

TEST_CASE(PreciseSleep) {
    FrameManager manager;
    CHECK_TRUE(manager.isPreciseSleep());
    manager.setSleepMode(false);
    CHECK_FALSE(manager.isPreciseSleep());
    manager.setSleepMode(true);
    CHECK_TRUE(manager.isPreciseSleep());
}

TEST_CASE(BusyWaitThreshold) {
    FrameManager manager;
    manager.setBusyWaitThreshold(0.002f);
    CHECK_FLOAT_EQ(manager.getBusyWaitThreshold(), 0.002f, 0.0001f);
}

TEST_CASE(SmoothDeltaTimeFactor) {
    FrameManager manager;
    manager.setDeltaTimeSmoothing(0.2f);
    CHECK_FLOAT_EQ(manager.getDeltaTimeSmoothing(), 0.2f, 0.001f);
}

TEST_CASE(NoLimitFPS) {
    FrameManager manager;
    CHECK_FALSE(manager.isNoLimitFPS());
    manager.setNoLimitFPS(true);
    CHECK_TRUE(manager.isNoLimitFPS());
    manager.setNoLimitFPS(false);
    CHECK_FALSE(manager.isNoLimitFPS());
}

TEST_CASE(RealDeltaTime) {
    FrameManager manager;
    manager.beginFrame();
    float realDt = manager.getRealDeltaTime();
    CHECK_TRUE(realDt >= 0.0f);
}

TEST_CASE(InterpolationFactor) {
    FrameManager manager;
    manager.setFixedTimestep(1.0f / 60.0f);
    manager.beginFrame();
    float factor = manager.getInterpolationFactor();
    CHECK_TRUE(factor >= 0.0f && factor < 1.0f);
}

TEST_SUITE_END

TEST_SUITE(RenderThreadSync)

TEST_CASE(DefaultDisabled) {
    RenderThreadSync sync;
    CHECK_FALSE(sync.isRenderThreadEnabled());
    CHECK_FALSE(sync.isRenderThreadRunning());
}

TEST_CASE(EnableDisable) {
    RenderThreadSync sync;
    sync.setRenderThreadEnabled(true);
    CHECK_TRUE(sync.isRenderThreadEnabled());
    sync.setRenderThreadEnabled(false);
    CHECK_FALSE(sync.isRenderThreadEnabled());
}

TEST_CASE(StartStopThread) {
    RenderThreadSync sync;
    sync.setRenderThreadEnabled(true);
    sync.setRenderCallback([]() {
        // 空渲染回调
    });
    sync.startRenderThread();
    CHECK_TRUE(sync.isRenderThreadRunning());
    sync.stopRenderThread();
    CHECK_FALSE(sync.isRenderThreadRunning());
}

TEST_SUITE_END

TEST_SUITE(GameLoopEvents)

TEST_CASE(LogicFrameStartEvent_TypeId) {
    LogicFrameStartEvent event;
    event.frameCount = 1;
    event.unscaledDeltaTime = 0.016f;
    CHECK_INT_EQ(static_cast<int>(event.typeId()), 1);
}

TEST_CASE(LogicFrameEndEvent_TypeId) {
    LogicFrameEndEvent event;
    event.frameCount = 1;
    event.deltaTime = 0.016f;
    CHECK_INT_EQ(static_cast<int>(event.typeId()), 2);
}

TEST_CASE(RenderFrameStartEvent_TypeId) {
    RenderFrameStartEvent event;
    event.frameCount = 1;
    CHECK_INT_EQ(static_cast<int>(event.typeId()), 3);
}

TEST_CASE(RenderFrameEndEvent_TypeId) {
    RenderFrameEndEvent event;
    event.frameCount = 1;
    CHECK_INT_EQ(static_cast<int>(event.typeId()), 4);
}

TEST_CASE(EventPriority_Normal) {
    LogicFrameStartEvent event;
    CHECK_TRUE(event.priority() == EventPriority::Normal);
}

TEST_CASE(EventPriority_High) {
    RenderFrameStartEvent event;
    CHECK_TRUE(event.priority() == EventPriority::High);
}

TEST_SUITE_END

TEST_SUITE(GameLoopEventBus)

TEST_CASE(Singleton) {
    auto& bus1 = GAME_LOOP_EVENT_BUS;
    auto& bus2 = GAME_LOOP_EVENT_BUS;
    CHECK_TRUE(&bus1 == &bus2);
}

TEST_CASE(SubscribeAndEmit) {
    auto& bus = GAME_LOOP_EVENT_BUS;
    bus.clear();

    int called = 0;
    bus.onEvent<LogicFrameStartEvent>([&called](LogicFrameStartEvent& e) {
        called++;
        CHECK_INT_EQ(static_cast<int>(e.frameCount), 42);
    });

    LogicFrameStartEvent event;
    event.frameCount = 42;
    event.unscaledDeltaTime = 0.016f;
    bus.emit(event);
    CHECK_INT_EQ(called, 1);
}

TEST_CASE(MultipleSubscribers) {
    auto& bus = GAME_LOOP_EVENT_BUS;
    bus.clear();

    int count1 = 0, count2 = 0;

    bus.onEvent<GamePausedEvent>([&count1](GamePausedEvent&) { count1++; });
    bus.onEvent<GamePausedEvent>([&count2](GamePausedEvent&) { count2++; });

    GamePausedEvent event;
    event.frameCount = 10;
    bus.emit(event);

    CHECK_INT_EQ(count1, 1);
    CHECK_INT_EQ(count2, 1);
}

TEST_CASE(Clear) {
    auto& bus = GAME_LOOP_EVENT_BUS;
    int called = 0;

    bus.onEvent<LogicFrameStartEvent>([&called](LogicFrameStartEvent&) { called++; });
    bus.clear();

    LogicFrameStartEvent event;
    event.frameCount = 1;
    event.unscaledDeltaTime = 0.016f;
    bus.emit(event);
    CHECK_INT_EQ(called, 0);
}

TEST_SUITE_END

TEST_SUITE(JobScheduler)

TEST_CASE(DefaultNotInitialized) {
    JobScheduler scheduler;
    CHECK_FALSE(scheduler.isInitialized());
}

TEST_CASE(Initialize) {
    JobScheduler scheduler;
    bool ok = scheduler.initialize();
    CHECK_TRUE(ok);
    CHECK_TRUE(scheduler.isInitialized());
    scheduler.shutdown();
}

TEST_CASE(CreateGraph) {
    JobScheduler scheduler;
    scheduler.initialize();
    auto graph = scheduler.createGraph();
    CHECK_NOT_NULL(graph);
    delete graph;
    scheduler.shutdown();
}

TEST_SUITE_END

TEST_SUITE(SmoothDeltaTime)

TEST_CASE(BasicSmoothing) {
    SmoothDeltaTime smoother;
    smoother.add(0.016f);
    CHECK_FLOAT_EQ(smoother.getRaw(), 0.016f, 0.001f);
    smoother.add(0.032f);
    CHECK_FLOAT_EQ(smoother.getRaw(), 0.032f, 0.001f);
}

TEST_CASE(SmoothingFactor) {
    SmoothDeltaTime smoother;
    smoother.setSmoothingFactor(0.5f);
    CHECK_FLOAT_EQ(smoother.getSmoothingFactor(), 0.5f, 0.001f);
}

TEST_SUITE_END

TEST_SUITE(Vec2Interpolator)

TEST_CASE(Lerp) {
    Vec2Interpolator interp;
    interp.setTarget(1.0f, 2.0f);
    interp.lerp(0.5f);
    CHECK_FLOAT_EQ(interp.current[0], 0.5f, 0.001f);
    CHECK_FLOAT_EQ(interp.current[1], 1.0f, 0.001f);
}

TEST_SUITE_END

TEST_SUITE(Vec3Interpolator)

TEST_CASE(Lerp) {
    Vec3Interpolator interp;
    interp.setTarget(1.0f, 2.0f, 3.0f);
    interp.lerp(0.5f);
    CHECK_FLOAT_EQ(interp.current[0], 0.5f, 0.001f);
    CHECK_FLOAT_EQ(interp.current[1], 1.0f, 0.001f);
    CHECK_FLOAT_EQ(interp.current[2], 1.5f, 0.001f);
}

TEST_SUITE_END

TEST_SUITE(QuaternionInterpolator)

TEST_CASE(Slerp) {
    QuaternionInterpolator interp;
    interp.current[0] = 0.0f; interp.current[1] = 0.0f;
    interp.current[2] = 0.0f; interp.current[3] = 1.0f;
    interp.setTarget(0.0f, 0.0f, 0.0f, 1.0f);
    interp.slerp(0.0f);
    // 无变化
    CHECK_FLOAT_EQ(interp.current[3], 1.0f, 0.001f);
}

TEST_SUITE_END

TEST_SUITE(TransformInterpolator)

TEST_CASE(Interpolate) {
    TransformInterpolator interp;
    interp.position.setTarget(10.0f, 20.0f, 30.0f);
    interp.rotation.setTarget(0.0f, 0.0f, 0.0f, 1.0f);
    interp.scale.setTarget(2.0f, 2.0f, 2.0f);
    interp.interpolate(0.5f);
    CHECK_FLOAT_EQ(interp.position.current[0], 5.0f, 0.001f);
}

TEST_SUITE_END

TEST_SUITE(GameLoopState)

TEST_CASE(InitialState) {
    auto& loop = GameLoop::instance();
    CHECK_INT_EQ(static_cast<int>(loop.getFrameCount()), 0);
    CHECK_FLOAT_EQ(loop.getDeltaTime(), 0.0f, 0.001f);
    CHECK_FLOAT_EQ(loop.getElapsedTime(), 0.0f, 0.001f);
}

TEST_CASE(ParallelEnabled) {
    auto& loop = GameLoop::instance();
    loop.setParallelEnabled(true);
    CHECK_TRUE(loop.isParallelEnabled());
    loop.setParallelEnabled(false);
    CHECK_FALSE(loop.isParallelEnabled());
}

TEST_SUITE_END

TEST_SUITE(AYTime)

TEST_CASE(PerformanceNowUs) {
    UInt64 t1 = ayt::performanceNowUs();
    CHECK_TRUE(t1 > 0);
    ayt::Thread::sleep(0.01f);
    UInt64 t2 = ayt::performanceNowUs();
    CHECK_TRUE(t2 > t1);
}

TEST_CASE(PauseResume) {
    bool wasPaused = ayt::isGamePaused();
    ayt::pauseGame();
    CHECK_TRUE(ayt::isGamePaused());
    ayt::resumeGame();
    if (!wasPaused) {
        CHECK_FALSE(ayt::isGamePaused());
    }
}

TEST_SUITE_END