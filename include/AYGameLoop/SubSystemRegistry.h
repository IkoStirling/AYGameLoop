#pragma once
// AYGameLoop/SubSystemRegistry.h - 子系统注册表，支持依赖排序

#include <AYCore.h>
#include <AYGameLoop/IGameLoop.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cstdint>
#include <optional>

namespace ayt::game
{

class SubSystemRegistry {
public:
    static SubSystemRegistry& instance();

    // Single-arg registration: descriptor comes from subsystem's getDescriptor().
    bool registerSubSystem(ISubSystem* system);

    // 3-arg registration: dependencies and priority OVERRIDE what
    // getDescriptor() would have returned. Used by REGISTER_SUBSYSTEM macro
    // and any host that wants explicit control.
    bool registerSubSystem(ISubSystem* system,
                           std::vector<const char*> dependencies,
                           int32_t priority);

    // Removes and destroys the system. Runtime code must first call shutdown;
    // GameLoop does this transactionally at a frame boundary.
    void unregisterSubSystem(const char* name);

    // Removes ownership without destroying. Used by GameLoop to unpublish a
    // system before shutdown while no phase is executing.
    [[nodiscard]] std::unique_ptr<ISubSystem> takeSubSystem(const char* name);

    // Destroys every registered subsystem. Callers must have shut down any
    // initialized systems first.
    void clearAll();

    // Topologically sorted execution order. Safe from main thread only.
    std::vector<ISubSystem*> getExecutionOrder() const;

    [[nodiscard]] ISubSystem* findSubSystem(const char* name) const;

    [[nodiscard]] size_t getCount() const;

    // Lookup the dependency override (set by 3-arg registerSubSystem).
    // Returns nullopt if no override; merge with subsystem's own descriptor.
    [[nodiscard]] std::optional<std::vector<std::string>>
    getDependencyOverride(const char* name) const;

private:
    SubSystemRegistry() = default;
    ~SubSystemRegistry() = default;
    SubSystemRegistry(const SubSystemRegistry&) = delete;
    SubSystemRegistry& operator=(const SubSystemRegistry&) = delete;

    struct Entry {
        std::unique_ptr<ISubSystem> system;
        std::optional<std::vector<std::string>> depOverride;
        int32_t priorityOverride = 0;
        bool hasPriorityOverride = false;
        uint64_t registrationOrder = 0;
    };

    // Kahn's algorithm for topological sort.
    // On circular dependency: returns an EMPTY vector + logs an error.
    std::vector<ISubSystem*> topologicalSort() const;

    mutable ayt::Mutex _mutex;
    std::unordered_map<std::string, Entry> _systems;
    mutable std::vector<ISubSystem*> _executionOrder;
    mutable bool _dirty = true;
    uint64_t _nextRegistrationOrder = 0;
};

} // namespace ayt::game
