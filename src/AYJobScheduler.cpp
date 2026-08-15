// AYJobScheduler.cpp - Job调度器实现

#include <AYGameLoop/JobScheduler.h>
#include <AYTask.h>
#include <AYLog.h>
#include <cstdio>

namespace ayt::game
{

JobScheduler::JobScheduler() : _initialized(false), _scheduler(nullptr), _workerHint(0) {
}

JobScheduler::~JobScheduler() {
    shutdown();
}

bool JobScheduler::initialize(int workerCount) {
    if (_initialized) {
        ayt::log::warn("[JobScheduler] Already initialized");
        return true;
    }

    _workerHint = workerCount;
    _scheduler = &ayt::task::ITaskScheduler::defaultScheduler();
    _initialized = true;

    ayt::log::info("[JobScheduler] Initialized with default scheduler (hint=%d, actual=%zu workers)",
                 _workerHint, _scheduler->numThreads());
    return true;
}

void JobScheduler::shutdown() {
    if (!_initialized) return;

    _initialized = false;
    _scheduler = nullptr;
    ayt::log::info("[JobScheduler] Shutdown");
}

TaskGraphHandle JobScheduler::createGraph() {
    if (!_initialized) {
        ayt::log::error("[JobScheduler] Not initialized");
        return nullptr;
    }
    return new ayt::task::TaskGraph();
}

void JobScheduler::executeAndWait(TaskGraphHandle graph) {
    if (!_initialized || !graph || !_scheduler) {
        ayt::log::error("[JobScheduler] Not initialized or invalid graph");
        return;
    }
    graph->executeAndWait(*_scheduler);
}

} // namespace ayt::game