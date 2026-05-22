include(ExternalProject)
include(CheckSymbolExists)

if(POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)
endif()

add_library(miniz miniz/miniz.h miniz/miniz.c)
target_include_directories(miniz PUBLIC miniz)

# Force Boost directory for Dynarmic - we never use one
set (Boost_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ext-boost/" CACHE PATH "Boost include directory")
set (Boost_INCLUDE_DIRS "${Boost_INCLUDE_DIR}" CACHE PATH "Boost include directories")
set (BOOST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ext-boost/")
if (NOT TARGET boost)
    add_library(boost INTERFACE)
    target_include_directories(boost SYSTEM INTERFACE ${Boost_INCLUDE_DIRS})
    target_compile_definitions(boost INTERFACE
            BOOST_ERROR_CODE_HEADER_ONLY
            BOOST_SYSTEM_NO_LIB
            BOOST_DATE_TIME_NO_LIB
            BOOST_REGEX_NO_LIB
            BOOST_ALL_NO_LIB)

    if (APPLE)
        target_compile_definitions(boost INTERFACE _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION)
    endif()
endif()

# PugiXML
add_library(pugixml
    pugixml/src/pugiconfig.hpp
    pugixml/src/pugixml.cpp
    pugixml/src/pugixml.hpp)

target_include_directories(pugixml PUBLIC pugixml/src)

set(CMAKE_REQUIRED_QUIET 1)

if (MSVC)
    check_symbol_exists("_M_ARM" "" ARCHITECTURE_ARM32)
    check_symbol_exists("_M_ARM64" "" ARCHITECTURE_AARCH64)
else()
    check_symbol_exists("__arm__" "" ARCHITECTURE_ARM32)
    check_symbol_exists("__aarch64__" "" ARCHITECTURE_AARCH64)
endif()

unset(CMAKE_REQUIRED_QUIET)

set(EKA2L1_BUILD_DYNARMIC OFF)
if (EKA2L1_ENABLE_DYNARMIC AND NOT ARCHITECTURE_ARM32)
    set(EKA2L1_BUILD_DYNARMIC ON)
endif()

# fmt
add_subdirectory(fmt)

if (NOT TARGET fmt::fmt)
    add_library(fmt::fmt ALIAS fmt)
endif()

set (fmt_DIR fmt)
if (IOS OR VITA)
    target_compile_definitions(fmt PUBLIC FMT_DISABLE_STRING_MACRO)
    if (TARGET fmt-header-only)
        target_compile_definitions(fmt-header-only INTERFACE FMT_DISABLE_STRING_MACRO)
    endif()
endif()
if (VITA)
    target_compile_definitions(fmt PUBLIC FMT_USE_FLOCKFILE=0)
    if (TARGET fmt-header-only)
        target_compile_definitions(fmt-header-only INTERFACE FMT_USE_FLOCKFILE=0)
    endif()
endif()

if (EKA2L1_ENABLE_SCRIPTING_ABILITY)
    set(LUA_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/luaorg)

    add_library(liblua INTERFACE)

    if (ANDROID)
        set(LUAJIT_ANDROID_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/luajit-android-src/src")
        set(LUAJIT_ANDROID_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/luajit-android-generated")

        file(MAKE_DIRECTORY "${LUAJIT_ANDROID_GENERATED_DIR}")
        file(READ "${LUAJIT_ANDROID_INCLUDE_DIR}/luajit_rolling.h" LUAJIT_ANDROID_HEADER)
        string(REGEX REPLACE "#error[^\n]*\n" "" LUAJIT_ANDROID_HEADER "${LUAJIT_ANDROID_HEADER}")
        execute_process(
            COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}/luajit-android-src" log -1 --format=%ct
            OUTPUT_VARIABLE LUAJIT_ANDROID_RELVER
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if (NOT LUAJIT_ANDROID_RELVER MATCHES "^[0-9]+$")
            set(LUAJIT_ANDROID_RELVER "ROLLING")
        endif()
        string(REPLACE "ROLLING" "${LUAJIT_ANDROID_RELVER}" LUAJIT_ANDROID_HEADER "${LUAJIT_ANDROID_HEADER}")
        file(WRITE "${LUAJIT_ANDROID_GENERATED_DIR}/luajit.h" "${LUAJIT_ANDROID_HEADER}")

        target_include_directories(liblua INTERFACE
            "${LUAJIT_ANDROID_GENERATED_DIR}"
            "${LUAJIT_ANDROID_INCLUDE_DIR}")

        if (ARCHITECTURE_ARM32)
            target_link_libraries(liblua INTERFACE
                "${CMAKE_CURRENT_SOURCE_DIR}/luajit-android/armv7a/libluajit.a")

            set(LUA_DLL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/luajit-android/armv7a/libluajit.so")
        elseif (ARCHITECTURE_AARCH64)
            target_link_libraries(liblua INTERFACE
                    "${CMAKE_CURRENT_SOURCE_DIR}/luajit-android/aarch64/libluajit.a")

            set(LUA_DLL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/luajit-android/aarch64/libluajit.so")
        else()
            target_link_libraries(liblua INTERFACE
                    "${CMAKE_CURRENT_SOURCE_DIR}/luajit-android/x86_64/libluajit.a")

            set(LUA_DLL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/luajit-android/x86_64/libluajit.so")
		endif()
    else()
        target_include_directories(liblua INTERFACE  "${CMAKE_CURRENT_SOURCE_DIR}/luajit/src/")

        option(LUAJIT_DIR "" ON)
        set(LUAJIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/luajit")

        add_subdirectory(luajit-cmake)

        # Fix up a bit
        target_include_directories(buildvm PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/luajit-cmake")

        target_link_libraries(liblua INTERFACE luajit::lib luajit::header)
        target_include_directories(liblua INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/luajit-cmake")
    endif()
endif()

# SPDLOG - Build examples off
option (SPDLOG_BUILD_EXAMPLES "Build spdlog Examples" OFF) 
option (SPDLOG_BUILD_BENCH "Build spdlog benchmarks" OFF)
option (SPDLOG_FMT_EXTERNAL "Use external fmt library instead of bundled" ON)

if (VITA)
    set(SPDLOG_NO_TZ_OFFSET ON CACHE BOOL "Omit timezone offset on Vita" FORCE)
    set(SPDLOG_FWRITE_UNLOCKED OFF CACHE BOOL "Use regular fwrite on Vita" FORCE)
endif()

add_subdirectory(spdlog)
if (IOS OR VITA)
    target_compile_definitions(spdlog PUBLIC SPDLOG_DISABLE_FMT_STRING)
    target_compile_definitions(spdlog_header_only INTERFACE SPDLOG_DISABLE_FMT_STRING)
endif()
if (VITA)
    target_compile_definitions(spdlog PUBLIC _POSIX_C_SOURCE=200809L)
    target_compile_definitions(spdlog_header_only INTERFACE _POSIX_C_SOURCE=200809L)
endif()

# GLM
option (GLM_TEST_ENABLE "Enable glm tests" OFF)
add_subdirectory(glm)

# Dynarmic: disable tests
option(DYNARMIC_TESTS "Enable dynarmic tests" OFF)

if (EKA2L1_BUILD_DYNARMIC)
    set(DYNARMIC_NO_BUNDLED_FMT ON CACHE BOOL "Use EKA2L1's fmt target for Dynarmic" FORCE)
    add_subdirectory(dynarmic)
endif()

add_subdirectory(lunasvg)

option(YAML_CPP_BUILD_TESTS "Enable YAML cpp testing" OFF)
add_subdirectory(yaml-cpp)

if (MSVC)
    add_library(libdemangle
		libdemangle/include/cxxabi.h
		libcxxabi/src/cxa_demangle.cpp)

    target_include_directories(libdemangle PUBLIC libdemangle/include)
endif()

#stb
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE stb)

#catch2
option (CATCH_BUILD_TESTING "Build catch testing" OFF)
option (CATCH_ENABLE_WERROR "Disable catch werror" OFF)

if (NOT VITA OR EKA2L1_BUILD_TESTS)
    add_subdirectory(Catch2)
endif()

## XXHash
add_library(xxHash STATIC xxHash/xxhash.c)
target_include_directories(xxHash PUBLIC xxHash)

if (EKA2L1_BUILD_TOOLS)
    find_package(LibClang QUIET)

    if (LIBCLANG_FOUND)
        set (LIBCLANG_AVAILABLE TRUE PARENT_SCOPE)

        add_library(libclang INTERFACE)
        target_link_libraries(libclang INTERFACE ${libclang_lib})
        target_include_directories(libclang INTERFACE ${libclang_include_dir})
    endif()
endif()

# Find Vulkan SDK
if (EKA2L1_BUILD_VULKAN_BACKEND)
    find_package(Vulkan QUIET)

    if (NOT Vulkan_FOUND)
        message(FATAL_ERROR "Vulkan SDK is required for the Vulkan-only graphics backend.")
    endif()

    add_library(Vulkan INTERFACE)
    target_include_directories(Vulkan INTERFACE ${Vulkan_INCLUDE_DIRS})
    if (ANDROID AND CMAKE_ANDROID_NDK)
        set(ANDROID_VULKAN_INCLUDE_DIR "${CMAKE_ANDROID_NDK}/sources/third_party/vulkan/src/include")
        if (EXISTS "${ANDROID_VULKAN_INCLUDE_DIR}/vulkan/vulkan.hpp")
            target_include_directories(Vulkan INTERFACE "${ANDROID_VULKAN_INCLUDE_DIR}")
        endif()
    endif()
    target_link_libraries(Vulkan INTERFACE ${Vulkan_LIBRARIES})
endif()

# Cubeb
if (NOT IOS AND NOT VITA)
    option (BUILD_TOOLS "Build tools" OFF)
    option (BUILD_TESTS "Build cubeb tests" OFF)

    # Forcefully disable AAudio (we build with API target 21 minimum)
    set (USE_AAUDIO FALSE CACHE INTERNAL "" FORCE)

    add_subdirectory(cubeb)
endif()


# FFMPEG
if (EKA2L1_ENABLE_FFMPEG)
    add_subdirectory(ffmpeg)
endif()

set(CAPSTONE_BUILD_SHARED_LIBS OFF CACHE BOOL "Build Capstone shared library." FORCE)
set(CAPSTONE_BUILD_STATIC_LIBS ON CACHE BOOL "Build Capstone static library." FORCE)
set(CAPSTONE_BUILD_LEGACY_TESTS OFF CACHE BOOL "Build Capstone legacy tests." FORCE)
set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "Build Capstone cstool." FORCE)
set(CAPSTONE_BUILD_CSTEST OFF CACHE BOOL "Build Capstone cstest." FORCE)
set(CAPSTONE_BUILD_WERROR OFF CACHE BOOL "Treat Capstone warnings as errors." FORCE)
set(CAPSTONE_ARCHITECTURE_DEFAULT OFF CACHE BOOL "Enable all Capstone architectures by default." FORCE)
foreach(_capstone_arch IN ITEMS AARCH64 M68K MIPS PPC SPARC SYSTEMZ XCORE X86 TMS320C64X M680X EVM MOS65XX WASM BPF RISCV SH TRICORE ALPHA HPPA LOONGARCH XTENSA ARC)
    set(CAPSTONE_${_capstone_arch}_SUPPORT OFF CACHE BOOL "${_capstone_arch} support" FORCE)
endforeach()
set(CAPSTONE_ARM_SUPPORT ON CACHE BOOL "ARM support" FORCE)

add_subdirectory(capstone EXCLUDE_FROM_ALL)

# libfat
add_subdirectory(libfat)

# SQLite3
add_library(sqlite3
    sqlite3/sqlite3.c
    sqlite3/sqlite3.h
    sqlite3/sqlite3ext.h)

target_include_directories(sqlite3 PUBLIC sqlite3)
if (VITA)
    target_compile_definitions(sqlite3 PRIVATE SQLITE_MAX_MMAP_SIZE=0 SQLITE_OMIT_WAL SQLITE_OMIT_LOAD_EXTENSION)
    target_compile_options(sqlite3 PRIVATE -O1)
endif()

# miniBae. Later in driver, we will implement the full thing hopefully :D
file(GLOB MINIBAE_SHARED_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/miniBAE/minibae/BAE_Source/Common/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/miniBAE/minibae/BAE_Source/Common/*.h
)

set(MINIBAE_INTERNAL_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/miniBAE/minibae/BAE_Source/Common/
    ${CMAKE_CURRENT_SOURCE_DIR}/miniBAE/minibae/BAE_Source/Platform/
    CACHE INTERNAL "MiniBAE internal include directories")

set(MINIBAE_PUBLIC_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/miniBAE/minibae/
    CACHE INTERNAL "MiniBAE public include directories")

set(MINIBAE_SHARED_SOURCES ${MINIBAE_SHARED_SOURCES} CACHE INTERNAL "MiniBAE source code")

# TinySoundFont
set (TSF_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "TSF include path")

# SDL2
# Can't believe in 2022 the official CMake can't still ship a FindSDL2 with the program...
if (NOT ANDROID AND NOT IOS)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/sdl2-cmake-scripts")

    if(APPLE)
        set(SDL2_PATH "${CMAKE_CURRENT_SOURCE_DIR}/sdl2/macos" CACHE PATH "Where SDL2 is located" FORCE)
    elseif(WIN32)
        set(SDL2_PATH "${CMAKE_CURRENT_SOURCE_DIR}/sdl2/Windows" CACHE PATH "Where SDL2 is located" FORCE)
        set(SDL2_INCLUDE_DIR "${SDL2_PATH}/include" CACHE PATH "Where the SDL2 include files can be found" FORCE)
    elseif(VITA AND DEFINED ENV{VITASDK})
        set(SDL2_PATH "$ENV{VITASDK}/arm-vita-eabi" CACHE PATH "Where SDL2 is located" FORCE)
        set(SDL2_INCLUDE_DIR "${SDL2_PATH}/include/SDL2" CACHE PATH "Where the SDL2 include files can be found" FORCE)
        set(SDL2_BUILDING_LIBRARY ON CACHE BOOL "Do not require SDL2main for Vita" FORCE)

        execute_process(
            COMMAND "${SDL2_PATH}/bin/sdl2-config" --libs
            OUTPUT_VARIABLE EKA2L1_SDL2_VITA_LIBS_RAW
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        separate_arguments(EKA2L1_SDL2_VITA_LIBS UNIX_COMMAND "${EKA2L1_SDL2_VITA_LIBS_RAW}")
        set(SDL2_LIBRARY ${EKA2L1_SDL2_VITA_LIBS} CACHE STRING "Where the SDL2 Library can be found" FORCE)
    endif()

    find_package(SDL2 REQUIRED)
    if (NOT TARGET SDL2)
        add_library(SDL2 INTERFACE)
    endif()

    if (TARGET SDL2::SDL2)
        target_link_libraries(SDL2 INTERFACE SDL2::SDL2)
    else()
        target_include_directories(SDL2 INTERFACE "${SDL2_INCLUDE_DIR}")
        target_link_libraries(SDL2 INTERFACE "${SDL2_LIBRARY}")
    endif()
endif()

# libuv
if (EKA2L1_ENABLE_NETWORKING)
    if (POLICY CMP0079)
        cmake_policy(SET CMP0079 NEW)
    endif()

    option(LIBUV_BUILD_TESTS "Build test for libuv" OFF)
    option(LIBUV_BUILD_BENCH "Build bench for libuv" OFF)
    set(LIBUV_BUILD_SHARED OFF CACHE BOOL "Build shared libuv library" FORCE)
    set(UVW_BUILD_LIBS ON CACHE BOOL "Prepare uvw targets for static library builds." FORCE)
    set(UVW_BUILD_SHARED_LIB OFF CACHE BOOL "Build uvw as a shared library." FORCE)
    set(UVW_FETCH_LIBUV OFF CACHE BOOL "Fetch the libuv repo using CMake FetchContent facility." FORCE)
    set (BUILD_TESTING OFF CACHE INTERNAL "Enable testing with ctest." FORCE)

    add_subdirectory(libuv)
    add_subdirectory(uvw)
    target_include_directories(uvw PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libuv/include")
    add_subdirectory(uvlooper)
endif()

# UPNP

if (EKA2L1_ENABLE_UPNP)
    option (UPNPC_BUILD_SHARED "Build shared library" FALSE)
    option (UPNPC_BUILD_TESTS "Build test executables" FALSE)
    option (UPNPC_BUILD_SAMPLE "Build sample executables" FALSE)
    option (UPNPC_NO_INSTALL "Disable installation" TRUE)

    add_subdirectory(miniupnp/miniupnpc)
endif()

# libtess2

add_library(libtess2
    libtess2/Source/bucketalloc.c
    libtess2/Source/bucketalloc.h
    libtess2/Source/dict.c
    libtess2/Source/dict.h
    libtess2/Source/geom.c
    libtess2/Source/geom.h
    libtess2/Source/mesh.c
    libtess2/Source/mesh.h
    libtess2/Source/priorityq.c
    libtess2/Source/priorityq.h
    libtess2/Source/sweep.c
    libtess2/Source/sweep.h
    libtess2/Source/tess.c
    libtess2/Source/tess.h)

target_include_directories(libtess2 PUBLIC libtess2/Include)

# RectangleBinPack
add_subdirectory(RectangleBinPack)

# Discord-RPC
# NOTE: Literally brought this code from Vita3K. I give credits to the contributors! if you want
# specific name here please raise an issue on the tracker! Thanks a lot

if(ENABLE_DISCORD_RICH_PRESENCE)
	if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/discord_game_sdk.zip")
		message(STATUS "Downloading discord gamesdk...")
		file(DOWNLOAD https://dl-game-sdk.discordapp.net/2.5.6/discord_game_sdk.zip
			"${CMAKE_BINARY_DIR}/external/discord_game_sdk.zip" SHOW_PROGRESS)
	endif()
endif()

if(ENABLE_DISCORD_RICH_PRESENCE)
	if(NOT EXISTS "${CMAKE_BINARY_DIR}/external/discord_game_sdk")
		file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/external/discord_game_sdk")
		execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${CMAKE_BINARY_DIR}/external/discord_game_sdk.zip"
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/external/discord_game_sdk")
		file(RENAME "${CMAKE_BINARY_DIR}/external/discord_game_sdk/lib/x86_64/discord_game_sdk.so"
			"${CMAKE_BINARY_DIR}/external/discord_game_sdk/lib/x86_64/libdiscord_game_sdk.so")
	endif()

	add_library(discord-rpc STATIC
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/achievement_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/activity_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/application_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/core.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/image_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/lobby_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/network_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/overlay_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/relationship_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/storage_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/store_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/types.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/user_manager.cpp
		${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp/voice_manager.cpp)

	if(APPLE)
		target_link_libraries(discord-rpc PUBLIC "${CMAKE_BINARY_DIR}/external/discord_game_sdk/lib/x86_64/discord_game_sdk.dylib")
	elseif(WIN32)
		target_link_libraries(discord-rpc PUBLIC "${CMAKE_BINARY_DIR}/external/discord_game_sdk/lib/x86_64/discord_game_sdk.dll.lib")
	elseif(UNIX)
		target_link_libraries(discord-rpc PUBLIC "${CMAKE_BINARY_DIR}/external/discord_game_sdk/lib/x86_64/libdiscord_game_sdk.so")
	endif()

	target_include_directories(discord-rpc PUBLIC "${CMAKE_BINARY_DIR}/external/discord_game_sdk/cpp")
	set_property(TARGET discord-rpc PROPERTY FOLDER externals)
endif()

add_definitions(-DFT_CONFIG_OPTION_SUBPIXEL_RENDERING)

set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_ENABLE_ERROR_STRINGS ON CACHE BOOL "" FORCE)

add_subdirectory(freetype)

set (capstone_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/capstone/include" PARENT_SCOPE)
set (YAML_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/yaml-cpp/include" PARENT_SCOPE)
set (LIBCXXABI_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libcxxabi/include" PARENT_SCOPE)
set (FRSML_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/FRSML/include" PARENT_SCOPE)
set (GLM_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/glm" PARENT_SCOPE)
