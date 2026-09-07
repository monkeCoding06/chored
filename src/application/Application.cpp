#include "application/Application.hpp"

#include "logging/Logger.hpp"

#include <csignal>

namespace chored
{
    Application::Application(const Config &config)
    {
    }

    int Application::run(SignalHandler &signalHandler)
    {
        CHORED_LOG_INFO("Starting ", backend_->name(), " backend: ", format.sampleRate,
                        " Hz, ", format.channels, " channel(s), ",
                        format.framesPerBuffer, " frames per buffer");

        engine_->start();
        CHORED_LOG_INFO("chored is running");

        int signal = 0;
        try
        {
            signal = signalHandler.wait();
        } catch (...)
        {
            engine_->stop();
            throw;
        }
        CHORED_LOG_INFO("Received signal ", signal, "; stopping chored");

        engine_->stop();

        if (backend_->failed())
        {
            CHORED_LOG_ERROR("Audio backend failed: ", backend_->failureMessage());
            return 1;
        }

        CHORED_LOG_INFO("chored stopped cleanly");
        return 0;
    }
} // namespace chored
