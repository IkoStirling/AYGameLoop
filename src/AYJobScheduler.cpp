// AYJobScheduler.cpp - Job调度器实现

#include <AYJobScheduler.h>
#include <AYTask.h>
#include <cstdio>

namespace ayt::game
{

JobScheduler::JobScheduler() : _initialized(false), _scheduler(nullptr) {
}

JobScheduler::~JobScheduler() {
    shutdown();
}

bool JobScheduler::initialize(int workerCount) {
    if (_initialized) {
        ::fprintf(stderr, "[JobScheduler] Already initialized\n");
        return true;
    }

    _scheduler = &ayt::task::ITaskScheduler::defaultScheduler();
    _initialized = true;

    ::printf("[JobScheduler] Initialized with default scheduler\n");
    return true;
}

void JobScheduler::shutdown() {
    if (!_initialized) return;

    _initialized = false;
    _scheduler = nullptr;
    ::printf("[JobScheduler] Shutdown\n");
}

TaskGraphHandle JobScheduler::createGraph() {
    if (!_initialized) {
        ::fprintf(stderr, "[JobScheduler] Not initialized\n");
        return nullptr;
    }
    return new ayt::task::TaskGraph();
}

void JobScheduler::executeAndWait(TaskGraphHandle graph) {
    if (!_initialized || !graph || !_scheduler) {
        ::fprintf(stderr, "[JobScheduler] Not initialized or invalid graph\n");
        return;
    }
    graph->executeAndWait(*_scheduler);
}

} // namespace ayt::game