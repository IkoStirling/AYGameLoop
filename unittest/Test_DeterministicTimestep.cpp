#include <AYGameLoop.h>
#include <AYGameLoop/GameLoopEvents.h>
#include <AYEventSystem/EventBus.h>
#include <AYTest.h>
#include <cfenv>
#include <vector>
#include <thread>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
namespace {
using namespace ayt::game;
using D = ayt::math::DetFloat32;
struct Capture final : ISubSystem {
    SubSystemDescriptor desc;
    std::vector<FrameContext> contexts;
    bool failPhysics = false, mutate = false, nested = false, ratioAccepted = false;
    bool blocked = false;
    bool ownerThread = false, wrongThread = false;
    std::thread::id owner=std::this_thread::get_id();
    unsigned presentations = 0;
    Capture() {
        desc.name = "TypedFixedCapture";
        desc.phases = phaseBit(FramePhase::FixedPrePhysics) | phaseBit(FramePhase::FixedPhysics)
            | phaseBit(FramePhase::FixedPostPhysics) | phaseBit(FramePhase::Presentation);
    }
    const char* getName() const override { return desc.name; }
    const SubSystemDescriptor& getDescriptor() const override { return desc; }
    bool initialize() override { return true; }
    bool requiresOwnerThread() const noexcept override {return ownerThread;}
    void update(float) override {}
    void fixedUpdate(float) override {}
    void shutdown() override {}
    bool tickChecked(FramePhase phase, const FrameContext& context) override {
        if(ownerThread && std::this_thread::get_id()!=owner)wrongThread=true;
        if(phase==FramePhase::Presentation){++presentations;return true;}
        contexts.push_back(context);
        if (phase == FramePhase::FixedPrePhysics && mutate) {
            auto& loop = GameLoop::instance();
            ratioAccepted = loop.setFixedTimestepRatio(1,32);
            loop.setFixedTimestep(1.0f/32.0f);
        }
        if (phase == FramePhase::FixedPrePhysics && nested) GameLoop::instance().stepOnce();
        return !(failPhysics && phase == FramePhase::FixedPhysics);
    }
    TickResult tickResult(FramePhase phase,const FrameContext& context) override {
        if(blocked && phase==FramePhase::FixedPrePhysics)return TickResult::Blocked;
        return ISubSystem::tickResult(phase,context);
    }
};
struct Session {
    GameLoop& loop = GameLoop::instance();
    Capture* capture;
    std::vector<SimTickEvent> events;
    std::uint64_t subscription;
    bool prepared;
    Session() {
        loop.endHostedSession();
        SubSystemRegistry::instance().clearAll();
        loop.setRenderThreadEnabled(false);
        loop.setParallelEnabled(false);
        loop.setMaxFixedUpdatesPerFrame(8);
        loop.setFixedBacklogPolicy(FixedBacklogPolicy::Drop);
        (void)loop.setFixedTimestepRatio(1,64);
        capture = new Capture;
        loop.registerSubSystem(capture);
        prepared = loop.prepareHostedSession();
        subscription = ayt::event::EventBus::instance().subscribe<SimTickEvent>(
            [this](const SimTickEvent& e) { events.push_back(e); });
    }
    ~Session() {
        ayt::event::EventBus::instance().unsubscribe(subscription);
        loop.endHostedSession();
        SubSystemRegistry::instance().clearAll();
        (void)loop.setFixedTimestepRatio(1,60);
        loop.setTimeScale(1);
    }
};
}
TEST_SUITE(DeterministicTimestepTests)
TEST_CASE(ratio_reduction_has_one_software_rounding_and_rejects_unsupported_values) {
    const auto value = FixedTimestep::fromRatio(1000,60000);
    CHECK_TRUE(value.has_value());
    CHECK(value->numerator()==1 && value->denominator()==60 && value->deltaTime().bits()==0x3c888889u);
    CHECK_TRUE(!FixedTimestep::fromRatio(0,1));
    CHECK_TRUE(!FixedTimestep::fromRatio(1,0));
    CHECK_TRUE(!FixedTimestep::fromRatio(0x01000001u,1));
    CHECK_TRUE(!FixedTimestep::fromRatio(1,0xffffffffu));
    CHECK_TRUE(FixedTimestep::fromRatio(0xffffffffu,0xffffffffu)->deltaTime()==D::fromInt(1));
    CHECK_TRUE(FixedTimestep{}.deltaTime().bits()==0x3c888889u);
}
TEST_CASE(native_step_is_a_marked_explicit_boundary) {
    const auto value = FixedTimestep::fromNative(D::fromBits(0x3c888888u).toFloat());
    CHECK_TRUE(value && !value->isRational() && value->numerator()==0 && value->denominator()==0);
    CHECK(value->deltaTime().bits()==0x3c888888u);
    for (auto bits : {0u,0x80000000u,0xbf800000u,0x7f800000u,0x7fc00000u})
        CHECK_TRUE(!FixedTimestep::fromNative(D::fromBits(bits).toFloat()));
}
TEST_CASE(frame_manager_keeps_native_and_typed_steps_consistent_without_partial_updates) {
    FrameManager manager;
    CHECK(manager.getFixedStep().deltaTime().bits()==0x3c888889u);
    CHECK_TRUE(manager.setFixedTimestepRatio(1,64));
    manager.beginFrameWithDelta(1.0f/32.0f);
    CHECK_FALSE(manager.setFixedTimestepRatio(1,0));
    CHECK(manager.getFixedUpdateCount()==2 && manager.getFixedStep().denominator()==64);
    CHECK(std::bit_cast<std::uint32_t>(manager.getFixedTimestep())==manager.getFixedStep().deltaTime().bits());
    manager.setFixedTimestep(0.02f);
    CHECK_FALSE(manager.getFixedStep().isRational());
    manager.setFixedTimestep(-1);
    CHECK(manager.getFixedStep().deltaTime().bits()==0x3c888889u);
}
TEST_CASE(very_small_steps_are_capped_before_integer_conversion) {
    FrameManager manager;
    CHECK_TRUE(manager.setFixedTimestepRatio(1,0x01000000u));
    manager.beginFrameWithDelta(0.25f);
    CHECK(manager.getFixedUpdateCount()==8);
    manager.setFixedTimestep(D::fromBits(1).toFloat());
    CHECK(manager.getFixedUpdateCount()==8);
}
TEST_CASE(ratio_construction_ignores_host_rounding_and_ftz_daz) {
    const auto rounding=std::fegetround();
#if defined(_M_X64) || defined(__x86_64__)
    const auto csr=_mm_getcsr();
#endif
    bool good=true;
    for (auto mode : {FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}) {
        std::fesetround(mode);
#if defined(_M_X64) || defined(__x86_64__)
        _mm_setcsr(_mm_getcsr() | 0x8040u);
#endif
        good &= FixedTimestep::fromRatio(1,60)->deltaTime().bits()==0x3c888889u;
    }
    std::fesetround(rounding);
#if defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(csr);
#endif
    CHECK_TRUE(good);
}
TEST_CASE(preconfigured_ratio_survives_host_preparation_and_reaches_all_tick_phases) {
    Session s; CHECK_TRUE(s.prepared);
    CHECK_FALSE(s.loop.setFixedTimestepRatio(0,64));
    const auto tick=s.loop.getSimTick();
    s.loop.tickHostedFrame({1.0f/32.0f,73,91});
    CHECK(s.loop.getSimTick()==tick+2 && s.capture->contexts.size()==6 && s.events.size()==2);
    for (const auto& c : s.capture->contexts) {
        CHECK(c.fixedStep && c.fixedStep->denominator()==64 && c.fixedStep->deltaTime().bits()==0x3c800000u);
        CHECK(c.hostFrameIndex==73 && c.inputFrameIndex==91);
        CHECK(std::bit_cast<std::uint32_t>(c.fixedDeltaTime)==0x3c800000u);
    }
    for (const auto& e : s.events) CHECK(e.fixedStep && e.fixedStep->deltaTime().bits()==0x3c800000u);
}
TEST_CASE(pause_and_single_step_keep_the_numeric_input_independent_of_time_scale) {
    Session s; CHECK_TRUE(s.prepared);
    const auto tick=s.loop.getSimTick();
    s.loop.pause(); s.loop.setTimeScale(0);
    s.loop.tickHostedFrame({1.0f/16.0f,1,1});
    CHECK(s.loop.getSimTick()==tick && s.capture->contexts.empty());
    s.loop.stepOnce();
    CHECK(s.loop.getSimTick()==tick+1 && s.events.size()==1);
    CHECK(s.events.back().fixedStep->deltaTime().bits()==0x3c800000u);
    s.loop.tickHostedFrame({1.0f/16.0f,2,2});
    CHECK(s.loop.getSimTick()==tick+1);
}
TEST_CASE(phase_failure_emits_no_commit_and_preserves_tick_number) {
    Session s; CHECK_TRUE(s.prepared); s.capture->failPhysics=true;
    const auto tick=s.loop.getSimTick(); s.loop.stepOnce();
    CHECK(s.loop.getSimTick()==tick && s.events.empty() && s.loop.getFixedTicksLastFrame()==0);
    CHECK(s.capture->contexts.size()==2);
}
TEST_CASE(recoverable_barrier_pauses_without_committing_and_keeps_presentation_alive) {
    Session s;CHECK_TRUE(s.prepared);s.capture->blocked=true;const auto tick=s.loop.getSimTick();
    s.loop.tickHostedFrame({1.0f/16.0f,1,1});
    CHECK(s.loop.getSimTick()==tick && s.events.empty() && s.capture->presentations==1);
    s.loop.tickHostedFrame({1.0f/16.0f,2,2});
    CHECK(s.loop.getSimTick()==tick && s.capture->presentations==2);
    s.capture->blocked=false;s.loop.stepOnce();
    CHECK(s.loop.getSimTick()==tick+1 && s.events.size()==1 && s.capture->presentations==3);
    s.loop.tickHostedFrame({1.0f/16.0f,3,3});CHECK(s.loop.getSimTick()==tick+1);
    s.loop.resume();s.loop.tickHostedFrame({1.0f/64.0f,4,4});CHECK(s.loop.getSimTick()==tick+2);
}
TEST_CASE(single_step_discards_preserved_debt_and_nested_steps_do_not_clear_outer_batch) {
    Session s;CHECK_TRUE(s.prepared);s.loop.setFixedBacklogPolicy(FixedBacklogPolicy::Preserve);
    s.loop.setMaxFixedUpdatesPerFrame(2);s.loop.tickHostedFrame({1.0f/16.0f,1,1});
    const auto tick=s.loop.getSimTick();s.loop.pause();s.loop.stepOnce();CHECK(s.loop.getSimTick()==tick+1);
    s.loop.resume();s.capture->nested=true;s.loop.tickHostedFrame({1.0f/32.0f,2,2});
    CHECK(s.loop.getSimTick()==tick+3);
    s.loop.setMaxFixedUpdatesPerFrame(8);s.loop.setFixedBacklogPolicy(FixedBacklogPolicy::Drop);
}
TEST_CASE(owner_affinity_is_respected_and_parallel_blocked_layer_remains_recoverable) {
    Session s;CHECK_TRUE(s.prepared);s.loop.setParallelEnabled(true);s.capture->ownerThread=true;
    const auto tick=s.loop.getSimTick();s.loop.stepOnce();
    CHECK(!s.capture->wrongThread && s.loop.getSimTick()==tick+1);
    s.capture->ownerThread=false;s.capture->blocked=true;s.loop.stepOnce();CHECK(s.loop.getSimTick()==tick+1);
    s.capture->blocked=false;s.loop.stepOnce();CHECK(s.loop.getSimTick()==tick+2);
    s.loop.setParallelEnabled(false);
}
TEST_CASE(configuration_cannot_split_a_fixed_batch_and_legacy_changes_wait_for_next_frame) {
    Session s; CHECK_TRUE(s.prepared); s.capture->mutate=true;
    s.loop.tickHostedFrame({1.0f/32.0f,1,1});
    CHECK_FALSE(s.capture->ratioAccepted);
    CHECK(s.events.size()==2);
    for (const auto& e : s.events) CHECK(e.fixedStep->denominator()==64);
    s.capture->mutate=false; s.capture->contexts.clear(); s.events.clear();
    s.loop.tickHostedFrame({1.0f/32.0f,2,2});
    CHECK(s.events.size()==1 && !s.events[0].fixedStep->isRational());
    CHECK(s.events[0].fixedStep->deltaTime().bits()==0x3d000000u);
}
TEST_CASE(nested_host_steps_do_not_duplicate_simulation) {
    Session s; CHECK_TRUE(s.prepared); s.capture->nested=true;
    const auto tick=s.loop.getSimTick(); s.loop.stepOnce();
    CHECK(s.loop.getSimTick()==tick+1 && s.capture->contexts.size()==3 && s.events.size()==1);
}
TEST_CASE(new_host_session_discards_fixed_debt_and_retains_the_numeric_configuration) {
    Session s; CHECK_TRUE(s.prepared);
    s.loop.tickHostedFrame({1.0f/128.0f,1,1});
    CHECK(s.events.empty());
    s.loop.endHostedSession();
    CHECK_TRUE(s.loop.prepareHostedSession());
    CHECK(s.loop.getFixedStep()->denominator()==64);
    s.loop.tickHostedFrame({1.0f/128.0f,2,2});
    CHECK(s.events.empty());
    CHECK(s.loop.getFrameCount()==1);
}
TEST_CASE(initial_event_pump_cannot_reenter_or_change_the_current_fixed_input) {
    Session s; CHECK_TRUE(s.prepared);
    const auto tick=s.loop.getSimTick();
    bool handled=false;
    auto& bus=ayt::event::EventBus::instance();
    const auto subscription=bus.subscribe<LogicFrameStartEvent>([&](const LogicFrameStartEvent&) {
        if (handled) return;
        handled=true;
        s.loop.stepOnce();
        s.loop.setFixedTimestep(1.0f/32.0f);
    });
    bus.post(LogicFrameStartEvent{});
    s.loop.stepOnce();
    bus.unsubscribe(subscription);
    CHECK_TRUE(handled);
    CHECK(s.loop.getSimTick()==tick+1 && s.events.size()==1);
    CHECK(s.events[0].fixedStep->denominator()==64);
}
TEST_SUITE_END;
