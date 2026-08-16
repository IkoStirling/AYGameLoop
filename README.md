# AYGameLoop

AYGameLoop 是引擎循环中枢，负责 SubSystem 生命周期、固定/可变步长更新、帧统计、插值、任务调度和 EventBus pump。

## 公开接口

```cpp
#include <AYGameLoop.h>
#include <AYGameLoop/IGameLoop.h>
#include <AYGameLoop/SubSystemRegistry.h>
#include <AYGameLoop/FrameInterpolator.h>
```

稳定接口位于 `interface/AYGameLoop/`，运行时实现头位于 `include/AYGameLoop/`。

## 依赖

- AYCore、AYPlatform、AYTime、AYLog、AYTask、AYEventSystem

Sim/Present 时间纪律和 SubSystem 顺序见 [design.md](design.md)。
