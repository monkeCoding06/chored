#include "logging/Logger.hpp"
#include "runtime/SignalHandler.hpp"
#include "application/Application.h"

#include <exception>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "execution/TaskRunner.hpp"

namespace
{
    struct Arguments
    {
        std::string configPath{"/etc/chored/chored.xml"};
    };

    void printHelp()
    {
        std::cout
                << "Usage: chored [options]\n"
                << "  --config PATH       XML configuration path\n"
                << "  --version           Print version and exit\n"
                << "  --help              Show this help\n";
    }

    Arguments parseArguments(int argc, char **argv)
    {
        Arguments arguments;
        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index];
            if (argument == "--config")
            {
                if (index + 1 >= argc)
                {
                    throw std::invalid_argument("Missing value after " + argument);
                }
                const std::string value = argv[++index];
                if (argument == "--config")
                {
                    arguments.configPath = value;
                }
            } else if (argument == "--version")
            {
                std::cout << "chored " << CHORED_VERSION << '\n';
                std::exit(0);
            } else if (argument == "--help")
            {
                printHelp();
                std::exit(0);
            } else
            {
                throw std::invalid_argument("Unknown argument: " + argument);
            }
        }
        return arguments;
    }
} // namespace

int main(int argc, char **argv)
{
    try
    {
        const Arguments arguments = parseArguments(argc, argv);
        const chored::Config config =
                chored::Config::load(arguments.configPath);

        chored::TaskRunner runner;
        bool anyFailed = false;

        for (const auto &task: config.values().tasks)
        {
            std::cout << "Task: " << task.name << '\n'
                    << "  Command: " << task.command << '\n'
                    << "  At: " << task.at.value_or("manual") << '\n'
                    << std::flush;

            const auto result = runner.run(task);

            if (result.terminationSignal != 0)
            {
                CHORED_LOG_ERROR(
                    "Task ", task.name,
                    " terminated by signal ", result.terminationSignal);
                anyFailed = true;
            } else
            {
                std::cout << "  Exit code: " << result.exitCode << '\n';
                anyFailed |= result.exitCode != 0;
            }
        }

        return anyFailed ? 1 : 0;
    } catch (const std::exception &e)
    {
        CHORED_LOG_ERROR(e.what());
        return 1;
    }
}
