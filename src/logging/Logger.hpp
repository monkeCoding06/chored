#pragma once

#include <atomic>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

namespace chored
{
    enum class LogLevel
    {
        Debug = 0,
        Info,
        Warning,
        Error,
        Critical
    };

    class Logger
    {
      public:
        static Logger& instance();

        static LogLevel parseLevel(const std::string& value);

        void configure(LogLevel minimumLevel, const std::string& filePath = {});

        template <typename... Args> void log(LogLevel level, const char* file, int line, Args&&... args)
        {
            if (level < minimumLevel_.load())
            {
                return;
            }

            std::ostringstream message;
            (message << ... << std::forward<Args>(args));
            write(level, file, line, message.str());
        }

      private:
        Logger() = default;

        void write(LogLevel level, const char* file, int line, const std::string& message);

        static const char* levelName(LogLevel level) noexcept;

        std::mutex mutex_;
        std::atomic<LogLevel> minimumLevel_{LogLevel::Info};
        std::ofstream file_;
    };
} // namespace chored

#define CHORED_LOG_DEBUG(...)                                                                                          \
    ::chored::Logger::instance().log(::chored::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define CHORED_LOG_INFO(...) ::chored::Logger::instance().log(::chored::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define CHORED_LOG_WARNING(...)                                                                                        \
    ::chored::Logger::instance().log(::chored::LogLevel::Warning, __FILE__, __LINE__, __VA_ARGS__)
#define CHORED_LOG_ERROR(...)                                                                                          \
    ::chored::Logger::instance().log(::chored::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
#define CHORED_LOG_CRITICAL(...)                                                                                       \
    ::chored::Logger::instance().log(::chored::LogLevel::Critical, __FILE__, __LINE__, __VA_ARGS__)
