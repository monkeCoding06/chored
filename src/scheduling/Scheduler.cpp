#include "Scheduler.hpp"
#include "logging/Logger.hpp"

#include <algorithm>
#include <ctime>
#include <exception>
#include <stdexcept>
#include <utility>

namespace chored
{
    Scheduler::Scheduler(std::vector<TaskConfig> tasks, std::size_t maxConcurrentTasks)
    {
        if (maxConcurrentTasks < 1 || maxConcurrentTasks > 64)
        {
            throw std::invalid_argument("maxConcurrentTasks must be between 1 and 64");
        }
        runners_.reserve(maxConcurrentTasks);
        workerThreads_.reserve(maxConcurrentTasks);
        for (std::size_t i = 0; i < maxConcurrentTasks; ++i)
        {
            runners_.push_back(std::make_unique<TaskRunner>());
        }
        const auto now = Clock::now();
        std::set<std::string> names;

        for (auto& task : tasks)
        {
            if (!names.insert(task.name).second)
            {
                throw std::invalid_argument("Duplicate task name: " + task.name);
            }

            if (!task.at)
            {
                continue;
            }

            const auto nextRun = calculateNextRun(task, now);
            tasks_.push_back({std::move(task), nextRun});
        }
    }

    std::vector<ActiveTask> Scheduler::activeTasks() const
    {
        std::vector<ActiveTask> result;
        result.reserve(runners_.size());
        for (const auto& runner : runners_)
        {
            if (const auto active = runner->activeTask())
            {
                result.push_back(*active);
            }
        }
        std::sort(result.begin(), result.end(),
            [](const ActiveTask& a, const ActiveTask& b)
            {
                return a.name < b.name;
            });
        return result;
    }

    std::vector<ConfiguredTask> Scheduler::configuredTasks() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<ConfiguredTask> result;
        result.reserve(tasks_.size());

        for (const auto& scheduled : tasks_)
        {
            result.push_back({
                scheduled.task.name,
                scheduled.nextRun
            });
        }

        return result;
    }

    Scheduler::~Scheduler()
    {
        requestStop();
        join();
    }

    void Scheduler::start()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (started_ || stopRequested_)
        {
            throw std::logic_error("Scheduler can only be started once, before shutdown");
        }

        started_ = true;

        try
        {
            for (const auto& runner : runners_)
            {
                workerThreads_.emplace_back(
                    [this, worker = runner.get()]
                    {
                        workerLoop(*worker);
                    });
            }
            schedulerThread_ = std::thread(&Scheduler::schedulerLoop, this);
        }
        catch (...)
        {
            stopRequested_ = true;
            lock.unlock();

            scheduleChanged_.notify_all();
            workAvailable_.notify_all();

            join();
            throw;
        }
    }

    void Scheduler::killAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!readyTasks_.empty())
        {
            pendingTasks_.erase(readyTasks_.front().name);
            readyTasks_.pop();
        }
        // The same queue lock covers dequeue + token capture, closing the
        // gap between a worker taking a job and spawning its process.
        for (const auto& runner : runners_)
            runner->requestCancel();
    }

    void Scheduler::requestStop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopRequested_ = true;
        }

        scheduleChanged_.notify_all();
        workAvailable_.notify_all();
    }

    void Scheduler::join()
    {
        if (schedulerThread_.joinable())
        {
            schedulerThread_.join();
        }

        for (auto& worker : workerThreads_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    Scheduler::Clock::time_point Scheduler::calculateNextRun(const TaskConfig& task, Clock::time_point now) const
    {
        // Config::load has already validated HH:MM.
        const auto& at = task.at.value();
        const int hours = std::stoi(at.substr(0, 2));
        const int minutes = std::stoi(at.substr(3, 2));

        const auto timestamp = Clock::to_time_t(now);
        std::tm local{};

#if defined(_WIN32)
        if (localtime_s(&local, &timestamp) != 0)
        {
            throw std::runtime_error("Cannot determine local time");
        }
#else
        if (localtime_r(&timestamp, &local) == nullptr)
        {
            throw std::runtime_error("Cannot determine local time");
        }
#endif

        local.tm_hour = hours;
        local.tm_min = minutes;
        local.tm_sec = 0;
        local.tm_isdst = -1;

        const auto makeTime = [](std::tm date)
        {
            const auto result = std::mktime(&date);

            if (result == static_cast<std::time_t>(-1))
            {
                throw std::runtime_error("Cannot calculate task execution time");
            }

            return Clock::from_time_t(result);
        };

        auto nextRun = makeTime(local);

        if (nextRun <= now)
        {
            // Advance the calendar date, rather than adding exactly 24 hours.
            ++local.tm_mday;
            local.tm_isdst = -1;
            nextRun = makeTime(local);
        }

        return nextRun;
    }

    void Scheduler::schedulerLoop()
    {
        try
        {
            std::unique_lock<std::mutex> lock(mutex_);

            while (!stopRequested_)
            {
                if (tasks_.empty())
                {
                    scheduleChanged_.wait(lock,
                        [this]
                        {
                            return stopRequested_;
                        });
                    continue;
                }

                const auto next = std::min_element(tasks_.begin(), tasks_.end(),
                    [](const ScheduledTask& left, const ScheduledTask& right)
                    {
                        return left.nextRun < right.nextRun;
                    });

                const auto deadline = next->nextRun;

                scheduleChanged_.wait_until(lock, deadline,
                    [this]
                    {
                        return stopRequested_;
                    });

                if (stopRequested_)
                {
                    break;
                }

                const auto now = Clock::now();

                for (auto& scheduled : tasks_)
                {
                    if (scheduled.nextRun > now)
                    {
                        continue;
                    }

                    // Skip this occurrence if the task is already queued/running.
                    if (pendingTasks_.insert(scheduled.task.name).second)
                    {
                        readyTasks_.push(scheduled.task);
                        workAvailable_.notify_one();
                    }

                    scheduled.nextRun = calculateNextRun(scheduled.task, now);
                }
            }
        }
        catch (const std::exception& error)
        {
            requestStop();
            // Keep logging failures from escaping a thread entry point.
            try
            {
                CHORED_LOG_ERROR("Scheduler failed: ", error.what());
            }
            catch (...)
            {
            }
        }
        catch (...)
        {
            requestStop();
        }
    }

    void Scheduler::workerLoop(TaskRunner& runner)
    {
        try
        {
            while (true)
            {
                TaskConfig task;
                std::uint64_t token = 0;

                {
                    std::unique_lock<std::mutex> lock(mutex_);

                    workAvailable_.wait(lock,
                        [this]
                        {
                            return stopRequested_ || !readyTasks_.empty();
                        });

                    if (stopRequested_)
                    {
                        // Discard queued work; an already running command finishes.
                        return;
                    }

                    task = std::move(readyTasks_.front());
                    readyTasks_.pop();
                    token = runner.cancellationToken();
                }

                // No scheduler mutex is held while the command runs.
                try
                {
                    CHORED_LOG_INFO("Starting task: ", task.name);

                    const auto result = runner.run(task, token);

                    if (result.terminationSignal != 0)
                    {
                        CHORED_LOG_ERROR("Task ", task.name, " terminated by signal ", result.terminationSignal);
                    }
                    else if (result.exitCode != 0)
                    {
                        CHORED_LOG_ERROR("Task ", task.name, " exited with code ", result.exitCode);
                    }
                    else
                    {
                        CHORED_LOG_INFO("Task finished: ", task.name);
                    }
                }
                catch (const std::exception& error)
                {
                    CHORED_LOG_ERROR("Task ", task.name, " failed: ", error.what());
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    pendingTasks_.erase(task.name);
                }
            }
        }
        catch (const std::exception& error)
        {
            requestStop();
            try
            {
                CHORED_LOG_ERROR("Task worker failed: ", error.what());
            }
            catch (...)
            {
            }
        }
        catch (...)
        {
            requestStop();
        }
    }
} // namespace chored
