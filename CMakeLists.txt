cmake_minimum_required(VERSION 3.21)

if (APPLE AND NOT CMAKE_OSX_SYSROOT)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE EKA2L1_DEFAULT_MACOS_SDK
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if (EKA2L1_DEFAULT_MACOS_SDK)
        set(CMAKE_OSX_SYSROOT "${EKA2L1_DEFAULT_MACOS_SDK}" CACHE PATH "macOS SDK used by CMake, clang-tidy, and compile_commands.json" FORCE)
    endif()
endif()

project(EKA2L1
    DESCRIPTION "Symbian OS emulator"
    HOMEPAGE_URL "https://github.com/EKA2L1/EKA2L1"
    LANGUAGES C CXX)

# Clang 14 is required to compile nicely with C++ 20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR})

if (APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(IOS TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Vita" OR VITA OR CMAKE_C_COMPILER MATCHES "arm-vita-eabi")
    set(VITA TRUE CACHE BOOL "Building for PS Vita" FORCE)
endif()

option(EKA2L1_BUILD_TOOLS "Build EKA2L1's tool" ON)
option(EKA2L1_BUILD_TESTS "Build EKA2L1's tests" ON)
option(EKA2L1_ENABLE_SCRIPTING_ABILITY "Enable to script with Python or Lua" ON)
option(EKA2L1_ENABLE_UNEXPECTED_EXCEPTION_HANDLER "Enable EKA2L1 to dump unexpected exception" OFF)
option(EKA2L1_BUILD_VULKAN_BACKEND "Build Vulkan backend" ON)
option(EKA2L1_BUILD_NATIVE_BACKEND "Build native non-Vulkan graphics backend for platforms without Vulkan WSI" OFF)
option(EKA2L1_DEPLOY_DMG "Deploy EKA2L1 as .dmg" OFF)
option(EKA2L1_BUILD_PATCH "Enable building Symbian's DLL patches using Symbian SDK" OFF)
option(EKA2L1_ENABLE_DISCORD_RICH_PRESENCE "Enable support for Discord Rich Presence" OFF)
option(EKA2L1_ENABLE_FFMPEG "Enable FFmpeg-backed media decoding" ON)
option(EKA2L1_ENABLE_DYNARMIC "Enable Dynarmic CPU recompiler" ON)
option(EKA2L1_ENABLE_UPNP "Enable UPnP port mapping support" ON)
option(EKA2L1_ENABLE_NETWORKING "Enable libuv-backed socket and Bluetooth networking" ON)
option(EKA2L1_IOS_USE_DYNARMIC_JIT "Enable Dynarmic JIT for iOS sideload/JIT-entitled builds" OFF)
set(EKA2L1_FFMPEG_ROOT "" CACHE PATH "Path to a platform FFmpeg install root with include/ and lib/")
set(EKA2L1_IOS_FFMPEG_ROOT "" CACHE PATH "Path to an iOS FFmpeg install root with include/ and lib/")

include(cmake/DeveloperTools.cmake)
include(cmake/ProjectOptions.cmake)
eka2l1_enable_ccache()

if (IOS)
    set(EKA2L1_BUILD_TOOLS OFF CACHE BOOL "Build EKA2L1's tool" FORCE)
    set(EKA2L1_BUILD_TESTS OFF CACHE BOOL "Build EKA2L1's tests" FORCE)
    set(EKA2L1_ENABLE_SCRIPTING_ABILITY OFF CACHE BOOL "Enable to script with Python or Lua" FORCE)
    if (EKA2L1_IOS_FFMPEG_ROOT OR EKA2L1_FFMPEG_ROOT)
        set(EKA2L1_ENABLE_FFMPEG ON CACHE BOOL "Enable FFmpeg-backed media decoding" FORCE)
    else()
        set(EKA2L1_ENABLE_FFMPEG OFF CACHE BOOL "Enable FFmpeg-backed media decoding" FORCE)
    endif()

    if (NOT EKA2L1_IOS_USE_DYNARMIC_JIT)
        set(EKA2L1_ENABLE_DYNARMIC OFF CACHE BOOL "Enable Dynarmic CPU recompiler" FORCE)
    endif()
endif()

if (VITA)
    set(EKA2L1_BUILD_TOOLS OFF CACHE BOOL "Build EKA2L1's tool" FORCE)
    set(EKA2L1_BUILD_TESTS OFF CACHE BOOL "Build EKA2L1's tests" FORCE)
    set(EKA2L1_BUILD_VULKAN_BACKEND OFF CACHE BOOL "Build Vulkan backend" FORCE)
    set(EKA2L1_BUILD_NATIVE_BACKEND ON CACHE BOOL "Build native non-Vulkan graphics backend for platforms without Vulkan WSI" FORCE)
    set(EKA2L1_ENABLE_SCRIPTING_ABILITY OFF CACHE BOOL "Enable to script with Python or Lua" FORCE)
    set(EKA2L1_ENABLE_FFMPEG OFF CACHE BOOL "Enable FFmpeg-backed media decoding" FORCE)
    set(EKA2L1_ENABLE_DYNARMIC OFF CACHE BOOL "Enable Dynarmic CPU recompiler" FORCE)
    set(EKA2L1_ENABLE_UPNP OFF CACHE BOOL "Enable UPnP port mapping support" FORCE)

    if (DEFINED ENV{VITASDK} AND EXISTS "$ENV{VITASDK}/share/vita.cmake")
        include("$ENV{VITASDK}/share/vita.cmake")
    endif()

    add_compile_definitions(EKA2L1_PLATFORM_VITA=1)
endif()

if (EKA2L1_BUILD_VULKAN_BACKEND)
    set (BUILD_WITH_VULKAN 1)
else()
    set (BUILD_WITH_VULKAN 0)
endif()

if (EKA2L1_BUILD_NATIVE_BACKEND)
    set (BUILD_WITH_NATIVE 1)
else()
    set (BUILD_WITH_NATIVE 0)
endif()

if (EKA2L1_ENABLE_FFMPEG)
    set(BUILD_WITH_FFMPEG 1)
else()
    set(BUILD_WITH_FFMPEG 0)
endif()

if (EKA2L1_ENABLE_DYNARMIC)
    set(BUILD_WITH_DYNARMIC 1)
else()
    set(BUILD_WITH_DYNARMIC 0)
endif()

if (EKA2L1_ENABLE_UPNP)
    set(BUILD_WITH_UPNP 1)
else()
    set(BUILD_WITH_UPNP 0)
endif()

if (EKA2L1_ENABLE_NETWORKING)
    set(BUILD_WITH_NETWORKING 1)
else()
    set(BUILD_WITH_NETWORKING 0)
endif()

add_compile_definitions(
    EKA2L1_BUILD_VULKAN_BACKEND=${BUILD_WITH_VULKAN}
    EKA2L1_BUILD_NATIVE_BACKEND=${BUILD_WITH_NATIVE}
    EKA2L1_ENABLE_FFMPEG=${BUILD_WITH_FFMPEG}
    EKA2L1_ENABLE_DYNARMIC=${BUILD_WITH_DYNARMIC}
    EKA2L1_ENABLE_UPNP=${BUILD_WITH_UPNP}
    EKA2L1_ENABLE_NETWORKING=${BUILD_WITH_NETWORKING})

set (ENABLE_SCRIPTING 0)
if (EKA2L1_ENABLE_SCRIPTING_ABILITY)
    set (ENABLE_SCRIPTING 1)
endif(EKA2L1_ENABLE_SCRIPTING_ABILITY)

if (CI)
    set (BUILD_FOR_USER 1)
endif (CI)

set (ENABLE_SEH_HANDLER 0)

if (EKA2L1_ENABLE_UNEXPECTED_EXCEPTION_HANDLER)
    set(ENABLE_SEH_HANDLER 1)
endif (EKA2L1_ENABLE_UNEXPECTED_EXCEPTION_HANDLER)

if (EKA2L1_ENABLE_DISCORD_RICH_PRESENCE AND NOT ANDROID AND NOT (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    set(ENABLE_DISCORD_RICH_PRESENCE 1)
else()
    set(ENABLE_DISCORD_RICH_PRESENCE 0)
endif()

add_subdirectory(src/patch)
add_subdirectory(src/external)

# Keep vendored dependencies on their own flags, then apply project-owned
# compile options to emulator, tool, and test targets.
set(CMAKE_CXX_EXTENSIONS OFF)
eka2l1_apply_project_compile_options()

if (VITA AND TARGET spdlog)
    get_target_property(EKA2L1_SPDLOG_INTERFACE_LIBS spdlog INTERFACE_LINK_LIBRARIES)
    if (EKA2L1_SPDLOG_INTERFACE_LIBS)
        list(REMOVE_ITEM EKA2L1_SPDLOG_INTERFACE_LIBS Threads::Threads)
        set_target_properties(spdlog PROPERTIES INTERFACE_LINK_LIBRARIES "${EKA2L1_SPDLOG_INTERFACE_LIBS}")
    endif()
endif()

add_subdirectory(src/emu)

if (EKA2L1_BUILD_TOOLS)
    add_subdirectory(src/tools)
endif()

if (EKA2L1_BUILD_TESTS)
    enable_testing()
    add_subdirectory(src/tests)
endif()

eka2l1_configure_lint_dependencies()
