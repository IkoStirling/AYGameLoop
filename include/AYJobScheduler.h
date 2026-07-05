#pragma once
// AYJobScheduler.h - Job调度器，封装AYTask的TaskScheduler

#include <AYCore.h>
#include <functional>
#include <memory>
#include <AYTaskGraph.h>

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

    // 初始化调度器
    bool initialize(int workerCount = 4);

    // 关闭调度器
    void shutdown();

    // 创建JobGraph
    TaskGraphHandle createGraph();

    // 执行Graph并等待完成
    void executeAndWait(TaskGraphHandle graph);

    // 获取底层调度器
    ayt::task::ITaskScheduler* getScheduler() { return _scheduler; }

    bool isInitialized() const { return _initialized; }

private:
    bool _initialized = false;
    ayt::task::ITaskScheduler* _scheduler = nullptr;
};

// 并行任务执行器 - 用于子系统间并行调度
class ParallelExecutor {
public:
    ParallelExecutor();
    ~ParallelExecutor();

    // 初始化
    bool initialize();

    // 添加并行任务组
    // tasks: 可以并行执行的任务
    // name: 组名称（用于调试）
    void addParallelGroup(std::vector<std::function<void()>> && tasks, const char* name);

    // 执行所有任务并等待完成
    void executeAndWait();

private:
    std::unique_ptr<JobScheduler> _scheduler;
    std::vector<std::unique_ptr<ayt::task::TaskGraph> > _graphs;
};

} // namespace ayt::game