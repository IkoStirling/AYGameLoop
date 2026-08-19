#pragma once

#include <AYCore.h>
#include <AYGameLoop/FramePolicy.h>
#include <cstdint>
#include <type_traits>
#include <vector>
#include <string>
#include <functional>

namespace ayt::game
{

enum class FramePhase : uint8_t {
    Platform,
    Ingress,
    FixedPrePhysics,
    FixedPhysics,
    FixedPostPhysics,
    Gameplay,
    World,
    Presentation,
    Egress,
    Count
};

using PhaseMask = uint32_t;

[[nodiscard]] constexpr PhaseMask phaseBit(FramePhase phase) {
    return PhaseMask{1u} << static_cast<uint8_t>(phase);
}

[[nodiscard]] constexpr bool hasPhase(PhaseMask mask, FramePhase phase) {
    return (mask & phaseBit(phase)) != 0;
}

[[nodiscard]] constexpr bool isFixedPhase(FramePhase phase) {
    return phase == FramePhase::FixedPrePhysics
        || phase == FramePhase::FixedPhysics
        || phase == FramePhase::FixedPostPhysics;
}

enum class ClockDomain : uint8_t {
    Legacy,
    Game,
    Unscaled,
    RealWall
};

// Decides who consumes the immutable render packet produced by the
// Presentation phase. Standalone applications normally leave this at
// GameLoop; editor hosts select ExternalHost and compose the packet with
// their own chrome before presenting.
enum class PresentationOwnership : uint8_t {
    GameLoop,
    ExternalHost
};

struct FrameContext {
    FramePhase phase = FramePhase::Platform;
    uint64_t frameCount = 0;
    uint64_t simTick = 0;
    float deltaTime = 0.0f;
    float fixedDeltaTime = 0.0f;
    float gameDeltaTime = 0.0f;
    float unscaledDeltaTime = 0.0f;
    float realWallDeltaTime = 0.0f;
    float interpolationAlpha = 0.0f;
    bool paused = false;
};

struct PhaseTimingStats {
    uint64_t invocationCount = 0;
    uint64_t totalTimeUs = 0;
    uint64_t lastTimeUs = 0;
    uint64_t maxTimeUs = 0;
};

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
        Real        // 真实时间，不受任何缩放（网络超时、诊断等）
    };
    TimeType timeType = TimeType::Scaled;

    // Explicit stage model. phases == 0 activates the legacy adapter
    // (FixedPrePhysics + Gameplay, using fixedUpdate/update respectively).
    PhaseMask phases = 0;
    ClockDomain clock = ClockDomain::Legacy;

    // Strict lifecycle dependencies. Unlike the legacy `dependencies` field,
    // missing names are initialization errors.
    std::vector<const char*> initializeAfter;

    // Per-frame ordering edges, applied only among systems in the same phase.
    std::vector<const char*> runsAfter;
    int32_t phasePriority = 0;

    // Declarative phase-local hazards. Systems in the same dependency layer
    // are parallel only when their read/write sets do not conflict.
    std::vector<const char*> reads;
    std::vector<const char*> writes;
};

class ISubSystem {
public:
    virtual ~ISubSystem() = default;
    virtual const char* getName() const = 0;
    virtual const SubSystemDescriptor& getDescriptor() const = 0;
    virtual bool initialize() = 0;
    virtual void update(float deltaTime) = 0;
    virtual void fixedUpdate(float fixedDeltaTime) = 0;
    virtual void tick(FramePhase phase, const FrameContext& context) {
        if (isFixedPhase(phase)) {
            fixedUpdate(context.fixedDeltaTime);
        } else {
            update(context.deltaTime);
        }
    }
    // Status-aware phase entry. Existing subsystems inherit the adapter;
    // barrier-owning systems can fail a phase without committing its sim tick.
    virtual bool tickChecked(FramePhase phase, const FrameContext& context) {
        tick(phase, context);
        return true;
    }
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
    virtual void stop() = 0;             // any-thread safe; event delivered by next main-thread pump
    virtual void pause() = 0;            // any-thread safe
    virtual void resume() = 0;           // any-thread safe

    // Editor / replay: drive simulation from host message loop instead of run().
    // These historical names remain the low-level compatibility API.
    [[nodiscard]] virtual bool preparePlaySession() = 0;
    virtual void endPlaySession() = 0;
    virtual void tickOnce() = 0;
    virtual void stepOnce() = 0;
    [[nodiscard]] virtual bool isPlaySessionActive() const = 0;

    // Host-facing lifecycle names. A hosted session initializes the engine
    // runtime, but does not itself mean that an editor Play scene is active.
    // The default adapters preserve compatibility for existing loop impls.
    [[nodiscard]] virtual bool prepareHostedSession() {
        return preparePlaySession();
    }
    virtual void endHostedSession() {
        endPlaySession();
    }

    virtual void setTargetFPS(float fps) = 0;
    virtual void setFixedTimestep(float dt) = 0;
    virtual void setTimeScale(float scale) = 0;
    virtual void setNoLimitFPS(bool noLimit) = 0;
    virtual void setMaxFixedUpdatesPerFrame(uint32_t count) = 0;
    virtual void setFixedBacklogPolicy(FixedBacklogPolicy policy) = 0;

    [[nodiscard]] virtual float getDeltaTime() const = 0;
    [[nodiscard]] virtual float getUnscaledDeltaTime() const = 0;
    [[nodiscard]] virtual float getRealDeltaTime() const = 0;
    [[nodiscard]] virtual float getInterpolationFactor() const = 0;
    [[nodiscard]] virtual float getElapsedTime() const = 0;
    [[nodiscard]] virtual double getGameElapsedTime() const = 0;
    [[nodiscard]] virtual float getEffectiveTimeScale() const = 0;
    [[nodiscard]] virtual FixedBacklogPolicy getFixedBacklogPolicy() const = 0;
    [[nodiscard]] virtual uint64_t getFrameCount() const = 0;

    /// Monotonic Sim tick count (increments once per fixedUpdate step).
    /// Shared time anchor for Scene / Save / Network / Replay subscribers of
    /// `SimTickEvent` (see docs/sim-present-time.md §5).
    [[nodiscard]] virtual uint64_t getSimTick() const = 0;
    [[nodiscard]] virtual uint64_t getDroppedSimTickCount() const = 0;
    [[nodiscard]] virtual double getDroppedSimulationSeconds() const = 0;
    [[nodiscard]] virtual double getClampedSimulationSeconds() const = 0;
    [[nodiscard]] virtual PhaseTimingStats getPhaseTiming(FramePhase phase) const = 0;
    [[nodiscard]] virtual uint32_t getFixedTicksLastFrame() const = 0;

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
    // Runtime mutations are committed transactionally at the next main-frame
    // boundary. Names and registration-time dependency strings are copied.
    virtual void registerSubSystem(ISubSystem* system) = 0;

    // Overload with explicit descriptor override (used by REGISTER_SUBSYSTEM
    // macro to forward deps / priority from the call site).
    virtual void registerSubSystem(ISubSystem* system,
                                   std::vector<const char*> dependencies,
                                   int32_t priority) = 0;

    virtual void unregisterSubSystem(const char* name) = 0;

    // Per-frame update callback (called once per variable update, on main thread).
    // Listener callbacks run on the main thread.
    using UpdateCallback = std::function<void(float deltaTime)>;
    [[nodiscard]] virtual uint64_t onUpdate(UpdateCallback callback) = 0;
    virtual void offUpdate(uint64_t listenerId) = 0;

    // Render-thread controls
    virtual void setRenderThreadEnabled(bool enabled) = 0;
    virtual void setPresentationOwnership(PresentationOwnership ownership) = 0;
    [[nodiscard]] virtual PresentationOwnership getPresentationOwnership() const = 0;
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
