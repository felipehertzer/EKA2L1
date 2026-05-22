cmake_minimum_required(VERSION 3.21)

if (NOT DEFINED CLANG_FORMAT_EXECUTABLE)
    message(FATAL_ERROR "CLANG_FORMAT_EXECUTABLE is required")
endif()

if (NOT DEFINED EKA2L1_SOURCE_DIR)
    message(FATAL_ERROR "EKA2L1_SOURCE_DIR is required")
endif()

if (NOT DEFINED EKA2L1_FORMAT_MODE)
    set(EKA2L1_FORMAT_MODE "format")
endif()

if (NOT EKA2L1_FORMAT_MODE STREQUAL "format" AND NOT EKA2L1_FORMAT_MODE STREQUAL "check")
    message(FATAL_ERROR "EKA2L1_FORMAT_MODE must be 'format' or 'check'")
endif()

find_package(Git REQUIRED)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" ls-files --cached --others --exclude-standard
    WORKING_DIRECTORY "${EKA2L1_SOURCE_DIR}"
    OUTPUT_VARIABLE EKA2L1_GIT_FILES
    RESULT_VARIABLE EKA2L1_GIT_RESULT
)

if (NOT EKA2L1_GIT_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to enumerate tracked source files with git")
endif()

string(REPLACE "\n" ";" EKA2L1_GIT_FILES "${EKA2L1_GIT_FILES}")

set(EKA2L1_SUBMODULE_PATHS)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" config --file .gitmodules --get-regexp "submodule\\..*\\.path"
    WORKING_DIRECTORY "${EKA2L1_SOURCE_DIR}"
    OUTPUT_VARIABLE EKA2L1_GIT_SUBMODULE_PATHS
    RESULT_VARIABLE EKA2L1_GIT_SUBMODULE_RESULT
    ERROR_QUIET
)

if (EKA2L1_GIT_SUBMODULE_RESULT EQUAL 0)
    string(REPLACE "\n" ";" EKA2L1_GIT_SUBMODULE_PATHS "${EKA2L1_GIT_SUBMODULE_PATHS}")

    foreach (EKA2L1_GIT_SUBMODULE_PATH IN LISTS EKA2L1_GIT_SUBMODULE_PATHS)
        if (EKA2L1_GIT_SUBMODULE_PATH MATCHES "^[^ ]+ (.+)$")
            list(APPEND EKA2L1_SUBMODULE_PATHS "${CMAKE_MATCH_1}/")
        endif()
    endforeach()
elseif (NOT EKA2L1_GIT_SUBMODULE_RESULT EQUAL 1)
    message(FATAL_ERROR "Failed to enumerate git submodule paths")
endif()

function(eka2l1_path_is_submodule SOURCE_FILE OUT_VAR)
    set(EKA2L1_IS_SUBMODULE FALSE)

    foreach (EKA2L1_SUBMODULE_PATH IN LISTS EKA2L1_SUBMODULE_PATHS)
        string(FIND "${SOURCE_FILE}/" "${EKA2L1_SUBMODULE_PATH}" EKA2L1_SUBMODULE_PATH_POSITION)
        if (EKA2L1_SUBMODULE_PATH_POSITION EQUAL 0)
            set(EKA2L1_IS_SUBMODULE TRUE)
            break()
        endif()
    endforeach()

    set(${OUT_VAR} "${EKA2L1_IS_SUBMODULE}" PARENT_SCOPE)
endfunction()

set(EKA2L1_FORMAT_ARGS "-i")
if (EKA2L1_FORMAT_MODE STREQUAL "check")
    set(EKA2L1_FORMAT_ARGS "--dry-run" "--Werror")
endif()

set(EKA2L1_FORMAT_COUNT 0)
set(EKA2L1_FORMAT_FAILED_COUNT 0)

foreach (SOURCE_FILE IN LISTS EKA2L1_GIT_FILES)
    if (SOURCE_FILE STREQUAL "")
        continue()
    endif()

    eka2l1_path_is_submodule("${SOURCE_FILE}" EKA2L1_IS_SUBMODULE_FILE)
    if (EKA2L1_IS_SUBMODULE_FILE)
        continue()
    endif()

    # src/external also contains vendored source blobs that are not git
    # submodules, such as miniz, sqlite, SDL2, and prebuilt LuaJIT files.
    if (NOT SOURCE_FILE MATCHES "\\.(c|cc|cpp|cxx|h|hh|hpp|hxx|m|mm)$")
        continue()
    endif()

    if (SOURCE_FILE MATCHES "^src/external/")
        continue()
    endif()

    if (NOT EXISTS "${EKA2L1_SOURCE_DIR}/${SOURCE_FILE}")
        continue()
    endif()

    math(EXPR EKA2L1_FORMAT_COUNT "${EKA2L1_FORMAT_COUNT} + 1")

    execute_process(
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" ${EKA2L1_FORMAT_ARGS} "${EKA2L1_SOURCE_DIR}/${SOURCE_FILE}"
        WORKING_DIRECTORY "${EKA2L1_SOURCE_DIR}"
        RESULT_VARIABLE EKA2L1_FORMAT_RESULT
        OUTPUT_VARIABLE EKA2L1_FORMAT_STDOUT
        ERROR_VARIABLE EKA2L1_FORMAT_STDERR
    )

    if (NOT EKA2L1_FORMAT_RESULT EQUAL 0)
        math(EXPR EKA2L1_FORMAT_FAILED_COUNT "${EKA2L1_FORMAT_FAILED_COUNT} + 1")
        message(STATUS "clang-format failed for ${SOURCE_FILE}")
        if (EKA2L1_FORMAT_STDOUT)
            message(STATUS "${EKA2L1_FORMAT_STDOUT}")
        endif()
        if (EKA2L1_FORMAT_STDERR)
            message(STATUS "${EKA2L1_FORMAT_STDERR}")
        endif()
    endif()
endforeach()

if (EKA2L1_FORMAT_FAILED_COUNT GREATER 0)
    message(FATAL_ERROR "clang-format ${EKA2L1_FORMAT_MODE} failed for ${EKA2L1_FORMAT_FAILED_COUNT} source file(s). Run the 'format' target to apply formatting.")
endif()

message(STATUS "clang-format ${EKA2L1_FORMAT_MODE} completed for ${EKA2L1_FORMAT_COUNT} source file(s)")
