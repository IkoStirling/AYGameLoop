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

bool SubSystemRegistry::registerSubSystem(ISubSystem* system) {
    std::unique_ptr<ISubSystem> incoming(system);
    AY_LOCK_GUARD(_mutex);

    if (!system) return false;

    const char* name = system->getName();
    if (!name) {
        ayt::log::error("[SubSystemRegistry] registration failed: name is null");
        return false;
    }

    std::string key(name);
    if (_systems.find(key) != _systems.end()) {
        ayt::log::warn("[SubSystemRegistry] '%s' already registered, skipping", name);
        // Registering the exact same pointer twice must not destroy the active
        // object. A distinct duplicate remains owned by `incoming` and is
        // destroyed here, preventing the old leak.
        if (_systems[key].system.get() == system) incoming.release();
        return false;
    }

    _systems[key] = Entry{std::move(incoming),
                          std::nullopt,
                          0,
                          false};
    _dirty = true;
    ayt::log::info("[SubSystemRegistry] Registered: %s", name);
    return true;
}

bool SubSystemRegistry::registerSubSystem(ISubSystem* system,
                                          std::vector<const char*> dependencies,
                                          int32_t priority) {
    std::unique_ptr<ISubSystem> incoming(system);
    AY_LOCK_GUARD(_mutex);

    if (!system) return false;

    const char* name = system->getName();
    if (!name) {
        ayt::log::error("[SubSystemRegistry] registration failed: name is null");
        return false;
    }

    std::string key(name);
    auto it = _systems.find(key);
    if (it != _systems.end()) {
        ayt::log::warn("[SubSystemRegistry] '%s' already registered, skipping", name);
        if (it->second.system.get() == system) incoming.release();
        return false;
    }

    Entry e;
    e.system = std::move(incoming);
    std::vector<std::string> ownedDependencies;
    ownedDependencies.reserve(dependencies.size());
    for (const char* dependency : dependencies) {
        if (dependency != nullptr) ownedDependencies.emplace_back(dependency);
    }
    e.depOverride = std::move(ownedDependencies);
    e.priorityOverride = priority;
    e.hasPriorityOverride = true;

    _systems[key] = std::move(e);
    _dirty = true;
    ayt::log::info("[SubSystemRegistry] Registered: %s (deps=%zu, priority=%d)",
                 name,
                 _systems[key].depOverride ? _systems[key].depOverride->size() : 0,
                 priority);
    return true;
}

void SubSystemRegistry::unregisterSubSystem(const char* name) {
    (void)takeSubSystem(name);
}

std::unique_ptr<ISubSystem> SubSystemRegistry::takeSubSystem(const char* name) {
    AY_LOCK_GUARD(_mutex);
    if (!name) return {};

    auto it = _systems.find(name);
    if (it == _systems.end()) return {};

    std::unique_ptr<ISubSystem> result = std::move(it->second.system);
    _systems.erase(it);
    _executionOrder.clear();
    _dirty = true;
    ayt::log::info("[SubSystemRegistry] Unregistered: %s", name);
    return result;
}

void SubSystemRegistry::clearAll() {
    AY_LOCK_GUARD(_mutex);
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

std::optional<std::vector<std::string>>
SubSystemRegistry::getDependencyOverride(const char* name) const {
    AY_LOCK_GUARD(_mutex);

    if (!name) return std::nullopt;
    auto it = _systems.find(name);
    if (it == _systems.end()) return std::nullopt;
    return it->second.depOverride;
}

size_t SubSystemRegistry::getCount() const {
    AY_LOCK_GUARD(_mutex);
    return _systems.size();
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
    auto effectiveDeps = [&](const Entry& e) -> std::vector<std::string> {
        if (e.depOverride) return *e.depOverride;
        const auto& desc = e.system->getDescriptor();
        const auto& source = !desc.initializeAfter.empty()
            ? desc.initializeAfter
            : desc.dependencies;
        std::vector<std::string> result;
        result.reserve(source.size());
        for (const char* dependency : source) {
            if (dependency != nullptr) result.emplace_back(dependency);
        }
        // Legacy dependencies remain lenient during migration.
        return result;
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

    bool invalidStrictDependency = false;
    for (const auto& pair : _systems) {
        const std::string& name = pair.first;
        for (const std::string& dep : effectiveDeps(pair.second)) {
            if (_systems.find(dep) != _systems.end()) {
                adj[dep].push_back(name);
                inDegree[name]++;
            } else if (!pair.second.system->getDescriptor().initializeAfter.empty()) {
                ayt::log::error("[SubSystemRegistry] '%s' requires missing lifecycle dependency '%s'",
                                name.c_str(), dep.c_str());
                invalidStrictDependency = true;
            }
        }
    }

    if (invalidStrictDependency) return {};

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
