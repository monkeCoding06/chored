#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace chored
{
    struct TaskConfig
    {
        std::string name;
        std::string command;
        std::optional<std::string> at;
    };

    struct SchedulerConfig
    {
        std::size_t maxConcurrentTasks{1};
    };

    struct AppConfig
    {
        SchedulerConfig scheduler;
        std::vector<TaskConfig> tasks;
    };
} // namespace chored
