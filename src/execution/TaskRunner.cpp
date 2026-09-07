#include "TaskRunner.hpp"

#include <cstdlib>
#include <stdexcept>
#include <sys/wait.h>

namespace chored {

    TaskResult TaskRunner::run(const TaskConfig& task) const
    {
        const int status = std::system(task.command.c_str());

        if (status == -1) {
            throw std::runtime_error("Failed to start or wait for command");
        }

        if (WIFEXITED(status)) {
            return TaskResult{WEXITSTATUS(status), 0};
        }

        if (WIFSIGNALED(status)) {
            return TaskResult{-1, WTERMSIG(status)};
        }

        throw std::runtime_error("Unexpected command wait status");
    }

} // namespace chored