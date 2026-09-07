#pragma once

#include "config/ConfigTypes.hpp"
#include "execution/TaskRunner.hpp"

#include <chrono>
#include <condition_variable>
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
  explicit Scheduler(std::vector<TaskConfig> tasks);

  ~Scheduler();

  Scheduler(const Scheduler &) = delete;

  Scheduler &operator=(const Scheduler &) = delete;

  std::optional<ActiveTask> activeTask() const
  {
    return runner_.activeTask();
  }

  void start();       // Starts scheduler and worker; returns immediately.
  void requestStop(); // Requests shutdown and wakes both threads.
  void join();        // Waits for both threads to finish.

private:
  using Clock = std::chrono::system_clock;

  struct ScheduledTask
  {
    TaskConfig task;
    Clock::time_point nextRun;
  };

  void schedulerLoop();

  void workerLoop();

  Clock::time_point calculateNextRun(const TaskConfig &task, Clock::time_point now) const;

  std::vector<ScheduledTask> tasks_;
  std::queue<TaskConfig> readyTasks_;
  TaskRunner runner_;

  std::thread schedulerThread_;
  std::thread workerThread_;

  std::mutex mutex_;
  std::condition_variable scheduleChanged_;
  std::condition_variable workAvailable_;

  bool started_{false};                // Protected by mutex_.
  std::set<std::string> pendingTasks_; // Queued or running task names.

  bool stopRequested_{false}; // Protected by mutex_.
};
} // namespace chored
