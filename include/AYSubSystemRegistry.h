#pragma once
// AYSubSystemRegistry.h - 子系统注册表，支持依赖排序

#include <AYCore.h>
#include <IAYGameLoop.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>

namespace ayt::game
{

class SubSystemRegistry {
public:
    static SubSystemRegistry& instance();

    void registerSubSystem(ISubSystem* system);
    void unregisterSubSystem(const char* name);
    void clearAll();

    std::vector<ISubSystem*> getExecutionOrder() const;
    ISubSystem* findSubSystem(const char* name) const;

    size_t getCount() const { return _systems.size(); }

private:
    SubSystemRegistry() = default;
    ~SubSystemRegistry() = default;
    SubSystemRegistry(const SubSystemRegistry&) = delete;
    SubSystemRegistry& operator=(const SubSystemRegistry&) = delete;

    // Kahn's algorithm for topological sort
    std::vector<ISubSystem*> topologicalSort() const;

    std::unordered_map<std::string, std::unique_ptr<ISubSystem>> _systems;
    mutable std::vector<ISubSystem*> _executionOrder;
    mutable bool _dirty = true;  // 需要重新排序
};

} // namespace ayt::game