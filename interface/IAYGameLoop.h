#pragma once

#include <AYCore.h>
#include <vector>
#include <string>
#include <functional>

namespace ayt::game
{

struct SubSystemDescriptor {
    const char* name = nullptr;
    std::vector<const char*> dependencies;
    int32_t basePriority = 0;

    // 时间类型（影响 update() 传入的时间）
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

class IGameLoop {
public:
    virtual ~IGameLoop() = default;

    static IGameLoop& instance();

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    virtual void run() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;

    // Editor / replay: drive simulation from host message loop instead of run().
    virtual bool preparePlaySession() = 0;
    virtual void endPlaySession() = 0;
    virtual void tickOnce() = 0;
    virtual void stepOnce() = 0;
    virtual bool isPlaySessionActive() const = 0;

    virtual void setTargetFPS(float fps) = 0;
    virtual void setFixedTimestep(float dt) = 0;
    virtual void setTimeScale(float scale) = 0;
    virtual void setNoLimitFPS(bool noLimit) = 0;

    virtual float getDeltaTime() const = 0;
    virtual float getUnscaledDeltaTime() const = 0;
    virtual float getRealDeltaTime() const = 0;
    virtual float getInterpolationFactor() const = 0;
    virtual float getElapsedTime() const = 0;
    virtual uint64_t getFrameCount() const = 0;

    virtual float getCurrentFPS() const = 0;
    virtual float getFrameTimeMS() const = 0;

    virtual void registerSubSystem(ISubSystem* system) = 0;
    virtual void unregisterSubSystem(const char* name) = 0;

    using UpdateCallback = std::function<void(float deltaTime)>;
    virtual uint64_t onUpdate(UpdateCallback callback) = 0;
    virtual void offUpdate(uint64_t listenerId) = 0;
};

} // namespace ayt::game

// Helper to create unique registration struct name without namespace issues
#define REGISTER_SUBSYSTEM_HELPER(cls, line) RegisterSubsystem_##cls##_##line

#define REGISTER_SUBSYSTEM(cls, deps, priority) \
    static_assert(std::is_base_of_v<::ayt::game::ISubSystem, cls>, #cls " must inherit ISubSystem"); \
    static struct REGISTER_SUBSYSTEM_HELPER(cls, __LINE__) { \
        REGISTER_SUBSYSTEM_HELPER(cls, __LINE__)() { \
            ::ayt::game::IGameLoop::instance().registerSubSystem(new cls()); \
        } \
    } g_registerSubsystem_##cls##_##__LINE__