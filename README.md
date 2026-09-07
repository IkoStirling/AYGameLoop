# AYGameLoop

AYGameLoop 是引擎循环中枢，负责 SubSystem 生命周期、固定/可变步长更新、帧统计、插值、任务调度和 EventBus pump。

## 公开接口

```cpp
#include <AYGameLoop.h>
#include <AYGameLoop/IGameLoop.h>
#include <AYGameLoop/SubSystemRegistry.h>
#include <AYGameLoop/SubSystemModule.h>
#include <AYGameLoop/FrameInterpolator.h>
```

稳定接口位于 `interface/AYGameLoop/`，运行时实现头位于 `include/AYGameLoop/`。

`SubSystemModule` 是 AYModule 与 GameLoop 之间的通用适配层：模块负责声明依赖和创建
`ISubSystem`，Host 负责将其安装到自己的 GameLoop，并在模块关闭时撤销所有权记录。
无参 `Factory` 适合独立 SubSystem；`ContextFactory` 在安装阶段接收
`IModuleContext`，供 Online 这类节点读取前置模块已经发布的 SubSystem 或 Host 服务。
默认 Client / Server / Editor 都通过该路径装配核心运行时子系统；直接向 GameLoop 注册
仍保留给独立 Demo 和兼容代码。

同一依赖层内按 `basePriority` 从高到低执行；优先级相同时严格保留注册顺序，结果不受
内部哈希表遍历顺序影响。`SubSystemModule` 默认拒绝同名的预注册 SubSystem，避免旧启动
路径与模块路径静默混用；只有确认实例和配置兼容时，才显式选择
`ExistingSubSystemPolicy::Adopt`，且采用的实例仍由原注册方负责撤销。

## 依赖

- AYCore、AYPlatform、AYTime、AYLog、AYTask、AYEventSystem、AYModule

Sim/Present 时间纪律和 SubSystem 顺序见 [design.md](design.md)。
