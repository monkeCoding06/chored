#pragma once

#include "config/ConfigTypes.hpp"
#include "execution/TaskRunner.hpp"

#include <chrono>
#include <vector>

namespace chored
{
    class Scheduler
    {
    public:
        explicit Scheduler(std::vector<TaskConfig> tasks);

        // Waits for due tasks and executes them; blocks while running.
        void run();

    private:
        using Clock = std::chrono::system_clock;

        struct ScheduledTask
        {
            TaskConfig task;
            Clock::time_point nextRun;
        };

        Clock::time_point calculateNextRun(
            const TaskConfig &task,
            Clock::time_point now) const;

        std::vector<ScheduledTask> tasks_;
        TaskRunner runner_;
    };
} // namespace chored
