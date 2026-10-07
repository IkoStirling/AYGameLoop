#pragma once
// AYGameLoop/GameLoopEvents.h - 游戏循环事件定义
//
// Phase 4 (2026-07-19): migrated from local IGameLoopEvent / GameLoopEventBus
// to AYEventSystem. Events are plain structs with hand-assigned kTypeId and
// kPriority. The EventBus dispatches by EventTypeId and drains deferred events
// in priority order (Critical → Low). See AYEventSystem design.md §3-§4.

#include <AYEventSystem/EventPriority.h>
#include <AYGameLoop/FixedTimestep.h>
#include <cstdint>

namespace ayt::game
{

// -----------------------------------------------------------------------------
// Logic frame events (Normal priority — drain after window/critical)
// -----------------------------------------------------------------------------
struct LogicFrameStartEvent {
    static constexpr uint32_t kTypeId = 1;
    static constexpr ayt::event::EventPriority kPriority = ayt::event::EventPriority::Normal;

    uint64_t frameCount = 0;
    float unscaledDeltaTime = 0.0f;

    LogicFrameStartEvent() = default;
    LogicFrameStartEvent(uint64_t frame, float dt)
        : frameCount(frame), unscaledDeltaTime(dt) {}
};

struct LogicFrameEndEvent {
    static constexpr uint32_t kTypeId = 2;
    static constexpr ayt::event::EventPriority kPriority = ayt::event::EventPriority::Normal;

    uint64_t frameCount = 0;
    float deltaTime = 0.0f;

    LogicFrameEndEvent() = default;
    LogicFrameEndEvent(uint64_t frame, float dt)
        : frameCount(frame), deltaTime(dt) {}
};

// -----------------------------------------------------------------------------
// Render frame events (High priority — discrete per-render-thread signals)
// -----------------------------------------------------------------------------
struct RenderFrameStartEvent {
    static constexpr uint32_t kTypeId = 3;
    static constexpr ayt::event::EventPriority kPriority = ayt::event::EventPriority::High;

    uint64_t frameCount = 0;

    RenderFrameStartEvent() = default;
    explicit RenderFrameStartEvent(uint64_t frame)
        : frameCount(frame) {}
};

struct RenderFrameEndEvent {
    static constexpr uint32_t kTypeId = 4;
    static constexpr ayt::event::EventPriority kPriority = ayt::event::EventPriority::High;

    uint64_t frameCount = 0;

    RenderFrameEndEvent() = default;
    explicit RenderFrameEndEvent(uint64_t frame)
        : frameCount(frame) {}
};

// -----------------------------------------------------------------------------
// Lifecycle events (High priority — discrete state transitions)
// -----------------------------------------------------------------------------
struct GamePausedEvent {
    static constexpr uint32_t kTypeId = 5;
    static constexpr ayt::event::EventPriority kPriority = ayt::event::EventPriority::High;

    uint64_t frameCount = 0;

    GamePausedEvent() = default;
    explicit GamePausedEvent(uint64_t frame)
        : frameCount(frame) {}
};

struct GameResumedEvent {
    static constexpr uint32_t kTypeId = 6;
    static constexpr ayt::event::EventPriority kPriority = ayt::event::EventPriority::High;

    uint64_t frameCount = 0;

    GameResumedEvent() = default;
    explicit GameResumedEvent(uint64_t frame)
        : frameCount(frame) {}
};

struct GameStoppedEvent {
    static constexpr uint32_t kTypeId = 7;
    static constexpr ayt::event::EventPriority kPriority = ayt::event::EventPriority::High;

    uint64_t frameCount = 0;

    GameStoppedEvent() = default;
    explicit GameStoppedEvent(uint64_t frame)
        : frameCount(frame) {}
};

// -----------------------------------------------------------------------------
// Sim tick (Normal — once per fixedUpdate step; shared time anchor)
// -----------------------------------------------------------------------------
struct SimTickEvent {
    static constexpr uint32_t kTypeId = 8;
    static constexpr ayt::event::EventPriority kPriority = ayt::event::EventPriority::Normal;

    /// Monotonic Sim tick count (increments once per fixed step, including
    /// catch-up steps within a single display frame).
    uint64_t simTick = 0;
    float fixedDeltaTime = 0.0f;
    uint64_t frameCount = 0;
    /// Same immutable fixed input as all three phases of this committed tick.
    /// nullopt for events manually constructed through the legacy constructor.
    std::optional<FixedTimestep> fixedStep;

    SimTickEvent() = default;
    SimTickEvent(uint64_t tick, float fixedDt, uint64_t frame)
        : simTick(tick), fixedDeltaTime(fixedDt), frameCount(frame) {}
};

} // namespace ayt::game
