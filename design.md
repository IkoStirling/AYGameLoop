# AYGameLoop Design

## 0. Reading guide

| Audience | Read |
|----------|------|
| All engineers adding SubSystems / gameplay systems | [`docs/sim-present-time.md`](docs/sim-present-time.md) — **Sim vs Present CR 纪律（现在就遵守）** |
| Lockstep / bit-exact replay (later) | [`ENGINE-DETERMINISM-ARCHITECTURE.md`](../../ENGINE-DETERMINISM-ARCHITECTURE.md) |

## 1. 概述

AYGameLoop 是 AY Engine 的**游戏循环中枢**，负责：
- 游戏主循环（主线程 + 渲染线程）
- 子系统注册与调度
- 时间管理（deltaTime、fixed timestep）
- 暂停/恢复/帧率控制
- 渲染/逻辑分离

### 1.1 设计目标

- **子系统组件化**：子系统可注册、带依赖元数据、自动排序
- **双线程架构**：主线程处理逻辑，渲染线程独立执行渲染
- **固定时间步**：物理等模块需要固定时间步长
- **事件驱动**：通过 AYEventSystem 分发循环事件
- **Job 并行**：通过 AYTask 支持子系统间并行调度
- **帧率精确控制**：防止帧时间抖动
- **DeltaTime 平滑**：防止卡顿后物理爆炸
- **帧插值**：渲染平滑动画
- **性能统计**：实时 FPS、帧时间监控

### 1.2 在引擎中的位置

```
┌─────────────────────────────────────────────────────────────────┐
│                        Game Engine                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                     AYGameLoop                            │   │
│  │                    (游戏循环中枢)                         │   │
│  │  ┌────────────────────────────────────────────────────┐   │   │
│  │  │  SubSystemRegistry (子系统注册表)                  │   │   │
│  │  │  FrameManager (帧率/时间/统计)                     │   │   │
│  │  │  RenderThread (渲染线程)                          │   │   │
│  │  │  JobScheduler (AYTask 任务调度)                   │   │   │
│  │  │  FrameInterpolator (帧插值)                      │   │   │
│  │  └────────────────────────────────────────────────────┘   │   │
│  └────────────────────────┬────────────────────────────────┘   │
│                           │                                      │
│       ┌───────────────────┼───────────────────┐                 │
│       │                   │                   │                 │
│       ▼                   ▼                   ▼                 │
│  ┌─────────┐      ┌─────────────┐     ┌─────────┐              │
│  │AYDevice │      │ AYRenderer  │     │AYPhysics│              │
│  │ (输入)  │      │  (渲染)     │     │ (物理)  │              │
│  └────┬────┘      └──────┬──────┘     └────┬────┘              │
│       │                  │                  │                   │
│       │           ┌──────┴──────┐           │                   │
│       │           │ AYResource  │           │                   │
│       │           │  (资源)     │           │                   │
│       │           └─────────────┘           │                   │
│       │                  ▲                  │                   │
│       └──────────────────┼──────────────────┘                   │
│                          │                                      │
│                   ┌──────▼──────┐                               │
│                   │AYEventSystem│                               │
│                   │ (事件中枢)  │                               │
│                   └─────────────┘                               │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. 核心架构

### 2.1 子系统组件化

子系统通过描述符注册，包含依赖和优先级信息：

```cpp
struct SubSystemDescriptor {
    const char* name;                           // 子系统名称
    std::vector<const char*> dependencies;       // 依赖的其他子系统
    int32_t basePriority;                        // 基础优先级
    
    // 时间类型（影响 update() 传入的时间）
    enum class TimeType : uint8_t {
        Scaled,     // 受 timeScale 影响（游戏逻辑）
        Unscaled,   // 不受 timeScale 影响（UI、编辑器）
        Real        // 真实时间，不受任何缩放（物理等）
    };
    TimeType timeType = TimeType::Scaled;
};

// 子系统基类
class ISubSystem {
public:
    virtual ~ISubSystem() = default;
    virtual const char* getName() const = 0;
    virtual const SubSystemDescriptor& getDescriptor() const = 0;
    
    // 生命周期
    virtual bool initialize() = 0;
    virtual void update(float deltaTime) = 0;
    virtual void fixedUpdate(float fixedDeltaTime) = 0;
    virtual void shutdown() = 0;
    
    // 时间类型由 getDescriptor().timeType 决定：
    // - TimeType::Scaled    → update(dt) 传入缩放后的时间 (受 timeScale 影响)
    // - TimeType::Unscaled  → update(dt) 传入未缩放的时间 (不受 timeScale 影响)
    // - TimeType::Real      → update(dt) 传入真实时间 (物理等)
    //
    // fixedUpdate() 始终使用固定时间步，不受任何时间缩放影响
};

// 子系统注册宏
#define REGISTER_SUBSYSTEM(cls, deps, priority) \
    static_assert(std::is_base_of_v<ISubSystem, cls>, #cls " must inherit ISubSystem"); \
    namespace { \
        struct cls##_Registrar { \
            cls##_Registrar() { \
                SubSystemRegistry::instance().registerSystem<cls>(#cls, deps, priority); \
            } \
        }; \
        static cls##_Registrar g_registrar; \
    }
```

### 2.2 依赖排序

子系统按依赖关系自动拓扑排序：

```cpp
class SubSystemRegistry {
public:
    template<typename T>
    void registerSystem(const char* name, 
                        std::vector<const char*> deps,
                        int32_t priority) {
        SubSystemDescriptor desc;
        desc.name = name;
        desc.dependencies = std::move(deps);
        desc.basePriority = priority;
        
        _descriptors[name] = desc;
        _creators[name] = []() -> std::unique_ptr<ISubSystem> {
            return std::make_unique<T>();
        };
    }
    
    // 获取拓扑排序后的执行顺序
    std::vector<ISubSystem*> getExecutionOrder() {
        // Kahn's algorithm 拓扑排序
        // 考虑 basePriority 作为 tie-breaker
    }

private:
    std::unordered_map<std::string, SubSystemDescriptor> _descriptors;
    std::unordered_map<std::string, std::function<std::unique_ptr<ISubSystem>() > > _creators;
};
```

### 2.3 双线程架构

```
┌─────────────────────────────────────────────────────────────────┐
│                      主线程 (Game Thread)                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  [Logic Frame N]                                                 │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ 1. Input Processing                                     │   │
│  │    └─ AYDevice::pollDevices() → InputActionEvent        │   │
│  │                                                          │   │
│  │ 2. Fixed Update (Physics)                                │   │
│  │    └─ AYPhysics::step(fixedDt) [多次，固定时间步]       │   │
│  │                                                          │   │
│  │ 3. Variable Update (Game Logic)                          │   │
│  │    ├─ Animation::evaluate(dt)                            │   │
│  │    ├─ Script/AI update                                    │   │
│  │    └─ Scene graph update                                 │   │
│  │                                                          │   │
│  │ 4. Resource Preload                                      │   │
│  │    └─ AYResource::preloadPending()                        │   │
│  │                                                          │   │
│  │ 5. Submit Render Commands + Interpolation Factor          │   │
│  │    └─ RenderCommandBuffer::submit() → Signal Render       │   │
│  └──────────────────────────────────────────────────────────┘   │
│                          ↓                                       │
│                   RenderThread ──────────────────────────────► GPU│
│                          ↑                                       │
│  [Render Frame N]        │                                      │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ 1. Wait for Logic Frame N Signal                          │   │
│  │ 2. Execute Render Commands (with interpolation)          │   │
│  │    ├─ RenderPipeline::beginFrame()                        │   │
│  │    ├─ Collect RenderItems (use interpolated transforms)  │   │
│  │    └─ RenderPipeline::execute()                          │   │
│  │ 3. Frame buffer swap                                     │   │
│  │ 4. Signal Logic Thread (Frame N+1 can proceed)           │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

### 2.4 线程同步

```cpp
class RenderThreadSync {
public:
    // 主线程：等待上一帧渲染完成
    void waitForRenderComplete() {
        _renderComplete.acquire();  // semaphore
    }
    
    // 渲染线程：通知主线程渲染完成
    void signalRenderComplete() {
        _renderComplete.release();
    }
    
    // 主线程：通知渲染线程逻辑帧完成
    void signalLogicComplete() {
        _logicComplete.release();
    }
    
    // 渲染线程：等待逻辑帧完成
    void waitForLogicComplete() {
        _logicComplete.acquire();
    }

private:
    Semaphore _logicComplete{0};   // 逻辑帧完成信号
    Semaphore _renderComplete{0};   // 渲染帧完成信号
};
```

### 2.5 Job 并行 (AYTask)

AYTask 的 TaskGraph 支持 DAG 并行。AYGameLoop 采用 **layer-by-layer DAG**
策略：每个 layer 内的子系统并发执行，layer 间串行。

```cpp
// 由 GameLoop::buildExecutionLayers() + runVariableUpdatesParallel() 实现
//
// 例如：注册 5 个子系统，依赖图：
//
//   Render ──┐
//            ├──► Composite ──► Output
//   Physics ─┘
//   Audio ──────────► Output
//
// → Layer 0: { Render, Audio }          (并发)
// → Layer 1: { Composite }              (依赖 Layer 0 全部)
// → Layer 2: { Output }                 (依赖 Composite)
//
// Layer 内的 subsystem->update 在同一个 TaskGraph 内并行（无 intra-layer 边）；
// Layer 间通过在下一层所有节点上 depend 上一层所有节点实现串行。
```

**约束**：layer 内并发的子系统 `update()` 必须**无共享状态依赖**，否则会
race。设计原则：**互相独立的子系统放同一 layer**；有数据依赖的子系统
放在后续 layer。

---

## 3. 时间管理系统

### 3.0 软件 binary32 固定步（2026-10-07）

`FixedTimestep` 是不可变数值输入。默认 1/60；ratio 先以 gcd 约分，正整数
项必须 <=2^24，先精确转为 DetFloat32，再以 profile 1 软件除法一次舍入。
native factory 仅复制有限正 float bits，约分项为 0/0，用于兼容边界。
FrameManager 保存该值和一致的 native float 副本；无效 ratio 返回 false
且不改变配置或 accumulator。旧非法 float 仍回退到默认 1/60。

GameLoop 在主线程配置；ratio 变更拒绝发生在帧执行中，旧 float setter
延后到下一帧初始边界。帧作用域 guard 含所有 early return，并拒绝嵌套
frame；三种固定 phase 与同批所有 tick 看到同一配置。FrameContext 的
optional fixedStep 和成功提交后 SimTickEvent 的 optional fixedStep 都由
Host 设置；手工 legacy context/event 可为空。ISubSystem 默认 float adapter
保持可用，Entity FixedPrePhysics 直接传 DetFloat32 给 World。

initialize 重置帧时钟与未消费积压，保留 fixed 配置；simTick 单调不重置。
固定 count 在 double 中先 clamp 到预算再转 int，避免极小 legacy dt 的
超范围转换。Wall-clock accumulator、timeScale、backlog policy 仍为 native
调度，它们决定执行多少 tick；本实现保证每个已选 tick 的数值输入，不能
替代 lockstep 输入门禁或 rollback。phase 失败不提交 simTick/事件，也不
自动撤销之前 phase 的副作用。旋转、物理与脚本确定性仍待扩展。

### 3.1 DeltaTime 平滑

防止卡顿后物理爆炸。计算平滑后的 deltaTime：

```cpp
class FrameManager {
public:
    using Clock = std::chrono::steady_clock;
    
    FrameManager() : _startTime(Clock::now()), _lastFrameTime(_startTime) {}
    
    // 目标帧率
    float getTargetFPS() const { return _targetFPS; }
    void setTargetFPS(float fps) {
        _targetFPS = std::clamp(fps, 1.0f, 10000.0f);
        _invTargetFPS = 1.0f / _targetFPS;
    }

    // 固定时间步
    float getFixedTimestep() const { return _fixedDt; }
    void setFixedTimestep(float dt) { _fixedDt = dt; }

    // 时间缩放
    float getTimeScale() const { return _timeScale; }
    void setTimeScale(float scale) { _timeScale = std::max(0.0f, scale); }

    // 获取真实时间（不受任何缩放影响）
    float getRealDeltaTime() const { return _realDeltaTime; }

    // 开始帧（每帧调用，计算时间）
    void beginFrame() {
        auto now = Clock::now();
        _unscaledDeltaTime = std::chrono::duration<float>(now - _lastFrameTime).count();
        _lastFrameTime = now;
        
        // 计算真实时间（从程序启动到现在，用于 TimeType::Real）
        _realDeltaTime = std::chrono::duration<float>(now - _startTime).count();
        
        // 防止第一帧或极端情况
        if (_unscaledDeltaTime > 0.5f) {
            _unscaledDeltaTime = _invTargetFPS;  // 限制最大帧时间
        }
        
        // 平滑处理：指数移动平均
        _smoothDeltaTime = _smoothDeltaTime * 0.8f + _unscaledDeltaTime * 0.2f;
        
        // 应用时间缩放（scaled dt 由 GameLoop 计算）
    }

    // 结束帧（更新统计）
    void endFrame() {
        _elapsedTime += _unscaledDeltaTime;
        _frameCount++;
        updateFPSStats();
    }

private:
    // 平滑 deltaTime
    float _smoothDeltaTime = 0.0f;
    float _realDeltaTime = 0.0f;  // 真实时间
    Clock::time_point _startTime;  // 程序启动时间
    
    // 帧率统计
    void updateFPSStats() {
        _fpsAccumulator += _unscaledDeltaTime;
        _fpsFrameCount++;
        if (_fpsAccumulator >= 1.0f) {
            _currentFPS = static_cast<float>(_fpsFrameCount) / _fpsAccumulator;
            _fpsFrameCount = 0;
            _fpsAccumulator = 0.0f;
        }
    }
};
```

### 3.2 帧率精确控制

防止帧时间抖动，精确睡眠 + 忙等待：

```cpp
class FrameManager {
public:
    using Clock = std::chrono::steady_clock;

    void regulateFrameRate(Clock::time_point frameStart) {
        if (_noLimitFPS) return;
        
        using namespace std::chrono;
        auto targetDuration = microseconds(static_cast<int64_t>(1000000.0f * _invTargetFPS));
        
        auto now = Clock::now();
        auto elapsed = now - frameStart;
        
        if (elapsed < targetDuration) {
            auto remaining = targetDuration - elapsed;
            
            // 三阶段等待策略
            if (remaining > 2ms) {
                std::this_thread::sleep_for(remaining - 1ms);
                busyWait(frameStart + targetDuration);
            }
            else if (remaining > 500us) {
                std::this_thread::sleep_for(remaining - 100us);
                busyWait(frameStart + targetDuration);
            }
            else {
                busyWait(frameStart + targetDuration);
            }
        }
    }

private:
    // 忙等待到精确时间
    void busyWait(Clock::time_point targetTime) {
        while (Clock::now() < targetTime) {
            // PAUSE 指令减少 CPU 功耗
            #if defined(AYT_MSVC)
                _mm_pause();
            #else
                __builtin_ia32_pause();
            #endif
        }
    }
    
    bool _noLimitFPS = false;
};
```

### 3.3 时间类型系统

GameLoop 支持三种时间类型，子系统根据类型获取不同的时间值：

```cpp
enum class TimeType : uint8_t {
    Scaled,     // 受 timeScale 影响（游戏逻辑）
    Unscaled,   // 不受 timeScale 影响（UI、编辑器）
    Real        // 真实时间，不受任何缩放（物理等）
};

// 获取不同类型的时间
float getScaledDeltaTime() const;     // unscaledDt * timeScale
float getUnscaledDeltaTime() const;    // 原始帧时间
float getRealDeltaTime() const;         // 真实流逝时间
```

**时间流向**：

```
真实时间 (Clock::now())
     │
     ├── TimeType::Real ──────────────────► 物理等（不受 timeScale 影响）
     │                                   
     ├── TimeType::Unscaled ───────────► UI、编辑器、音频（暂停时仍运行）
     │                                   
     └── timeScale 缩放
              │
              └── TimeType::Scaled ────► 游戏逻辑（暂停时停止）
```

**暂停场景**：

| 子系统 | TimeType | 暂停时行为 |
|--------|----------|-----------|
| Physics | Real | 继续运行（固定时间步） |
| Game Logic | Scaled | 暂停（dt = 0） |
| Animation | Scaled | 暂停（dt = 0） |
| UI | Unscaled | 继续运行（不受暂停影响） |
| Audio | Unscaled | 继续运行（不受暂停影响） |
| Editor | Unscaled | 继续运行（编辑器模式下） |

**使用示例**：

```cpp
// Physics - Real 时间，固定步长
class PhysicsSubSystem : public ISubSystem {
    const SubSystemDescriptor& getDescriptor() const override {
        static SubSystemDescriptor desc = {
            .name = "Physics",
            .timeType = TimeType::Real
        };
        return desc;
    }
    
    void update(float realDt) override { /* 不使用，物理用 fixedUpdate */ }
    void fixedUpdate(float fixedDt) override { _physics.step(fixedDt); }
};

// GameLogic - Scaled 时间，受暂停影响
class GameLogicSubSystem : public ISubSystem {
    const SubSystemDescriptor& getDescriptor() const override {
        static SubSystemDescriptor desc = {
            .name = "GameLogic",
            .timeType = TimeType::Scaled
        };
        return desc;
    }
    
    void update(float scaledDt) override { /* scaledDt = 0 当暂停时 */ }
};

// UI - Unscaled 时间，暂停时继续运行
class UISubSystem : public ISubSystem {
    const SubSystemDescriptor& getDescriptor() const override {
        static SubSystemDescriptor desc = {
            .name = "UI",
            .timeType = TimeType::Unscaled
        };
        return desc;
    }
    
    void update(float unscaledDt) override { /* 暂停时仍运行 */ }
};
```

**实现**：

```cpp
void GameLoop::processVariableUpdates() {
    float scaledDt = _unscaledDeltaTime * _timeScale;
    float unscaledDt = _unscaledDeltaTime;
    float realDt = getRealDeltaTime();
    
    for (auto* system : _executionOrder) {
        auto timeType = system->getDescriptor().timeType;
        float dt;
        
        switch (timeType) {
            case TimeType::Scaled:     dt = scaledDt; break;
            case TimeType::Unscaled:   dt = unscaledDt; break;
            case TimeType::Real:       dt = realDt; break;
        }
        
        system->update(dt);
    }
}
```

---

### 3.4 帧插值 (Interpolation)

渲染时使用插值平滑动画：

```cpp
class FrameInterpolator {
public:
    // 在逻辑帧结束时调用，计算插值因子
    void onLogicFrameEnd(float fixedDt, float realDt) {
        // 插值因子：0.0 = 完全旧帧，1.0 = 完全新帧
        _interpolationFactor = std::min(1.0f, realDt / fixedDt);
    }
    
    // 获取插值因子
    float getInterpolationFactor() const { return _interpolationFactor; }
    
    // 插值两个变换
    Transform interpolate(const Transform& prev, const Transform& curr) {
        return Transform::lerp(prev, curr, _interpolationFactor);
    }
    
    // 插值位置
    FVector3 interpolatePosition(const FVector3& prev, const FVector3& curr) {
        return FVector3::lerp(prev, curr, _interpolationFactor);
    }
    
    // 插值旋转（四元数 slerp）
    FQuaternion interpolateRotation(const FQuaternion& prev, const FQuaternion& curr) {
        return FQuaternion::slerp(prev, curr, _interpolationFactor);
    }

private:
    float _interpolationFactor = 0.0f;  // [0, 1]
};
```

**插值使用场景**：
- 角色移动位置
- 相机变换
- 骨骼动画混合

---

## 4. 核心接口

### 4.1 IGameLoop

```cpp
enum class PresentationOwnership : uint8_t {
    GameLoop,     // GameLoop submits its own render frame
    ExternalHost  // host composites and submits the frame
};

struct HostedFrameContext {
    float realWallDeltaTime = 0.0f;
    uint64_t hostFrameIndex = 0;
    uint64_t inputFrameIndex = 0;
};

// FrameContext delivered to each subsystem includes the two host indices.
// In a standalone run they are both zero.

class IGameLoop {
public:
    virtual ~IGameLoop() = default;

    // Threading contract (see §8.5):
    //   - run / tickOnce / stepOnce / preparePlaySession / endPlaySession
    //     / prepareHostedSession / endHostedSession
    //     and per-frame accessors (getDeltaTime, getFrameCount, ...) are
    //     **main-thread only**.
    //   - stop / pause / resume are any-thread safe (atomic flag).
    //   - registerSubSystem / onUpdate / offUpdate are mutex-protected and
    //     any-thread safe.

    // === 生命周期 ===
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    // === 循环控制 ===
    virtual void run() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual bool preparePlaySession() = 0;  // editor / replay entry
    virtual void endPlaySession() = 0;
    // Explicit names for a host that drives tickOnce itself.  The default
    // adapters preserve the legacy play-session lifecycle.
    virtual bool prepareHostedSession() { return preparePlaySession(); }
    virtual void endHostedSession() { endPlaySession(); }
    virtual void tickOnce() = 0;            // drive from host message loop
    virtual void tickHostedFrame(const HostedFrameContext& context) = 0;
    virtual void stepOnce() = 0;            // single fixed-step frame
    virtual bool isPlaySessionActive() const = 0;

    // Who consumes the immutable render packet produced by Presentation.
    // ExternalHost means GameLoop does not issue a separate render submission.
    virtual void setPresentationOwnership(PresentationOwnership ownership) = 0;
    virtual PresentationOwnership getPresentationOwnership() const = 0;

    // === 配置 ===
    virtual void setTargetFPS(float fps) = 0;
    virtual void setFixedTimestep(float dt) = 0;
    virtual void setTimeScale(float scale) = 0;
    virtual void setNoLimitFPS(bool noLimit) = 0;

    // === 时间查询 ===
    virtual float getDeltaTime() const = 0;
    virtual float getUnscaledDeltaTime() const = 0;
    virtual float getRealDeltaTime() const = 0;
    virtual float getElapsedTime() const = 0;
    virtual uint64_t getFrameCount() const = 0;
    virtual float getInterpolationFactor() const = 0;

    // === 帧率统计 ===
    virtual float getCurrentFPS() const = 0;
    virtual float getFrameTimeMS() const = 0;

    // === 子系统注册 ===
    // 1-arg form: descriptor from subsystem's getDescriptor().
    virtual void registerSubSystem(ISubSystem* system) = 0;
    // 3-arg form: dependencies and priority OVERRIDE the descriptor.
    // Used by REGISTER_SUBSYSTEM macro.
    virtual void registerSubSystem(ISubSystem* system,
                                   std::vector<const char*> dependencies,
                                   int32_t priority) = 0;
    virtual void unregisterSubSystem(const char* name) = 0;

    // === 回调 ===
    using UpdateCallback = std::function<void(float deltaTime)>;
    virtual uint64_t onUpdate(UpdateCallback callback) = 0;
    virtual void offUpdate(uint64_t listenerId) = 0;

    // === Render thread + parallel execution ===
    virtual void setRenderThreadEnabled(bool enabled) = 0;
    virtual void setRenderCallback(std::function<void()> callback) = 0;
    virtual void setParallelEnabled(bool enabled) = 0;
    virtual bool isParallelEnabled() const = 0;
};
```

### 4.2 ISubSystem

```cpp
class ISubSystem {
public:
    virtual ~ISubSystem() = default;

    virtual const char* getName() const = 0;
    virtual const SubSystemDescriptor& getDescriptor() const = 0;

    virtual bool initialize() = 0;
    virtual void update(float deltaTime) = 0;
    virtual void fixedUpdate(float fixedDeltaTime) = 0;
    virtual void shutdown() = 0;
};
```

### 4.3 GameLoop 主类

```cpp
class GameLoop : public IGameLoop {
public:
    static GameLoop& instance();

    bool initialize() override;
    void shutdown() override;

    void run() override;
    void stop() override;
    void pause() override;
    void resume() override;

    void setTargetFPS(float fps) override;
    void setFixedTimestep(float dt) override;
    void setTimeScale(float scale) override;
    void setNoLimitFPS(bool noLimit) override;

    float getDeltaTime() const override { return _scaledDt; }
    float getUnscaledDeltaTime() const override { return _unscaledDeltaTime; }
    float getRealDeltaTime() const override { return _realDt; }
    float getElapsedTime() const override { return _elapsedTime; }
    uint64_t getFrameCount() const override { return _frameCount; }
    float getInterpolationFactor() const override { return _interpolator.getInterpolationFactor(); }

    float getCurrentFPS() const override { return _frameManager.getCurrentFPS(); }
    float getFrameTimeMS() const override { return _frameManager.getFrameTimeMS(); }

    void registerSubSystem(ISubSystem* system) override;
    void unregisterSubSystem(const char* name) override;

    uint64_t onUpdate(UpdateCallback callback) override;
    void offUpdate(uint64_t listenerId) override;

private:
    GameLoop() = default;
    ~GameLoop() = default;

    void mainLoop();                      // 主循环
    void renderLoop();                   // 渲染线程循环

    void processFixedUpdates();           // 固定时间步
    void processVariableUpdates();        // 可变时间步

    void submitRenderCommands();         // 提交渲染命令
    void regulateFrameRate(Clock::time_point frameStart);            // 帧率控制

    // 子系统管理
    SubSystemRegistry _registry;
    std::vector<std::unique_ptr<ISubSystem> > _systems;
    std::vector<ISubSystem*> _executionOrder;

    // 时间管理
    FrameManager _frameManager;
    FrameInterpolator _interpolator;
    float _targetFPS = 60.0f;
    float _fixedDt = 1.0f / 60.0f;
    float _scaledDt = 0.0f;      // unscaledDt * timeScale
    float _unscaledDeltaTime = 0.0f;  // 原始帧时间
    float _realDt = 0.0f;       // 真实时间
    float _elapsedTime = 0.0f;
    float _timeScale = 1.0f;
    uint64_t _frameCount = 0;
    bool _noLimitFPS = false;
    using Clock = std::chrono::steady_clock;

    // 线程同步
    std::thread _renderThread;
    RenderThreadSync _sync;
    std::atomic<bool> _running{false};
    std::atomic<bool> _paused{false};
};
```

### 4.4 子系统注册示例

```cpp
// Device 子系统
class DeviceSubSystem : public ISubSystem {
public:
    const char* getName() const override { return "Device"; }
    const SubSystemDescriptor& getDescriptor() const override {
        static SubSystemDescriptor desc = {
            .name = "Device",
            .dependencies = {},
            .basePriority = 0   // 最先执行
        };
        return desc;
    }
    
    bool initialize() override;
    void update(float dt) override { /* poll devices */ }
    void fixedUpdate(float dt) override {}  // 不需要固定步
    void shutdown() override;
};
REGISTER_SUBSYSTEM(DeviceSubSystem, {}, 0);

// Physics 子系统 - 使用 Real 时间 + 固定时间步
class PhysicsSubSystem : public ISubSystem {
public:
    const char* getName() const override { return "Physics"; }
    const SubSystemDescriptor& getDescriptor() const override {
        static SubSystemDescriptor desc = {
            .name = "Physics",
            .dependencies = {"Device"},  // 需要输入
            .basePriority = 100,
            .timeType = SubSystemDescriptor::TimeType::Real  // 使用真实时间
        };
        return desc;
    }
    
    bool initialize() override;
    void update(float realDt) override {}  // TimeType::Real 不受暂停影响
    void fixedUpdate(float fixedDt) override { /* Jolt step */ }
    void shutdown() override;
};
REGISTER_SUBSYSTEM(PhysicsSubSystem, {"Device"}, 100);

// UI 子系统 - 使用 Unscaled 时间，暂停时继续运行
class UISubSystem : public ISubSystem {
public:
    const char* getName() const override { return "UI"; }
    const SubSystemDescriptor& getDescriptor() const override {
        static SubSystemDescriptor desc = {
            .name = "UI",
            .timeType = SubSystemDescriptor::TimeType::Unscaled  // 不受 timeScale 影响
        };
        return desc;
    }
    
    bool initialize() override;
    void update(float unscaledDt) override { /* 暂停时仍运行 */ }
    void fixedUpdate(float dt) override {}
    void shutdown() override;
};
REGISTER_SUBSYSTEM(UISubSystem, {}, 200);
```

---

## 5. 循环事件

### 5.1 预定义事件

```cpp
// 逻辑帧开始
struct LogicFrameStartEvent : IEvent {
    static constexpr EventPriority kPriority = EventPriority::Normal;
    uint64_t frameCount;
    float unscaledDeltaTime;
    float smoothDeltaTime;

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

// 逻辑帧结束
struct LogicFrameEndEvent : IEvent {
    static constexpr EventPriority kPriority = EventPriority::Normal;
    uint64_t frameCount;
    float deltaTime;
    float interpolationFactor;

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

// 渲染帧开始
struct RenderFrameStartEvent : IEvent {
    static constexpr EventPriority kPriority = EventPriority::High;
    uint64_t frameCount;
    float interpolationFactor;

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

// 渲染帧结束
struct RenderFrameEndEvent : IEvent {
    static constexpr EventPriority kPriority = EventPriority::High;
    uint64_t frameCount;
    float frameTimeMS;
    float currentFPS;

    uint32_t typeId() const override { return kTypeId; }
    EventPriority priority() const override { return kPriority; }
};

// 暂停/恢复
struct PauseEvent : IEvent { /* ... */ };
struct ResumeEvent : IEvent { /* ... */ };
```

### 5.2 事件发布时机

```cpp
void GameLoop::mainLoop() {
    using Clock = std::chrono::steady_clock;
    
    while (_running && !_shouldExit) {
        _frameStartTime = Clock::now();
        
        // 等待上一帧渲染完成
        _sync.waitForRenderComplete();
        
        // 计算时间
        _frameManager.beginFrame();
        _unscaledDeltaTime = _frameManager.getUnscaledDeltaTime();
        _scaledDt = _unscaledDeltaTime * _timeScale;
        _realDt = _frameManager.getRealDeltaTime();
        
        // 发布 LogicFrameStartEvent
        EventBus::emit(LogicFrameStartEvent{
            _frameCount, 
            _unscaledDeltaTime
        });
        
        // 处理固定时间步
        processFixedUpdates();
        
        // 处理可变时间步
        processVariableUpdates();
        
        // 更新插值器
        _interpolator.onLogicFrameEnd(_fixedDt, _unscaledDeltaTime);
        
        // 发布 LogicFrameEndEvent
        EventBus::emit(LogicFrameEndEvent{
            _frameCount, 
            _scaledDt,
            _interpolator.getInterpolationFactor()
        });
        
        // 提交渲染命令，通知渲染线程
        submitRenderCommands();
        _sync.signalLogicComplete();
        
        _frameManager.endFrame();
        
        // 帧率控制
        if (!_noLimitFPS) {
            regulateFrameRate(_frameStartTime);
        }
        
        _frameCount++;
    }
}

void GameLoop::renderLoop() {
    while (_running) {
        _sync.waitForLogicComplete();
        
        // 发布 RenderFrameStartEvent
        EventBus::emit(RenderFrameStartEvent{
            _renderFrameCount,
            _interpolator.getInterpolationFactor()
        });
        
        _renderer->execute();
        
        // 发布 RenderFrameEndEvent
        EventBus::emit(RenderFrameEndEvent{
            _renderFrameCount,
            _frameManager.getFrameTimeMS(),
            _frameManager.getCurrentFPS()
        });
        
        // 通知主线程渲染完成
        _sync.signalRenderComplete();
        
        _renderFrameCount++;
    }
}
```

---

## 6. 目录结构

```
AYGameLoop/
├── design.md
├── AYGameLoop.h           # 主入口
├── CMakeLists.txt
├── interface/
│   ├── IAYGameLoop.h          # 游戏循环接口
│   └── ISubSystem.h           # 子系统接口
│
├── include/    
│   ├── AYGameLoop/GameLoopImpl.h         # GameLoop 主类
│   ├── AYGameLoop/SubSystemRegistry.h    # 子系统注册表
│   ├── AYSubSystemDescriptor.h   # 子系统描述符
│   ├── AYGameLoop/FrameManager.h         # 时间/帧率/统计管理
│   ├── AYGameLoop/FrameInterpolator.h    # 帧插值
│   ├── AYGameLoop/RenderThreadSync.h     # 线程同步
│   └── AYGameLoop/GameLoopEvents.h       # 循环事件定义
│
└── src/
    ├── AYGameLoopImpl.cpp
    ├── AYGameLoop.cpp
    ├── AYSubSystemRegistry.cpp
    ├── AYFrameManager.cpp
    ├── AYFrameInterpolator.cpp
    └── AYRenderThreadSync.cpp
```

---

## 7. 实现优先级

### Phase 1: 核心（单线程）
- [x] GameLoop 单例
- [x] 主循环 run/stop/pause/resume
- [x] FrameManager 时间管理
- [x] SubSystemRegistry 子系统注册
- [x] 依赖排序执行

### Phase 2: 双线程
- [x] RenderThread 渲染线程
- [x] RenderThreadSync 信号量同步
- [x] 渲染/逻辑分离

### Phase 3: 帧率控制
- [x] 精确睡眠 + 忙等待
- [x] 平滑 deltaTime
- [x] FPS 统计
- [x] 性能统计 API

### Phase 4: 帧插值
- [x] FrameInterpolator 插值器
- [x] 位置插值
- [x] 旋转插值 (slerp)
- [x] 变换插值

### Phase 5: 事件集成
- [x] 循环事件定义
- [x] 事件发布时机
- [x] 与 AYEventSystem 集成

### Phase 6: Job 并行
- [x] AYTask TaskGraph 集成
- [x] 可并行的子系统自动并行
- [x] Job 依赖声明

---

## 8. 变更记录

### 8.1 时间类型系统

 AYGameLoop 支持三种时间类型，子系统根据 `timeType` 获取不同的时间：

| 成员变量 | 类型 | 说明 |
|----------|------|------|
| `_scaledDt` | `float` | `unscaledDt * timeScale`，受时间缩放影响 |
| `_unscaledDeltaTime` | `float` | 原始帧时间，不受 `timeScale` 影响 |
| `_realDt` | `float` | 真实流逝时间，从程序启动开始计算 |

**变更原因**：支持 UI、编辑器等在游戏暂停时仍需继续运行的系统。

### 8.2 FrameManager 成员变更

**新增成员**：

| 成员 | 类型 | 说明 |
|------|------|------|
| `_startTime` | `Clock::time_point` | 程序启动时间，用于计算真实时间 |
| `_realDeltaTime` | `float` | 真实流逝时间 |
| `_frameStartTime` | `Clock::time_point` | 当前帧开始时间，用于帧率控制 |

**新增方法**：

| 方法 | 说明 |
|------|------|
| `getRealDeltaTime()` | 获取真实时间 |
| `getTimeScale()` | 获取时间缩放因子 |
| `setTimeScale()` | 设置时间缩放因子 |

### 8.3 SubSystemDescriptor 变更

**新增字段**：

```cpp
enum class TimeType : uint8_t {
    Scaled,     // 受 timeScale 影响（游戏逻辑）
    Unscaled,   // 不受 timeScale 影响（UI、编辑器）
    Real        // 真实时间，不受任何缩放（物理等）
};
TimeType timeType = TimeType::Scaled;
```

### 8.4 语法修复

- `std::function<std::unique_ptr<ISubSystem>()>>` → `std::function<std::unique_ptr<ISubSystem>() > >`（空格）
- `Clock` 类型别名在 `beginFrame()` 中使用 `Clock::now()` 计算 `_realDeltaTime`

### 8.5 Threading contract (v0.x cleanup)

IGameLoop 的方法按线程归属分三档：

| 方法 | 线程 |
|---|---|
| `run / tickOnce / stepOnce / preparePlaySession / endPlaySession / prepareHostedSession / endHostedSession / setPresentationOwnership` | main only |
| `stop / pause / resume` | any thread（atomic flag，下一帧顶部观测） |
| `registerSubSystem / unregister / onUpdate / offUpdate` | any thread（mutex） |
| `getDeltaTime / getFrameCount / getCurrentFPS / …` | main only |

`stop()` **不再触碰 `_running`**——`_running` 是「mainLoop 在跑」状态，
由 `run()` 进入和退出 mainLoop 时维护。`stop()` 只 set `_shouldExit`，
并在主线程下一次 emit `GameStoppedEvent`。

### 8.6 Parallel update strategy (v0.x cleanup)

`setParallelEnabled(true)` 启用 **layer-by-layer DAG 调度**：

- `buildExecutionLayers()` 把 `_executionOrder` 按 effective dependencies
  分层（layer 0 = 无依赖；layer N+1 = 依赖仅落在 layer ≤N）
- `runVariableUpdatesParallel(layers, dt)` 在每个 layer 内 f
  所有 subsystem→update 到同一个 TaskGraph，layer 间串行
- 旧实现把所有 subsystem 串成 `depend(node, prev)` 单链 → 实际上还是
  串行；新实现真正并发

**用户责任**：layer 内并发的子系统 `update()` 必须无共享状态依赖；
否则需自行同步（如 `std::mutex`）。

### 8.7 Phase 7 cleanup (v0.x cleanup)

- **3 个孤儿 header 删除**：`include/AYGameLoop/FrameManager.h`、
  `include/AYGameLoop/FrameStats.h`、`include/AYGameLoop/RenderThreadSync.h`——grep 全工程
  0 命中，无外部代码 include。
- **`_ownedSystems` 字段删除**：从未赋值。
- **`shouldRender()` 方法删除**：永远返回 true，从未调用。
- **`ParallelExecutor` 类删除**：全工程 0 调用方。
- **`FrameStats::frameTimeMs` 重命名为 `lastFrameTimeMs`**：字段语义
  是「最近一帧」，不是统计聚合——**breaking change**，外部若有引用
  需同步更新（grep 已确认仅 AYGameLoop 内引用）。
- **`REGISTER_SUBSYSTEM` 宏修复**：
  - `(a)` `deps / priority` 形参现已转发到 `registerSubSystem` 的 3-arg
    overload（之前静默丢弃）
  - `(b)` `__LINE__` 现通过两层 `AY_CONCAT_INDIRECT` 展开（之前
    因 `##` 拼接不展开，identifier 字面是 `RegisterSubsystem_Foo___LINE__`）
  - `(c)` 显式 `#include <type_traits>`
  - 详见 `IAYGameLoop.h` 头注释
- **`SubSystemRegistry::clearAll()` 改为只清 map 不析构**：
  避免 `GameLoop` 静态 dtor 与静态 `REGISTER_SUBSYSTEM` 之间的析构顺序
  race（析构顺序参见 §21 SIOF）。
- **`SubSystemRegistry::topologicalSort()` 循环依赖改为拒绝执行**：
  之前返回 partial order 静默失败；现在返回空 vector + AYT_LOG_ERROR。
- **`SubSystemRegistry::topologicalSort()` O(N²) → O(N log N)**：
  改用 `std::priority_queue`（按 effectivePriority 比较）。
- **`RenderThreadSync::signal{Logic,Render}Complete` 改为持锁 notify**：
  满足 POSIX condvar「notify 与 wait predicate 用同一 mutex」约束。
- **`FrameManager::beginFrame()` 0.25s cap 不重置 `_lastTime`**：
  cap 只影响上报的 delta / smoothed 值，时间轴继续走 wall clock
  （避免下次 beginFrame 出现大跳跃）。
- **`AYFrameInterpolator` 注释更新**：明确 interpolator 是 first-order
  low-pass filter 朝 `target` 逼近，**不是** frame-state lerp(prev, curr, alpha)；
  对应 frame interpolation 需要单独的 (prev, curr) helper。
- **`SmoothDeltaTime::setWindowSize` clamp ≥ 1**：避免 `add()` 中 `% 0`。

### 8.8 设计权衡

- **`setRenderCallback` / `setParallelEnabled` 已加入 IGameLoop 接口**（之前
  只在 GameLoop 派生类上）。原因：保持 IGameLoop 的 thin facade 性质，
  不让用户必须 downcast 到 GameLoop。
- **`_renderFrameCount` 字段已删除**：之前没有任何 getter，render-thread
  内部 counter 没意义——`RenderFrame{Start,End}Event` 现携带
  `FrameManager::getFrameCount()` 的值（统一用 logic frame count）。
- **`stop()` 不再发 `GameStoppedEvent` 当 _running=false 时**：只 set
  flag + emit event；如果当前没在 run，也照发 event（让 host 可观测
  stop request）——这是设计选择，区别于 `pause/resume` 的语义。

---

## 9. 参考

- [Game Loop Pattern](https://gameprogrammingpatterns.com/game-loop.html)
- [Fix Your Timestep](https://gafferongames.com/post/fix_your_timestep/)
- [Glenn Fiedler - Fix Your Timestep](https://gafferongames.com/post/fixed_time_step/)
- [O3DE Tick System](https://docs.o3de.org/docs/user-guide/engine/tick/)
- [Unreal Engine GameViewportClient](https://docs.unrealengine.com/en-US/Programming/Development/Architecture/UnrealArchitecture/)
- [帧插值 (Frame Interpolation)](https://en.wikipedia.org/wiki/Motion_interpolation)
