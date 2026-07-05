#pragma once
// AYGameLoopEvents.h - 游戏循环事件定义

#include <AYCore.h>
#include <cstdint>

namespace ayt::game
{

// 事件优先级
enum class EventPriority : int {
    Low = -100,
    Normal = 0,
    High = 100,
};

// 事件基类
struct IGameLoopEvent {
    virtual ~IGameLoopEvent() = default;
    virtual uint32_t typeId() const = 0;
    virtual EventPriority priority() const = 0;
};

// 逻辑帧开始事件
struct LogicFrameStartEvent : IGameLoopEvent {
    static constexpr uint32_t kTypeId = 1;
    static constexpr EventPriority kPriority = EventPriority::Normal;

    uint64_t frameCount = 0;
    float unscaledDeltaTime = 0.0f;

    LogicFrameStartEvent() = default;
    LogicFrameStartEvent(uint64_t frame, float dt)
        : frameCount(frame), unscaledDeltaTime(dt) {}

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

// 逻辑帧结束事件
struct LogicFrameEndEvent : IGameLoopEvent {
    static constexpr uint32_t kTypeId = 2;
    static constexpr EventPriority kPriority = EventPriority::Normal;

    uint64_t frameCount = 0;
    float deltaTime = 0.0f;

    LogicFrameEndEvent() = default;
    LogicFrameEndEvent(uint64_t frame, float dt)
        : frameCount(frame), deltaTime(dt) {}

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

// 渲染帧开始事件
struct RenderFrameStartEvent : IGameLoopEvent {
    static constexpr uint32_t kTypeId = 3;
    static constexpr EventPriority kPriority = EventPriority::High;

    uint64_t frameCount = 0;

    RenderFrameStartEvent() = default;
    explicit RenderFrameStartEvent(uint64_t frame)
        : frameCount(frame) {}

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

// 渲染帧结束事件
struct RenderFrameEndEvent : IGameLoopEvent {
    static constexpr uint32_t kTypeId = 4;
    static constexpr EventPriority kPriority = EventPriority::High;

    uint64_t frameCount = 0;

    RenderFrameEndEvent() = default;
    explicit RenderFrameEndEvent(uint64_t frame)
        : frameCount(frame) {}

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

// 游戏暂停事件
struct GamePausedEvent : IGameLoopEvent {
    static constexpr uint32_t kTypeId = 5;
    static constexpr EventPriority kPriority = EventPriority::High;

    uint64_t frameCount = 0;

    GamePausedEvent() = default;
    explicit GamePausedEvent(uint64_t frame)
        : frameCount(frame) {}

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

// 游戏恢复事件
struct GameResumedEvent : IGameLoopEvent {
    static constexpr uint32_t kTypeId = 6;
    static constexpr EventPriority kPriority = EventPriority::High;

    uint64_t frameCount = 0;

    GameResumedEvent() = default;
    explicit GameResumedEvent(uint64_t frame)
        : frameCount(frame) {}

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

// 游戏停止事件
struct GameStoppedEvent : IGameLoopEvent {
    static constexpr uint32_t kTypeId = 7;
    static constexpr EventPriority kPriority = EventPriority::High;

    uint64_t frameCount = 0;

    GameStoppedEvent() = default;
    explicit GameStoppedEvent(uint64_t frame)
        : frameCount(frame) {}

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

} // namespace ayt::game