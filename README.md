# AYGameLoop

**Version:** 0.3.0
**Date:** 2026-10-07
**Status:** Active
**Owner:** Runtime / GameLoop

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

- AYCore、AYMath、AYPlatform、AYTime、AYLog、AYTask、AYEventSystem、AYModule

## 软件浮点固定步

标准 GameLoop 默认由整数比率 1/60 构造软件 binary32 固定步。可在启动前
或帧间通过公共接口配置，prepare/run 不覆盖该配置：

```cpp
auto& loop = ayt::game::IGameLoop::instance();
if (!loop.setFixedTimestepRatio(1, 60)) { /* invalid or unsupported configuration */ }
auto step = loop.getFixedStep();
// 正常 Host 自行推进；玩法不再手动 tick 同一 World。
```

`FixedTimestep::fromRatio` 先约分，再要求正整数项不超过 2^24，保证整数
转换精确、软件除法只舍入一次。`FrameContext::fixedStep` 与提交后的
`SimTickEvent::fixedStep` 携带相同步长、约分比率；标准 Entity adapter
直接使用 typed World 入口。现有子系统仍可读 `fixedDeltaTime` 的 native
位复制；调用 `setFixedTimestep(float)` 则明确选择 native 输入边界。

ratio setter 在帧回调中返回 false，旧 float setter 在回调中延后到下一帧；
同一批固定步的配置保持一致。新 session 清空墙钟与固定步积压，保留数值
配置与单调 simTick。暂停/时间缩放改变推进次数，不改变单步数值输入。
墙钟调度、追帧/丢帧策略和 native 物理/脚本仍不是跨平台 lockstep 保证。
验证入口：`Test_DeterministicTimestep.cpp`、`AYEntity_DeterministicHostTests`。

Sim/Present 时间纪律和 SubSystem 顺序见 [design.md](design.md)。
