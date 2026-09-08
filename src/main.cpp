#include "config/Config.hpp"
#include "control/ControlServer.hpp"
#include "logging/Logger.hpp"
#include "runtime/SignalHandler.hpp"
#include "scheduling/Scheduler.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace
{
    std::string defaultConfigPath()
    {
        if (const auto* base = std::getenv("XDG_CONFIG_HOME"); base && *base)
            return std::string(base) + "/chored/chored.toml";
        if (const auto* home = std::getenv("HOME"); home && *home)
            return std::string(home) + "/.config/chored/chored.toml";
        return "/etc/chored/chored.toml";
    }

    struct Arguments
    {
        std::string configPath = defaultConfigPath();
        std::string socketPath = chored::defaultSocketPath();
        bool listActive = false;
        bool listTasks = false;
        bool killAll = false;
        bool daemon = false;
        std::optional<std::string> runTask;
    };

    void printHelp()
    {
        std::cout << "Usage: chored [--daemon | --list | --list-active | --kill-all | --run TASK] [options]\n"
                  << "  --daemon            Run scheduler in foreground (default)\n"
                  << "  --list-active       Query the running daemon and exit\n"
                  << "  --list              Query the configured Tasks and exit\n"
                  << "  --kill-all          Cancel running tasks and clear queued tasks\n"
                  << "  --run TASK          Queue a configured task immediately\n"
                  << "  --config PATH       TOML configuration path (daemon only)\n"
                  << "  --socket PATH       Absolute control socket path\n"
                  << "  --version           Print version and exit\n"
                  << "  --help              Show this help\n";
    }

    Arguments parseArguments(int argc, char** argv)
    {
        Arguments arguments;
        for (int index = 1; index < argc; ++index)
        {
            const std::string arg = argv[index];
            if (arg == "--config" || arg == "--socket")
            {
                if (++index >= argc)
                    throw std::invalid_argument("Missing value after " + arg);
                (arg == "--config" ? arguments.configPath : arguments.socketPath) = argv[index];
            }
            else if (arg == "--run")
            {
                if (arguments.runTask)
                    throw std::invalid_argument("--run may only be specified once");
                if (++index >= argc || std::string(argv[index]).empty() || std::string(argv[index]).rfind("--", 0) == 0)
                    throw std::invalid_argument("Missing task name after --run");
                arguments.runTask = argv[index];
            }
            else if (arg.rfind("--run=", 0) == 0)
            {
                if (arguments.runTask || arg.size() == 6)
                    throw std::invalid_argument("--run requires one non-empty task name");
                arguments.runTask = arg.substr(6);
            }
            else if (arg == "--daemon")
                arguments.daemon = true;
            else if (arg == "--kill-all")
                arguments.killAll = true;
            else if (arg == "--list-active")
                arguments.listActive = true;
            else if (arg == "--list")
                arguments.listTasks = true;
            else if (arg == "--help")
            {
                printHelp();
                std::exit(0);
            }
            else if (arg == "--version")
            {
                std::cout << "chored " << CHORED_VERSION << '\n';
                std::exit(0);
            }
            else
                throw std::invalid_argument("Unknown argument: " + arg);
        }
        if (static_cast<int>(arguments.daemon) + arguments.listActive + arguments.listTasks + arguments.killAll +
                arguments.runTask.has_value() >
            1)
            throw std::invalid_argument("--daemon, --list, --list-active, --kill-all and --run are mutually exclusive");
        return arguments;
    }
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const auto args = parseArguments(argc, argv);
        if (args.runTask)
        {
            std::cout << chored::runTask(args.socketPath, *args.runTask);
            return 0;
        }
        if (args.killAll)
        {
            std::cout << chored::killAll(args.socketPath);
            return 0;
        }
        if (args.listActive)
        {
            std::cout << chored::listActive(args.socketPath);
            return 0;
        }
        if (args.listTasks)
        {
            std::cout << chored::listTasks(args.socketPath);
            return 0;
        }

        const auto config = chored::Config::load(args.configPath);
        // Block signals before any threads are created.
        chored::SignalHandler signals;
        chored::Scheduler scheduler(config.values().tasks, config.values().scheduler.maxConcurrentTasks);
        chored::ControlServer control(args.socketPath, scheduler);
        control.start(); // Claim the socket before scheduling any work.
        scheduler.start();
        CHORED_LOG_INFO("Scheduler started; control socket: ", args.socketPath);
        const int signal = signals.wait();
        CHORED_LOG_INFO("Received signal ", signal, ", stopping");
        scheduler.requestStop();
        scheduler.join(); // Keep status queries available while a job finishes.
        control.stop();
        CHORED_LOG_INFO("Scheduler stopped");
        return 0;
    }
    catch (const std::exception& error)
    {
        CHORED_LOG_ERROR(error.what());
        return 1;
    }
}
