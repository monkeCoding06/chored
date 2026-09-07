#pragma once
#include "scheduling/Scheduler.hpp"
#include <string>
#include <thread>

namespace chored
{
std::string defaultSocketPath();

std::string listActive(const std::string &socketPath);

class ControlServer
{
public:
  ControlServer(std::string socketPath, const Scheduler &scheduler);

  ~ControlServer();

  ControlServer(const ControlServer &) = delete;

  ControlServer &operator=(const ControlServer &) = delete;

  void start();

  void stop();

private:
  void loop() noexcept;

  void cleanup() noexcept;

  std::string path_;
  const Scheduler &scheduler_;
  int listener_{-1};
  int lock_{-1};
  int wake_[2]{-1, -1};
  bool bound_{false};
  bool started_{false};
  std::thread thread_;
};
} // namespace chored
