#include "ControlServer.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iomanip>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>

namespace chored
{
    namespace
    {
        struct Fd
        {
            int value;
            ~Fd()
            {
                if (value >= 0)
                    close(value);
            }
        };

        [[noreturn]] void fail(const char* operation)
        {
            throw std::system_error(errno, std::generic_category(), operation);
        }

        sockaddr_un address(const std::string& path)
        {
            sockaddr_un result{};
            result.sun_family = AF_UNIX;
            if (path.empty() || path.front() != '/' || path.size() >= sizeof(result.sun_path))
                throw std::runtime_error("Control socket requires an absolute path shorter than 108 bytes");
            std::memcpy(result.sun_path, path.c_str(), path.size() + 1);
            return result;
        }

        std::string printable(const std::string& input)
        {
            std::string output;
            for (unsigned char c : input)
            {
                if (output.size() >= 160)
                {
                    output += "...";
                    break;
                }
                output += (c < 32 || c == 127) ? '?' : static_cast<char>(c);
            }
            return output;
        }

        std::string snapshot(const Scheduler& scheduler)
        {
            const auto active = scheduler.activeTasks();
            if (active.empty())
            {
                return "No active tasks.\n";
            }
            std::ostringstream out;
            out << "TASK\tPID\tRUNNING FOR\n";
            for (const auto& task : active)
            {
                const auto seconds =
                    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - task.started)
                        .count();
                out << std::quoted(printable(task.name)) << '\t' << task.pid << '\t' << seconds / 60 << "m "
                    << seconds % 60 << "s\n";
            }
            return out.str();
        }
    } // namespace

    std::string defaultSocketPath()
    {
        const char* runtime = std::getenv("XDG_RUNTIME_DIR");
        const std::string base = runtime && *runtime ? runtime : "/run/user/" + std::to_string(getuid());
        return base + "/chored/control.sock";
    }

    ControlServer::ControlServer(std::string socketPath, const Scheduler& scheduler)
        : path_(std::move(socketPath)), scheduler_(scheduler)
    {
    }

    ControlServer::~ControlServer()
    {
        stop();
    }

    void ControlServer::start()
    {
        if (started_)
            throw std::logic_error("ControlServer can only start once");
        started_ = true;
        try
        {
            const auto addr = address(path_);
            const auto directory = std::filesystem::path(path_).parent_path();
            if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST)
                fail("Cannot create control directory (set XDG_RUNTIME_DIR or --socket)");
            struct stat info{};
            if (lstat(directory.c_str(), &info) != 0)
                fail("Cannot inspect control directory");
            if (!S_ISDIR(info.st_mode) || info.st_uid != getuid() || (info.st_mode & 0077))
                throw std::runtime_error("Control directory must be owned by you with mode 0700");
            lock_ = open((path_ + ".lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (lock_ < 0)
                fail("Cannot open daemon lock");
            if (flock(lock_, LOCK_EX | LOCK_NB) != 0)
                throw std::runtime_error("Another chored daemon already owns this socket");
            // The persistent lock file prevents deleting a live daemon's socket.
            if (lstat(path_.c_str(), &info) == 0)
            {
                if (!S_ISSOCK(info.st_mode) || info.st_uid != getuid())
                    throw std::runtime_error("Refusing to replace a non-socket or foreign control path");
                if (unlink(path_.c_str()) != 0)
                    fail("Cannot remove stale control socket");
            }
            else if (errno != ENOENT)
                fail("Cannot inspect control socket");
            listener_ = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
            if (listener_ < 0)
                fail("Cannot create control socket");
            if (bind(listener_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0)
                fail("Cannot bind control socket");
            bound_ = true;
            if (chmod(path_.c_str(), 0600) != 0)
                fail("Cannot set socket permissions");
            if (listen(listener_, 16) != 0)
                fail("Cannot listen on control socket");
            if (pipe2(wake_, O_CLOEXEC | O_NONBLOCK) != 0)
                fail("Cannot create shutdown pipe");
            thread_ = std::thread(&ControlServer::loop, this);
        }
        catch (...)
        {
            cleanup();
            throw;
        }
    }

    void ControlServer::cleanup() noexcept
    {
        if (bound_)
        {
            unlink(path_.c_str());
            bound_ = false;
        }
        for (int* fd : {&listener_, &wake_[0], &wake_[1], &lock_})
        {
            if (*fd >= 0)
            {
                close(*fd);
                *fd = -1;
            }
        }
    }

    void ControlServer::stop()
    {
        if (thread_.joinable())
        {
            const char byte = 1;
            // Retry EINTR so shutdown cannot lose its wakeup.
            while (write(wake_[1], &byte, 1) < 0 && errno == EINTR)
            {
            }
            thread_.join();
        }
        cleanup();
    }

    void ControlServer::loop() noexcept
    {
        // Every wait includes the shutdown pipe. An idle client cannot delay stop.
        for (;;)
        {
            pollfd listeners[]{{wake_[0], POLLIN, 0}, {listener_, POLLIN, 0}};
            const int ready = poll(listeners, 2, -1);
            if (ready < 0 && errno == EINTR)
                continue;
            if (ready < 0 || listeners[0].revents)
                return;
            if (!(listeners[1].revents & POLLIN))
                return;
            Fd client{accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK)};
            if (client.value < 0)
                continue;
            ucred peer{};
            socklen_t size = sizeof(peer);
            if (getsockopt(client.value, SOL_SOCKET, SO_PEERCRED, &peer, &size) != 0 || peer.uid != getuid())
                continue;
            pollfd request[]{{wake_[0], POLLIN, 0}, {client.value, POLLIN, 0}};
            int result;
            do
            {
                result = poll(request, 2, 1000);
            } while (result < 0 && errno == EINTR);
            if (request[0].revents)
                return;
            if (result <= 0 || !(request[1].revents & POLLIN))
                continue;
            char buffer[64];
            const auto count = recv(client.value, buffer, sizeof(buffer), MSG_TRUNC);
            try
            {
                const bool valid = count == 12 && std::memcmp(buffer, "LIST_ACTIVE\n", 12) == 0;
                const auto response = valid ? "OK\n" + snapshot(scheduler_) : std::string("ERROR\nUnknown request\n");
                send(client.value, response.data(), response.size(), MSG_NOSIGNAL);
            }
            catch (...)
            {
                constexpr char error[] = "ERROR\nCannot read active tasks\n";
                send(client.value, error, sizeof(error) - 1, MSG_NOSIGNAL);
            }
        }
    }

    std::string listActive(const std::string& path)
    {
        const auto addr = address(path);
        Fd socketFd{socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)};
        if (socketFd.value < 0)
            fail("Cannot create client socket");
        if (connect(socketFd.value, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0)
            fail("Cannot connect to chored; start the daemon and check --socket");
        ucred peer{};
        socklen_t size = sizeof(peer);
        if (getsockopt(socketFd.value, SOL_SOCKET, SO_PEERCRED, &peer, &size) != 0)
            fail("Cannot verify daemon identity");
        if (peer.uid != getuid())
            throw std::runtime_error("Control socket belongs to another user");
        constexpr char request[] = "LIST_ACTIVE\n";
        if (send(socketFd.value, request, sizeof(request) - 1, MSG_NOSIGNAL) != sizeof(request) - 1)
            fail("Cannot send control request");
        pollfd reply{socketFd.value, POLLIN, 0};
        int result;
        do
        {
            result = poll(&reply, 1, 3000);
        } while (result < 0 && errno == EINTR);
        if (result < 0)
            fail("Cannot wait for daemon response");
        if (result == 0)
            throw std::runtime_error("Timed out waiting for chored");
        // Up to 64 rows with escaped, length-limited names.
        char buffer[65536];
        const auto count = recv(socketFd.value, buffer, sizeof(buffer), MSG_TRUNC);
        if (count <= 0 || count > static_cast<ssize_t>(sizeof(buffer)))
            throw std::runtime_error("Missing or oversized daemon response");
        std::string response(buffer, static_cast<std::size_t>(count));
        if (response.rfind("OK\n", 0) != 0)
            throw std::runtime_error("Invalid or unsuccessful daemon response");
        return response.substr(3);
    }
} // namespace chored
