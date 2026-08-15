#pragma once
// AYGameLoop.h - 游戏循环主入口

#include <AYGameLoop/IGameLoop.h>
#include <AYGameLoop/GameLoopImpl.h>

namespace ayt::game
{

// 实现 IGameLoop::instance() 委托到 GameLoop::instance()
inline IGameLoop& IGameLoop::instance() {
    return GameLoop::instance();
}

} // namespace ayt::game