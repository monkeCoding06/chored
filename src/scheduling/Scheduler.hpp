#pragma once

#include "config/ConfigTypes.hpp"
#include "execution/TaskRunner.hpp"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <thread>
#include <vector>

namespace chored
{
    class Scheduler
    {
      public:
        explicit Scheduler(std::vector<TaskConfig> tasks, std::size_t maxConcurrentTasks = 1);

        ~Scheduler();

        Scheduler(const Scheduler&) = delete;

        Scheduler& operator=(const Scheduler&) = delete;

        std::vector<ActiveTask> activeTasks() const;
        std::vector<ConfiguredTask> configuredTasks() const;

        void killAll(); // Cancel current work and clear queued occurrences.

        void start();       // Starts scheduler and workers; returns immediately.
        void requestStop(); // Requests shutdown and wakes all threads.
        void join();        // Waits for all threads to finish.

      private:
        using Clock = std::chrono::system_clock;

        struct ScheduledTask
        {
            TaskConfig task;
            Clock::time_point nextRun;
        };

        void schedulerLoop();

        void workerLoop(TaskRunner& runner);

        Clock::time_point calculateNextRun(const TaskConfig& task, Clock::time_point now) const;

        std::vector<ScheduledTask> tasks_;
        std::queue<TaskConfig> readyTasks_;
        // Constructed before start; one independent runner per worker.
        std::vector<std::unique_ptr<TaskRunner>> runners_;

        std::thread schedulerThread_;
        std::vector<std::thread> workerThreads_;

        mutable std::mutex mutex_;
        std::condition_variable scheduleChanged_;
        std::condition_variable workAvailable_;

        bool started_{false};                // Protected by mutex_.
        std::set<std::string> pendingTasks_; // Queued or running task names.

        bool stopRequested_{false}; // Protected by mutex_.
    };
} // namespace chored
