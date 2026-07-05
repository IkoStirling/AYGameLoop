// AYSubSystemRegistry.cpp - 子系统注册表实现

#include <AYSubSystemRegistry.h>
#include <cstdio>

namespace ayt::game
{

SubSystemRegistry& SubSystemRegistry::instance() {
    static SubSystemRegistry registry;
    return registry;
}

void SubSystemRegistry::registerSubSystem(ISubSystem* system) {
    if (!system) return;

    const char* name = system->getName();
    if (!name) {
        ::fprintf(stderr, "SubSystem registration failed: name is null\n");
        return;
    }

    std::string key(name);
    if (_systems.find(key) != _systems.end()) {
        ::fprintf(stderr, "SubSystem '%s' already registered, skipping\n", name);
        return;
    }

    _systems[key] = std::unique_ptr<ISubSystem>(system);
    _dirty = true;
    ::printf("[SubSystemRegistry] Registered: %s\n", name);
}

void SubSystemRegistry::unregisterSubSystem(const char* name) {
    if (!name) return;

    std::string key(name);
    auto it = _systems.find(key);
    if (it != _systems.end()) {
        ::printf("[SubSystemRegistry] Unregistered: %s\n", name);
        _systems.erase(it);
        _dirty = true;
    }
}

void SubSystemRegistry::clearAll() {
    _systems.clear();
    _executionOrder.clear();
    _dirty = true;
}

ISubSystem* SubSystemRegistry::findSubSystem(const char* name) const {
    if (!name) return nullptr;
    std::string key(name);
    auto it = _systems.find(key);
    if (it != _systems.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<ISubSystem*> SubSystemRegistry::getExecutionOrder() const {
    if (_dirty) {
        _executionOrder = topologicalSort();
        _dirty = false;
    }
    return _executionOrder;
}

std::vector<ISubSystem*> SubSystemRegistry::topologicalSort() const {
    std::vector<ISubSystem*> result;
    std::unordered_map<std::string, int> inDegree;
    std::unordered_map<std::string, std::vector<std::string>> adj;

    // 初始化
    for (const auto& pair : _systems) {
        inDegree[pair.first] = 0;
    }

    // 构建依赖图
    for (const auto& pair : _systems) {
        const std::string& name = pair.first;
        const auto& desc = pair.second->getDescriptor();

        for (const char* depName : desc.dependencies) {
            if (!depName) {
                continue;
            }
            std::string dep(depName);
            if (_systems.find(dep) != _systems.end()) {
                adj[dep].push_back(name);
                inDegree[name]++;
            }
        }
    }

    // Kahn's algorithm with priority tie-breaker
    std::vector<ISubSystem*> zeroDegree;
    for (const auto& pair : _systems) {
        if (inDegree[pair.first] == 0) {
            zeroDegree.push_back(pair.second.get());
        }
    }

    // 按优先级排序（高优先级先执行）
    std::sort(zeroDegree.begin(), zeroDegree.end(),
        [](ISubSystem* a, ISubSystem* b) {
            return a->getDescriptor().basePriority > b->getDescriptor().basePriority;
        });

    while (!zeroDegree.empty()) {
        // 取最高优先级
        ISubSystem* current = zeroDegree.back();
        zeroDegree.pop_back();
        result.push_back(current);

        std::string currentName(current->getName());
        for (const std::string& neighbor : adj[currentName]) {
            inDegree[neighbor]--;
            if (inDegree[neighbor] == 0) {
                // 找到下一个入度为0的节点
                ISubSystem* next = _systems.at(neighbor).get();
                // 按优先级插入合适位置
                auto it = zeroDegree.begin();
                while (it != zeroDegree.end() &&
                       (*it)->getDescriptor().basePriority >= next->getDescriptor().basePriority) {
                    ++it;
                }
                zeroDegree.insert(it, next);
            }
        }
    }

    // 检查循环依赖
    if (result.size() != _systems.size()) {
        ::fprintf(stderr, "[SubSystemRegistry] Warning: circular dependency detected, some systems may not be initialized\n");
    }

    return result;
}

} // namespace ayt::game