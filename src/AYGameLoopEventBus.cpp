// GameLoopEventBus.cpp - 游戏循环事件总线实现

#include <AYGameLoopEventBus.h>

namespace ayt::game
{

GameLoopEventBus& GameLoopEventBus::instance() {
    static GameLoopEventBus bus;
    return bus;
}

void GameLoopEventBus::offEvent(uint64_t listenerId) {
    for (auto& [typeIndex, listeners] : _listeners) {
        (void)typeIndex;
        listeners.erase(
            std::remove_if(listeners.begin(), listeners.end(),
                [listenerId](const ListenerEntry& entry) { return entry.id == listenerId; }),
            listeners.end()
        );
    }
}

void GameLoopEventBus::clear() {
    _listeners.clear();
}

} // namespace ayt::game