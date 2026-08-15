#pragma once
// AYJobScheduler.h - Job调度器，封装AYTask的TaskScheduler

#include <AYCore.h>
#include <functional>
#include <memory>
#include <AYTask/TaskGraph.h>

namespace ayt::task
{
class ITaskScheduler;
}

namespace ayt::game
{

// TaskGraph句柄类型
using TaskGraphHandle = ayt::task::TaskGraph*;

// Job调度器 - 使用TaskGraph实现并行调度
class JobScheduler {
public:
    JobScheduler();
    ~JobScheduler();

    // 初始化调度器。workerCount hint currently accepted but the underlying
    // default scheduler uses its own thread pool (see AYTask); the hint is
    // logged for visibility.
    bool initialize(int workerCount = 4);

    void shutdown();

    // Caller owns the returned TaskGraph; delete via `delete graph`.
    TaskGraphHandle createGraph();

    void executeAndWait(TaskGraphHandle graph);

    ayt::task::ITaskScheduler* getScheduler() { return _scheduler; }

    [[nodiscard]] bool isInitialized() const { return _initialized; }

private:
    bool _initialized = false;
    ayt::task::ITaskScheduler* _scheduler = nullptr;
    int _workerHint = 0;
};

} // namespace ayt::game