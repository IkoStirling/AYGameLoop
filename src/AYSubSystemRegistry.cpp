// AYSubSystemRegistry.cpp - 子系统注册表实现

#include <AYGameLoop/SubSystemRegistry.h>
#include <AYLog.h>
#include <queue>
#include <cstdio>

namespace ayt::game
{

SubSystemRegistry& SubSystemRegistry::instance() {
    static SubSystemRegistry registry;
    return registry;
}

void SubSystemRegistry::registerSubSystem(ISubSystem* system) {
    AY_LOCK_GUARD(_mutex);

    if (!system) return;

    const char* name = system->getName();
    if (!name) {
        ayt::log::error("[SubSystemRegistry] registration failed: name is null");
        return;
    }

    std::string key(name);
    if (_systems.find(key) != _systems.end()) {
        ayt::log::warn("[SubSystemRegistry] '%s' already registered, skipping", name);
        return;
    }

    _systems[key] = Entry{std::unique_ptr<ISubSystem>(system),
                          std::nullopt,
                          0,
                          false};
    _dirty = true;
    ayt::log::info("[SubSystemRegistry] Registered: %s", name);
}

void SubSystemRegistry::registerSubSystem(ISubSystem* system,
                                          std::vector<const char*> dependencies,
                                          int32_t priority) {
    AY_LOCK_GUARD(_mutex);

    if (!system) return;

    const char* name = system->getName();
    if (!name) {
        ayt::log::error("[SubSystemRegistry] registration failed: name is null");
        return;
    }

    std::string key(name);
    auto it = _systems.find(key);
    if (it != _systems.end()) {
        ayt::log::warn("[SubSystemRegistry] '%s' already registered, skipping", name);
        return;
    }

    Entry e;
    e.system = std::unique_ptr<ISubSystem>(system);
    e.depOverride = std::move(dependencies);
    e.priorityOverride = priority;
    e.hasPriorityOverride = true;

    _systems[key] = std::move(e);
    _dirty = true;
    ayt::log::info("[SubSystemRegistry] Registered: %s (deps=%zu, priority=%d)",
                 name,
                 _systems[key].depOverride ? _systems[key].depOverride->size() : 0,
                 priority);
}

void SubSystemRegistry::unregisterSubSystem(const char* name) {
    AY_LOCK_GUARD(_mutex);

    if (!name) return;

    std::string key(name);
    auto it = _systems.find(key);
    if (it != _systems.end()) {
        ayt::log::info("[SubSystemRegistry] Unregistered: %s", name);
        _systems.erase(it);
        _dirty = true;
    }
}

void SubSystemRegistry::clearAll() {
    AY_LOCK_GUARD(_mutex);
    // NOTE: subsystems are NOT destroyed. Their lifetime extends to process
    // exit. This avoids use-after-free when static-init registrars
    // (REGISTER_SUBSYSTEM) tear down before GameLoop's static dtor runs.
    _systems.clear();
    _executionOrder.clear();
    _dirty = true;
}

ISubSystem* SubSystemRegistry::findSubSystem(const char* name) const {
    AY_LOCK_GUARD(_mutex);

    if (!name) return nullptr;
    std::string key(name);
    auto it = _systems.find(key);
    if (it != _systems.end()) {
        return it->second.system.get();
    }
    return nullptr;
}

std::optional<std::vector<const char*>>
SubSystemRegistry::getDependencyOverride(const char* name) const {
    AY_LOCK_GUARD(_mutex);

    if (!name) return std::nullopt;
    auto it = _systems.find(name);
    if (it == _systems.end()) return std::nullopt;
    return it->second.depOverride;
}

std::vector<ISubSystem*> SubSystemRegistry::getExecutionOrder() const {
    AY_LOCK_GUARD(_mutex);

    if (_dirty) {
        _executionOrder = topologicalSort();
        _dirty = false;
    }
    return _executionOrder;
}

std::vector<ISubSystem*> SubSystemRegistry::topologicalSort() const {
    std::vector<ISubSystem*> result;
    if (_systems.empty()) return result;

    // Effective descriptor merging: override wins over getDescriptor().
    auto effectiveDeps = [&](const Entry& e) -> std::vector<const char*> {
        if (e.depOverride) return *e.depOverride;
        // Fall back to subsystem's own getDescriptor().
        const auto& desc = e.system->getDescriptor();
        return desc.dependencies;
    };
    auto effectivePriority = [](const Entry& e) -> int32_t {
        if (e.hasPriorityOverride) return e.priorityOverride;
        return e.system->getDescriptor().basePriority;
    };

    std::unordered_map<std::string, int> inDegree;
    std::unordered_map<std::string, std::vector<std::string>> adj;

    for (const auto& pair : _systems) {
        inDegree[pair.first] = 0;
    }

    for (const auto& pair : _systems) {
        const std::string& name = pair.first;
        for (const char* depName : effectiveDeps(pair.second)) {
            if (!depName) continue;
            std::string dep(depName);
            if (_systems.find(dep) != _systems.end()) {
                adj[dep].push_back(name);
                inDegree[name]++;
            }
        }
    }

    // Min-heap by priority descending; stable by insertion order on ties.
    struct PrioItem {
        int32_t priority;
        ISubSystem* system;
    };
    auto cmp = [](const PrioItem& a, const PrioItem& b) {
        // std::priority_queue is max-heap by default; we want higher priority first.
        return a.priority < b.priority;
    };
    std::priority_queue<PrioItem, std::vector<PrioItem>, decltype(cmp)> zeroDegree(cmp);

    for (const auto& pair : _systems) {
        if (inDegree[pair.first] == 0) {
            zeroDegree.push({effectivePriority(pair.second), pair.second.system.get()});
        }
    }

    while (!zeroDegree.empty()) {
        PrioItem current = zeroDegree.top();
        zeroDegree.pop();
        result.push_back(current.system);

        std::string currentName(current.system->getName());
        for (const std::string& neighbor : adj[currentName]) {
            inDegree[neighbor]--;
            if (inDegree[neighbor] == 0) {
                auto it = _systems.find(neighbor);
                if (it != _systems.end()) {
                    zeroDegree.push({effectivePriority(it->second), it->second.system.get()});
                }
            }
        }
    }

    if (result.size() != _systems.size()) {
        ayt::log::error("[SubSystemRegistry] Circular dependency detected (%zu of %zu subsystems ordered). "
                      "Refusing to execute until the cycle is broken.",
                      result.size(), _systems.size());
        return {};  // refuse partial order
    }

    return result;
}

} // namespace ayt::game