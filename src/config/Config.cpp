#include "config/Config.hpp"

#include <toml++/toml.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace chored
{
    Config::Config(AppConfig values) : values_(std::move(values))
    {
    }

    Config Config::load(const std::string& tomlPath)
    {
        const auto table = toml::parse_file(tomlPath);
        const auto* tasks = table["tasks"].as_table();

        if (!tasks)
        {
            throw std::runtime_error("Configuration must contain a [tasks] table");
        }

        AppConfig values;
        if (table.contains("scheduler"))
        {
            const auto* settings = table["scheduler"].as_table();
            if (!settings)
            {
                throw std::runtime_error("'scheduler' must be a table");
            }
            if (settings->contains("max_concurrent_tasks"))
            {
                const auto* node = settings->get("max_concurrent_tasks");
                const auto* integer = node->as_integer();
                if (!integer || integer->get() < 1 || integer->get() > 64)
                {
                    throw std::runtime_error("scheduler.max_concurrent_tasks must be an integer from 1 to 64");
                }
                values.scheduler.maxConcurrentTasks = static_cast<std::size_t>(integer->get());
            }
        }

        for (const auto& [key, node] : *tasks)
        {
            TaskConfig task;
            task.name = std::string{key.str()};

            const auto* settings = node.as_table();
            if (!settings)
            {
                throw std::runtime_error("Task '" + task.name + "' must be a table");
            }

            const auto command = (*settings)["command"].value<std::string>();

            if (!command || command->find_first_not_of(" \t\r\n") == std::string::npos)
            {
                throw std::runtime_error("Task '" + task.name + "' requires a non-empty command string");
            }

            task.command = *command;

            if (settings->contains("at"))
            {
                const auto at = (*settings)["at"].value<std::string>();

                if (!at)
                {
                    throw std::runtime_error("Task '" + task.name + "': 'at' must be a string");
                }

                const auto isDigit = [](char character)
                {
                    return character >= '0' && character <= '9';
                };

                if (at->size() != 5 || (*at)[2] != ':' || !isDigit((*at)[0]) || !isDigit((*at)[1]) ||
                    !isDigit((*at)[3]) || !isDigit((*at)[4]))
                {
                    throw std::runtime_error("Task '" + task.name + "': 'at' must use HH:MM format");
                }

                const int hours = ((*at)[0] - '0') * 10 + ((*at)[1] - '0');
                const int minutes = ((*at)[3] - '0') * 10 + ((*at)[4] - '0');

                if (hours > 23 || minutes > 59)
                {
                    throw std::runtime_error("Task '" + task.name + "': 'at' must be between 00:00 and 23:59");
                }

                task.at = *at;
            }

            values.tasks.push_back(std::move(task));
        }

        return Config(std::move(values));
    }

    const AppConfig& Config::values() const noexcept
    {
        return values_;
    }
} // namespace chored
