#pragma once

#include "config/ConfigTypes.hpp"

#include <string>

namespace chored
{
    class Config
    {
      public:
        static Config load(const std::string& tomlPath);

        const AppConfig& values() const noexcept;

      private:
        explicit Config(AppConfig values);

        AppConfig values_;
    };
} // namespace chored
