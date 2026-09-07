#pragma once

#include "config/ConfigTypes.hpp"

namespace chored {

    struct TaskResult {
        int exitCode{-1};       // Valid when terminationSignal is 0.
        int terminationSignal{0};
    };

    class TaskRunner {
    public:
        // Runs through /bin/sh -c and waits for completion.
        // Throws if starting or waiting for the process fails.
        TaskResult run(const TaskConfig& task) const;
    };

} // namespace chored