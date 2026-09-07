#pragma once

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

struct AppConfig
{
  std::vector<TaskConfig> tasks;
};
} // namespace chored
