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

# Scheduler, task state, and control server use threads and mutexes.
find_package(Threads REQUIRED)

# Linux daemon and control client.
add_executable(chored
        src/control/ControlServer.cpp
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

if (MSVC)
    target_compile_options(chored PRIVATE /W4 /permissive-)
else ()
    target_compile_options(chored PRIVATE -Wall -Wextra -Wpedantic)
endif ()

include(GNUInstallDirs)

set(CHORED_SERVICE_USER "" CACHE STRING
        "Existing Linux user that executes chored tasks")

if (CHORED_SERVICE_USER STREQUAL "")
    message(FATAL_ERROR
            "Set -DCHORED_SERVICE_USER=<your Linux username>")
endif ()

set(CHORED_SYSTEMD_UNIT_DIR "lib/systemd/system" CACHE STRING
        "Installation directory for the systemd system unit")

configure_file(
        packaging/systemd/chored.service.in
        chored.service
        @ONLY
)

install(TARGETS chored
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/chored.service"
        DESTINATION "${CHORED_SYSTEMD_UNIT_DIR}"
)

install(FILES config/chored.example.toml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/chored
)