#pragma once
// AYGameLoop/SubSystemRegistry.h - 子系统注册表，支持依赖排序

#include <AYCore.h>
#include <AYGameLoop/IGameLoop.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <optional>

namespace ayt::game
{

class SubSystemRegistry {
public:
    static SubSystemRegistry& instance();

    // Single-arg registration: descriptor comes from subsystem's getDescriptor().
    void registerSubSystem(ISubSystem* system);

    // 3-arg registration: dependencies and priority OVERRIDE what
    // getDescriptor() would have returned. Used by REGISTER_SUBSYSTEM macro
    // and any host that wants explicit control.
    void registerSubSystem(ISubSystem* system,
                           std::vector<const char*> dependencies,
                           int32_t priority);

    // Removes the system from the registry. Does NOT destroy the system
    // (subsystem lifetime extends to process exit — see GameLoop::shutdown).
    void unregisterSubSystem(const char* name);

    // Forgets every registration (map only). Subsystems are NOT destroyed.
    void clearAll();

    // Topologically sorted execution order. Safe from main thread only.
    std::vector<ISubSystem*> getExecutionOrder() const;

    [[nodiscard]] ISubSystem* findSubSystem(const char* name) const;

    [[nodiscard]] size_t getCount() const { return _systems.size(); }

    // Lookup the dependency override (set by 3-arg registerSubSystem).
    // Returns nullopt if no override; merge with subsystem's own descriptor.
    [[nodiscard]] std::optional<std::vector<const char*>>
    getDependencyOverride(const char* name) const;

private:
    SubSystemRegistry() = default;
    ~SubSystemRegistry() = default;
    SubSystemRegistry(const SubSystemRegistry&) = delete;
    SubSystemRegistry& operator=(const SubSystemRegistry&) = delete;

    struct Entry {
        std::unique_ptr<ISubSystem> system;
        std::optional<std::vector<const char*>> depOverride;
        int32_t priorityOverride = 0;
        bool hasPriorityOverride = false;
    };

    // Kahn's algorithm for topological sort.
    // On circular dependency: returns an EMPTY vector + logs an error.
    std::vector<ISubSystem*> topologicalSort() const;

    mutable ayt::Mutex _mutex;
    std::unordered_map<std::string, Entry> _systems;
    mutable std::vector<ISubSystem*> _executionOrder;
    mutable bool _dirty = true;
};

} // namespace ayt::game