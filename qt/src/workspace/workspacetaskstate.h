#pragma once

#include "runtime/ragprocessbridge.h"

struct WorkspaceTaskState
{
    WorkerState workerState = WorkerState::Stopped;
    EngineState engineState = EngineState::Idle;
    bool taskRunning = false;
};
