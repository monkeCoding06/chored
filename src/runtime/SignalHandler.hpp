#pragma once

#include <csignal>

namespace chored
{
    class SignalHandler
    {
      public:
        SignalHandler();

        ~SignalHandler();

        SignalHandler(const SignalHandler&) = delete;

        SignalHandler& operator=(const SignalHandler&) = delete;

        int wait() const;

      private:
        sigset_t signalSet_{};
        sigset_t previousSet_{};
    };
} // namespace chored
