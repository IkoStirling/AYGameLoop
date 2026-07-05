#pragma once
// GameLoopEventBus.h - 游戏循环事件总线
// 简单的观察者模式实现，后续可与 AYEventSystem 集成

#include <GameLoopEvents.h>
#include <functional>
#include <vector>
#include <unordered_map>
#include <typeindex>

namespace ayt::game
{

// 事件监听器
using EventListener = std::function<void(IGameLoopEvent&)>;

// 事件总线
class GameLoopEventBus {
public:
    static GameLoopEventBus& instance();

    // 订阅事件
    template<typename EventType>
    uint64_t onEvent(std::function<void(EventType&)> listener) {
        uint64_t id = _nextListenerId++;
        auto typeIdx = std::type_index(typeid(EventType));
        _listeners[typeIdx].push_back({id, [listener](IGameLoopEvent& e) {
            listener(static_cast<EventType&>(e));
        }});
        return id;
    }

    // 取消订阅
    void offEvent(uint64_t listenerId);

    // 发布事件（已知类型，已构造对象）
    template<typename EventType>
    void emit(EventType& event) {
        auto it = _listeners.find(std::type_index(typeid(EventType)));
        if (it != _listeners.end()) {
            for (auto& entry : it->second) {
                entry.callback(event);
            }
        }
    }

    // 清空所有监听器
    void clear();

private:
    GameLoopEventBus() = default;
    ~GameLoopEventBus() = default;

    struct ListenerEntry {
        uint64_t id;
        EventListener callback;
    };

    std::unordered_map<std::type_index, std::vector<ListenerEntry>> _listeners;
    uint64_t _nextListenerId = 1;
};

// 便捷宏
#define GAME_LOOP_EVENT_BUS ayt::game::GameLoopEventBus::instance()

} // namespace ayt::game