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

set(CHORED_SERVICE_USER "chored" CACHE STRING
        "Linux user that executes chored tasks")

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

install(FILES packaging/chored.sudoers
        DESTINATION /etc/sudoers.d
        RENAME chored
)

install(DIRECTORY DESTINATION /var/lib/chored
        DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
)

set(CPACK_RPM_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")

set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    "/usr/lib/systemd"
    "/usr/lib/systemd/system"
    "/var/lib"
    "/etc/sudoers.d"
)

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_GENERATOR "RPM;DEB")

# RPM specific settings
set(CPACK_RPM_PRE_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/rpm/preinstall.sh")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/rpm/postinstall.sh")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/rpm/postuninstall.sh")

# DEB specific settings
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Junie <junie@jetbrains.com>") # Placeholder maintainer
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/preinst"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/postinst"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/prerm"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/postrm"
)

set(CPACK_INSTALLED_DIRECTORIES "")
set(CPACK_INSTALL_CMAKE_PROJECTS
        "${CMAKE_BINARY_DIR};${PROJECT_NAME};ALL;/")

include(CPack)
