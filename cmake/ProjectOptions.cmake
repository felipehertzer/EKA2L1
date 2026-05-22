include_guard(GLOBAL)

option(EKA2L1_ENABLE_CCACHE "Use ccache when it is available" ON)
option(EKA2L1_ENABLE_STRICT_WARNINGS "Enable stricter warnings for project-owned source" OFF)
option(EKA2L1_WARNINGS_AS_ERRORS "Treat project-owned warning diagnostics as errors" OFF)
option(EKA2L1_ENABLE_SANITIZERS "Enable compiler sanitizers for project-owned source" OFF)
set(EKA2L1_SANITIZERS "address;undefined" CACHE STRING "Semicolon-separated sanitizer list used when EKA2L1_ENABLE_SANITIZERS is ON")

function(eka2l1_enable_ccache)
    if (NOT EKA2L1_ENABLE_CCACHE)
        return()
    endif()

    find_program(CCACHE_PROGRAM ccache)
    if (NOT CCACHE_PROGRAM)
        message(STATUS "ccache not found")
        return()
    endif()

    if (NOT CMAKE_C_COMPILER_LAUNCHER)
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "C compiler launcher" FORCE)
    endif()

    if (NOT CMAKE_CXX_COMPILER_LAUNCHER)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "CXX compiler launcher" FORCE)
    endif()

    if (APPLE AND NOT CMAKE_OBJCXX_COMPILER_LAUNCHER)
        set(CMAKE_OBJCXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "Objective-CXX compiler launcher" FORCE)
    endif()

    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
endfunction()

function(eka2l1_apply_project_compile_options)
    if (MSVC)
        add_compile_definitions(
            _CRT_SECURE_NO_DEPRECATE
            _CRT_NONSTDC_NO_DEPRECATE
            _DISABLE_VECTOR_ANNOTATION)
    endif()

    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Wno-error)
    endif()

    if (EKA2L1_ENABLE_STRICT_WARNINGS)
        if (MSVC)
            add_compile_options(/W4)
            if (EKA2L1_WARNINGS_AS_ERRORS)
                add_compile_options(/WX)
            endif()
        elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
            add_compile_options(
                -Wall
                -Wextra
                -Wpedantic
                -Wshadow
                -Wconversion
                -Wsign-conversion)
            if (EKA2L1_WARNINGS_AS_ERRORS)
                add_compile_options(-Werror)
            endif()
        endif()
    endif()

    if (EKA2L1_ENABLE_SANITIZERS)
        if (MSVC)
            message(WARNING "EKA2L1_ENABLE_SANITIZERS is not wired for MSVC builds")
        elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
            string(REPLACE ";" "," EKA2L1_SANITIZER_FLAGS "${EKA2L1_SANITIZERS}")
            add_compile_options(-fsanitize=${EKA2L1_SANITIZER_FLAGS} -fno-omit-frame-pointer)
            add_link_options(-fsanitize=${EKA2L1_SANITIZER_FLAGS})
            message(STATUS "Enabled sanitizers for project source: ${EKA2L1_SANITIZER_FLAGS}")
        endif()
    endif()
endfunction()
