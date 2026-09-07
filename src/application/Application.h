#pragma once

#include "config/Config.hpp"
#include "runtime/SignalHandler.hpp"

#include <memory>

namespace chored {

    class Application {
    public:
        explicit Application(const Config& config);

        int run(SignalHandler& signalHandler);

    private:
    };

} // namespace chored
