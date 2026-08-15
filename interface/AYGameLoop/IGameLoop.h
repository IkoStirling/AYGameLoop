#pragma once

#include <AYCore.h>
#include <cstdint>
#include <type_traits>
#include <vector>
#include <string>
#include <functional>

namespace ayt::game
{

// -----------------------------------------------------------------------------
// Sub-system descriptor (passed at registration time)
// -----------------------------------------------------------------------------
struct SubSystemDescriptor {
    const char* name = nullptr;
    std::vector<const char*> dependencies;
    int32_t basePriority = 0;

    // Time type (drives which delta-time GameLoop feeds update())
    enum class TimeType : uint8_t {
        Scaled,     // 受 timeScale 影响（游戏逻辑）
        Unscaled,   // 不受 timeScale 影响（UI、编辑器）
        Real        // 真实时间，不受任何缩放（物理等）
    };
    TimeType timeType = TimeType::Scaled;
};

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

// -----------------------------------------------------------------------------
// Game loop interface
//
// Threading contract:
//   - run() / tickOnce() / stepOnce() / preparePlaySession() / endPlaySession()
//     and the per-frame accessors (getDeltaTime, getFrameCount, etc.) are
//     **main-thread only** unless explicitly documented otherwise.
//   - stop() / pause() / resume() set an atomic flag observable at the next
//     frame top; safe to call from any thread, main thread preferred.
//   - registerSubSystem() / onUpdate() / offUpdate() serialize through an
//     internal mutex and are safe from any thread.
// -----------------------------------------------------------------------------
class IGameLoop {
public:
    virtual ~IGameLoop() = default;

    [[nodiscard]] static IGameLoop& instance();

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    virtual void run() = 0;
    virtual void stop() = 0;             // any-thread safe; observable at next frame top
    virtual void pause() = 0;            // any-thread safe
    virtual void resume() = 0;           // any-thread safe

    // Editor / replay: drive simulation from host message loop instead of run().
    [[nodiscard]] virtual bool preparePlaySession() = 0;
    virtual void endPlaySession() = 0;
    virtual void tickOnce() = 0;
    virtual void stepOnce() = 0;
    [[nodiscard]] virtual bool isPlaySessionActive() const = 0;

    virtual void setTargetFPS(float fps) = 0;
    virtual void setFixedTimestep(float dt) = 0;
    virtual void setTimeScale(float scale) = 0;
    virtual void setNoLimitFPS(bool noLimit) = 0;

    [[nodiscard]] virtual float getDeltaTime() const = 0;
    [[nodiscard]] virtual float getUnscaledDeltaTime() const = 0;
    [[nodiscard]] virtual float getRealDeltaTime() const = 0;
    [[nodiscard]] virtual float getInterpolationFactor() const = 0;
    [[nodiscard]] virtual float getElapsedTime() const = 0;
    [[nodiscard]] virtual uint64_t getFrameCount() const = 0;

    /// Monotonic Sim tick count (increments once per fixedUpdate step).
    /// Shared time anchor for Scene / Save / Network / Replay subscribers of
    /// `SimTickEvent` (see docs/sim-present-time.md §5).
    [[nodiscard]] virtual uint64_t getSimTick() const = 0;

    [[nodiscard]] virtual float getCurrentFPS() const = 0;
    [[nodiscard]] virtual float getFrameTimeMS() const = 0;

    // Sub-system registration
    //
    // The registry takes **ownership** of `system` and destroys it on
    // shutdown (or process exit). Subsystems with dependencies declared
    // here are inserted in the topological order respecting `dependencies`
    // and `priority`; those fields override what the subsystem's own
    // getDescriptor() would have returned.
    //
    // `name` must be stable for the lifetime of the registration; the
    // registry keys on it.
    virtual void registerSubSystem(ISubSystem* system) = 0;

    // Overload with explicit descriptor override (used by REGISTER_SUBSYSTEM
    // macro to forward deps / priority from the call site).
    virtual void registerSubSystem(ISubSystem* system,
                                   std::vector<const char*> dependencies,
                                   int32_t priority) = 0;

    virtual void unregisterSubSystem(const char* name) = 0;

    // Per-frame update callback (called once per variable update, on main thread).
    // Listeners run under an internal mutex; DO NOT unsubscribe yourself
    // from inside the callback (recursive lock would deadlock).
    using UpdateCallback = std::function<void(float deltaTime)>;
    [[nodiscard]] virtual uint64_t onUpdate(UpdateCallback callback) = 0;
    virtual void offUpdate(uint64_t listenerId) = 0;

    // Render-thread controls
    virtual void setRenderThreadEnabled(bool enabled) = 0;
    virtual void setRenderCallback(
        std::function<void()> callback) = 0;

    // Parallel subsystem updates via TaskGraph (defaults OFF).
    virtual void setParallelEnabled(bool enabled) = 0;
    [[nodiscard]] virtual bool isParallelEnabled() const = 0;
};

} // namespace ayt::game

// -----------------------------------------------------------------------------
// REGISTER_SUBSYSTEM macro — auto-registration at static-init.
//
// Usage:
//   REGISTER_SUBSYSTEM(MySubSystem, { "OtherSystem" }, 100);
//
// Notes:
//   1. `deps` and `priority` ARE forwarded to IGameLoop::registerSubSystem —
//      not discarded as in earlier revisions.
//   2. Identifier uniquification uses AY_CONCAT_INDIRECT so __LINE__
//      expands BEFORE the ## paste (otherwise __LINE__ is literal).
//   3. MSVC's static-lib linker drops unreferenced .obj files, so the TU
//      containing this macro MUST be referenced from the final binary,
//      OR the host must also call SubSystem::register<>() explicitly.
//      See AYEntity/design.md §957 for the pattern.
//
//   Place this macro in a .cpp (not .h) to avoid duplicate registration
//   across translation units.
// -----------------------------------------------------------------------------
#define AY_CONCAT_INDIRECT(a, b) a##b
#define AY_CONCAT(a, b) AY_CONCAT_INDIRECT(a, b)
#define AY_REGISTER_SUBSYSTEM_TAG(cls) AY_CONCAT(AY_CONCAT(RegisterSubsystem_, cls), __LINE__)
#define AY_REGISTER_SUBSYSTEM_INSTANCE(cls) AY_CONCAT(AY_CONCAT(g_registerSubsystem_, cls), __LINE__)

#define REGISTER_SUBSYSTEM(cls, deps, priority) \
    static_assert(std::is_base_of_v<::ayt::game::ISubSystem, cls>, #cls " must inherit ISubSystem"); \
    static struct AY_REGISTER_SUBSYSTEM_TAG(cls) { \
        AY_REGISTER_SUBSYSTEM_TAG(cls)() { \
            ::ayt::game::IGameLoop::instance().registerSubSystem(new cls(), deps, priority); \
        } \
    } AY_REGISTER_SUBSYSTEM_INSTANCE(cls)