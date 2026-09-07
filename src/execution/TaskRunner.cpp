#include "TaskRunner.hpp"

#include <cerrno>
#include <csignal>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/types.h>
#include <sys/wait.h>

extern char ** environ;

namespace chored
{
    namespace
    {
        void checkSpawnError(int error, const char *operation)
        {
            // posix_spawn functions return error numbers directly.
            if (error != 0)
            {
                throw std::system_error(
                    error, std::generic_category(), operation);
            }
        }

        // Ensures attributes are cleaned up even if an exception occurs.
        struct SpawnAttributes
        {
            posix_spawnattr_t value{};

            SpawnAttributes()
            {
                checkSpawnError(
                    posix_spawnattr_init(&value),
                    "Cannot initialize spawn attributes");
            }

            ~SpawnAttributes()
            {
                posix_spawnattr_destroy(&value);
            }

            SpawnAttributes(const SpawnAttributes &) = delete;

            SpawnAttributes &operator=(const SpawnAttributes &) = delete;
        };
    } // namespace

    TaskResult TaskRunner::run(const TaskConfig &task) const
    {
        SpawnAttributes attributes;

        // Do not inherit chored's blocked SIGINT/SIGTERM.
        sigset_t signalMask;
        sigemptyset(&signalMask);

        checkSpawnError(
            posix_spawnattr_setsigmask(&attributes.value, &signalMask),
            "Cannot configure child signal mask");

        // Restore normal handling of termination and pipe signals.
        sigset_t signalDefaults;
        sigemptyset(&signalDefaults);
        sigaddset(&signalDefaults, SIGINT);
        sigaddset(&signalDefaults, SIGTERM);
        sigaddset(&signalDefaults, SIGQUIT);
        sigaddset(&signalDefaults, SIGPIPE);

        checkSpawnError(
            posix_spawnattr_setsigdefault(
                &attributes.value, &signalDefaults),
            "Cannot configure child signal defaults");

        // A group ID of zero creates a group whose ID equals the child's PID.
        checkSpawnError(
            posix_spawnattr_setpgroup(&attributes.value, 0),
            "Cannot configure child process group");

        const short flags =
                POSIX_SPAWN_SETSIGMASK |
                POSIX_SPAWN_SETSIGDEF |
                POSIX_SPAWN_SETPGROUP;

        checkSpawnError(
            posix_spawnattr_setflags(&attributes.value, flags),
            "Cannot configure spawn flags");

        char shell[] = "sh";
        char option[] = "-c";
        std::string command = task.command;

        char *arguments[] = {
            shell,
            option,
            command.data(),
            nullptr
        };

        pid_t pid = -1;

        checkSpawnError(
            posix_spawn(
                &pid,
                "/bin/sh",
                nullptr, // Inherit standard input/output/error.
                &attributes.value,
                arguments,
                environ),
            "Cannot start command");

        int status = 0;

        // Wait without polling; retry if interrupted by a signal.
        while (waitpid(pid, &status, 0) == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            throw std::system_error(
                errno, std::generic_category(),
                "Cannot wait for command");
        }

        if (WIFEXITED(status))
        {
            return TaskResult{WEXITSTATUS(status), 0};
        }

        if (WIFSIGNALED(status))
        {
            return TaskResult{-1, WTERMSIG(status)};
        }

        throw std::runtime_error("Unexpected command wait status");
    }

} // namespace chored