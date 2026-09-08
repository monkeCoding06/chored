#include "TaskRunner.hpp"

#include <cerrno>
#include <csignal>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>

extern char** environ;

namespace chored
{
    namespace
    {
        void checkSpawnError(int error, const char* operation)
        {
            // posix_spawn functions return error numbers directly.
            if (error != 0)
            {
                throw std::system_error(error, std::generic_category(), operation);
            }
        }

        // Ensures attributes are cleaned up even if an exception occurs.
        struct SpawnAttributes
        {
            posix_spawnattr_t value{};

            SpawnAttributes()
            {
                checkSpawnError(posix_spawnattr_init(&value), "Cannot initialize spawn attributes");
            }

            ~SpawnAttributes()
            {
                posix_spawnattr_destroy(&value);
            }

            SpawnAttributes(const SpawnAttributes&) = delete;

            SpawnAttributes& operator=(const SpawnAttributes&) = delete;
        };
    } // namespace

    std::uint64_t TaskRunner::cancellationToken() const
    {
        std::lock_guard<std::mutex> lock(activeMutex_);
        return cancellationEpoch_;
    }

    void TaskRunner::requestCancel()
    {
        {
            std::lock_guard<std::mutex> lock(activeMutex_);
            ++cancellationEpoch_;
        }
        changed_.notify_all();
    }

    TaskResult TaskRunner::run(const TaskConfig& task) const
    {
        return run(task, cancellationToken());
    }

    TaskResult TaskRunner::run(const TaskConfig& task, std::uint64_t token) const
    {
        std::unique_lock<std::mutex> activeLock(activeMutex_);
        if (token != cancellationEpoch_)
            return TaskResult{-1, SIGTERM};
        if (active_)
            throw std::logic_error("TaskRunner already has an active task");
        SpawnAttributes attributes;

        // Do not inherit chored's blocked SIGINT/SIGTERM.
        sigset_t signalMask;
        sigemptyset(&signalMask);

        checkSpawnError(
            posix_spawnattr_setsigmask(&attributes.value, &signalMask), "Cannot configure child signal mask");

        // Restore normal handling of termination and pipe signals.
        sigset_t signalDefaults;
        sigemptyset(&signalDefaults);
        sigaddset(&signalDefaults, SIGINT);
        sigaddset(&signalDefaults, SIGTERM);
        sigaddset(&signalDefaults, SIGQUIT);
        sigaddset(&signalDefaults, SIGPIPE);

        checkSpawnError(posix_spawnattr_setsigdefault(&attributes.value, &signalDefaults),
            "Cannot configure child signal defaults");

        // A group ID of zero creates a group whose ID equals the child's PID.
        checkSpawnError(posix_spawnattr_setpgroup(&attributes.value, 0), "Cannot configure child process group");

        const short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETPGROUP;

        checkSpawnError(posix_spawnattr_setflags(&attributes.value, flags), "Cannot configure spawn flags");

        char shell[] = "sh";
        char option[] = "-c";
        std::string command = task.command;

        char* arguments[] = {shell, option, command.data(), nullptr};

        pid_t pid = -1;

        // Allocate the name before spawn. The lock remains held for spawn/reaping.
        active_.emplace(ActiveTask{task.name, -1, {}});
        struct ResetActive
        {
            std::optional<ActiveTask>& active;
            ~ResetActive()
            {
                active.reset();
            }
        } reset{active_};
        const int spawnError = posix_spawn(&pid, "/bin/sh",
            nullptr, // Inherit standard input/output/error.
            &attributes.value, arguments, environ);
        if (spawnError == 0)
        {
            active_->pid = pid;
            active_->started = std::chrono::steady_clock::now();
        }
        checkSpawnError(spawnError, "Cannot start command");

        int status = 0;
        bool cancelling = false;
        auto deadline = std::chrono::steady_clock::time_point::max();
        for (;;)
        {
            // WNOWAIT keeps the leader PID reserved, even when it exits before its
            // descendants. Do not reap it until group signalling has finished.
            siginfo_t info{};
            if (waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT) != 0)
            {
                if (errno == EINTR)
                    continue;
                throw std::system_error(errno, std::generic_category(), "Cannot inspect command");
            }
            if (!cancelling && token != cancellationEpoch_)
            {
                if (kill(-pid, SIGTERM) != 0 && errno != ESRCH)
                    throw std::system_error(errno, std::generic_category(), "Cannot terminate task group");
                cancelling = true;
                deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            }
            if (cancelling && std::chrono::steady_clock::now() >= deadline)
            {
                if (kill(-pid, SIGKILL) != 0 && errno != ESRCH)
                    throw std::system_error(errno, std::generic_category(), "Cannot kill task group");
                break;
            }
            if (!cancelling && info.si_pid == pid)
                break;
            if (cancelling)
            {
                // Keep the leader reserved for the full grace period so surviving
                // descendants can still be signalled without a recycled group ID.
                changed_.wait_until(activeLock, deadline);
            }
            else
            {
                // Only occupied workers check completion; idle workers do not poll.
                changed_.wait_for(activeLock, std::chrono::milliseconds(100),
                    [&]
                    {
                        return token != cancellationEpoch_;
                    });
            }
        }
        // Same mutex as cancellation/spawn; no further signals after this reap.
        while (waitpid(pid, &status, 0) == -1)
        {
            if (errno == EINTR)
                continue;
            throw std::system_error(errno, std::generic_category(), "Cannot wait for command");
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

    std::optional<ActiveTask> TaskRunner::activeTask() const
    {
        std::lock_guard<std::mutex> lock(activeMutex_);
        if (!active_ || active_->pid <= 0)
            return std::nullopt;
        return active_;
    }
} // namespace chored
