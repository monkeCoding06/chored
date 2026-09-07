cmake_minimum_required(VERSION 3.20)

project(chored VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Download the TOML parser when configuring a fresh build directory.
include(FetchContent)
FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

# Logger uses std::mutex even though the prototype has no scheduler threads.
find_package(Threads REQUIRED)

# Parsing prototype only. Add Application and SignalHandler when implementing
# the long-running service. Config.cpp must be migrated from XML to TOML.
add_executable(chored
        src/main.cpp
        src/config/Config.cpp
        src/logging/Logger.cpp
        src/runtime/SignalHandler.cpp
        src/execution/TaskRunner.cpp
        src/scheduling/Scheduler.cpp
)

target_compile_features(chored PRIVATE cxx_std_17)
set_target_properties(chored PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(chored PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(chored PRIVATE
        CHORED_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(chored PRIVATE
        tomlplusplus::tomlplusplus
        Threads::Threads
)

if(MSVC)
    target_compile_options(chored PRIVATE /W4 /permissive-)
else()
    target_compile_options(chored PRIVATE -Wall -Wextra -Wpedantic)
endif()
