#pragma once

#include "config/ConfigTypes.hpp"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <sys/types.h>

namespace chored
{
    struct TaskResult
    {
        int exitCode{-1}; // Valid when terminationSignal is 0.
        int terminationSignal{0};
    };

    struct ActiveTask
    {
        std::string name;
        pid_t pid; // Shell PID (also its process-group ID).
        std::chrono::steady_clock::time_point started;
    };

    class TaskRunner
    {
      public:
        // Runs through /bin/sh -c and waits for completion.
        // Throws if starting or waiting for the process fails.
        TaskResult run(const TaskConfig& task) const;

        // Capture while holding the scheduler queue lock; pass it to run().
        std::uint64_t cancellationToken() const;
        TaskResult run(const TaskConfig& task, std::uint64_t token) const;
        void requestCancel();
        std::optional<ActiveTask> activeTask() const;

      private:
        mutable std::mutex activeMutex_;
        mutable std::optional<ActiveTask> active_;
        mutable std::condition_variable changed_;
        std::uint64_t cancellationEpoch_{0};
    };
} // namespace chored
