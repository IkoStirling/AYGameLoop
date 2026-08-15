# Sim / Present 时间纪律（文档 + CR）

**Status:** Apply now (guardrails) — full `ISimClock` / DET stack later  
**Owner module:** `AYGameLoop`  
**Related:** [`../design.md`](../design.md) · [`ENGINE-DETERMINISM-ARCHITECTURE.md`](../../../ENGINE-DETERMINISM-ARCHITECTURE.md) §8

本文件是**日常代码评审**用的时间域约定。不要求现在实现 fixed-point lockstep；要求新代码挂对钩子、评审能对照检查。

---

## 1. 两套时间（先记住这个）

| 域 | GameLoop 钩子 | 时间语义 | 典型用途 |
|----|---------------|----------|----------|
| **Sim** | `ISubSystem::fixedUpdate(fixedDt)` | 固定步长（默认 1/60）；一帧可 0..N 次 | 物理步进、移动/碰撞真相、战斗判定、可存档状态推进 |
| **Present** | `ISubSystem::update(deltaTime)` | 变长帧时间（平滑后的 dt） | 渲染提交、UI、编辑器、相机、视觉动画 |

暂停 / 慢动作：优先 `IGameLoop::setTimeScale(0|0.5|…)`。  
UI / Editor 转场应声明 `SubSystemDescriptor::TimeType::Unscaled`（不受 timeScale）。

插值：`getInterpolationFactor()`（alpha）只给 Present 用来平滑显示，**不参与** Sim 判定。

---

## 2. CR 检查清单（加 SubSystem / ECS System 时）

PR / design review 必须能回答：

1. **Lane：** Sim / Present / Bridge？（写在 design 一小段或 PR 描述里）
2. **钩子：** 改游戏真相的逻辑是否只在 `fixedUpdate`（或明确标注的 Sim 路径）？
3. **时钟：** 是否避免用 `QueryPerformanceCounter` / `std::chrono::system_clock` / 未种子 `rand()` 影响玩法结果？
4. **暂停：** `timeScale==0` 时，玩法是否停、UI 是否仍可动？
5. **并行：** Present 可并行；Sim 内禁止对同一组件无并行写（见 Determinism §8.2）

### 2.1 推荐分类（当前引擎）

| 模块 / 系统 | Lane | 钩子 |
|-------------|------|------|
| `AYPhysics` step | Sim | `fixedUpdate` |
| 角色移动 / 命中 / 库存等玩法真相 | Sim | `fixedUpdate`（或未来 Sim ECS lane） |
| `RenderSystem` / GPU 提交 | Present | `update` |
| `AnimationSystem`（视觉 pose） | Present | `update`（默认） |
| AYUI / Editor chrome | Present · Unscaled | `update` |
| `AYResource` hot-reload poll | Present · Unscaled | `update`（或帧末泵） |
| 输入采样 → 玩法 | Bridge | Device 每帧采集；**消费**放在下一个 Sim tick |

若玩法必须读骨骼位置做判定：不要直接锁 Present 的 skinned 矩阵；用 Sim 侧 proxy（以后再细化，见 Determinism §8.4）。

---

## 3. `TimeType` 怎么用

`SubSystemDescriptor::TimeType` 只影响 **`update(dt)` 喂什么**：

| 值 | 含义 | 例子 |
|----|------|------|
| `Scaled`（默认） | 受 `timeScale` | 一般游戏逻辑的变长更新 |
| `Unscaled` | 不受 timeScale | UI、Editor、加载提示 |
| `Real` | 墙钟原始 dt | 少数诊断 / 音视频同步（慎用） |

**约定：** `fixedUpdate(fixedDt)` 始终是固定步长本身；积蓄多少步由 FrameManager 决定（与 timeScale 的精确关系以实现为准，评审时以「玩法是否随暂停停住」为准）。

> **实现缺口（诚实注明）：** 今日 `processVariableUpdates` 可能尚未按 `TimeType` 分流 dt。新代码仍应**正确填写** `timeType`；补接线是 GameLoop 后续小任务，不阻塞本纪律。

---

## 4. 禁止 / 允许（Sim 相关，现在就生效）

**禁止用玩法结果依赖：**

- 未种子 RNG、墙钟、文件系统枚举顺序
- `unordered_map` / `unordered_set` 迭代顺序影响判定
- 在玩法里 `GPU readback` / 依赖渲染线程时序
- 在「将来要 lockstep」的路径里直接当真相调用 Jolt（Jolt = 权威服 / 视觉物理默认路径，见 Determinism §6）

**允许：**

- Present 全链路继续用 float
- 单机 / 服务器权威：Sim 也可先用 float + `fixedUpdate`
- 完整 Fixed / Lockstep：等 DET-01+ 产品触发再建

---

## 5. 以后再加什么（不要现在做）

| 以后 | 现在 |
|------|------|
| DET fixed-point Sim 世界 / `ISimClock` 完整抽象 | **已有** `SimTickEvent` + `IGameLoop::getSimTick()` 作为共享时间锚点 |
| Replay / Lockstep 会话 | 为“以后可能联网”重写移动 |

Scene / Save / Network / Replay 订阅 `ayt::game::SimTickEvent`（每 fixed 步一次）即可对齐；分类表仍然适用。

---

## 6. 一句话

**改真相 → `fixedUpdate`；画画/UI → `update`；暂停靠 `timeScale`；确定性数学以后再说。**
