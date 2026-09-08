#include "config/Config.hpp"
#include "control/ControlServer.hpp"
#include "logging/Logger.hpp"
#include "runtime/SignalHandler.hpp"
#include "scheduling/Scheduler.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>
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
        bool daemon = false;
    };

    void printHelp()
    {
        std::cout << "Usage: chored [--daemon | --list-active] [options]\n"
                  << "  --daemon            Run scheduler in foreground (default)\n"
                  << "  --list-active       Query the running daemon and exit\n"
                  << "  --kill-all          Kill all Chored Tasks\n"
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
            else if (arg == "--daemon")
                arguments.daemon = true;
            else if (arg == "--list-active")
                arguments.listActive = true;
            else if (arg =="--kill-all")
                arguments.listActive = false;
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
        if (arguments.daemon && arguments.listActive)
            throw std::invalid_argument("--daemon and --list-active are mutually exclusive");
        return arguments;
    }
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const auto args = parseArguments(argc, argv);
        if (args.listActive)
        {
            std::cout << chored::listActive(args.socketPath);
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
