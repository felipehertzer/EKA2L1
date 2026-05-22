include_guard(GLOBAL)

option(EKA2L1_ENABLE_DEVELOPER_TARGETS "Add source lint and format targets" ON)
set(EKA2L1_CLANG_TIDY_JOBS "4" CACHE STRING "Number of parallel clang-tidy jobs used by the lint target")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json for source linting" FORCE)

function(eka2l1_configure_lint_dependencies)
    if (NOT TARGET lint)
        return()
    endif()

    foreach (EKA2L1_LINT_DEPENDENCY IN ITEMS lj_gen_headers)
        if (TARGET ${EKA2L1_LINT_DEPENDENCY})
            add_dependencies(lint ${EKA2L1_LINT_DEPENDENCY})
        endif()
    endforeach()

    if (TARGET eka2l1_qt)
        set(EKA2L1_QT_AUTOGEN_TIMESTAMP "${CMAKE_BINARY_DIR}/src/emu/qt/eka2l1_qt_autogen/timestamp")
        add_custom_target(eka2l1_lint_qt_autogen
            DEPENDS "${EKA2L1_QT_AUTOGEN_TIMESTAMP}")
        add_dependencies(lint eka2l1_lint_qt_autogen)
    endif()
endfunction()

if (NOT EKA2L1_ENABLE_DEVELOPER_TARGETS)
    return()
endif()

set(EKA2L1_LLVM_HINTS
    /opt/homebrew/opt/llvm/bin
    /usr/local/opt/llvm/bin
)

find_program(CLANG_FORMAT_EXECUTABLE
    NAMES clang-format clang-format-22 clang-format-21 clang-format-20 clang-format-19 clang-format-18 clang-format-17
    HINTS ${EKA2L1_LLVM_HINTS}
)

if (CLANG_FORMAT_EXECUTABLE)
    add_custom_target(format
        COMMAND "${CMAKE_COMMAND}"
            -DCLANG_FORMAT_EXECUTABLE=${CLANG_FORMAT_EXECUTABLE}
            -DEKA2L1_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DEKA2L1_FORMAT_MODE=format
            -P "${PROJECT_SOURCE_DIR}/cmake/RunClangFormat.cmake"
        COMMENT "Formatting EKA2L1 source code with clang-format"
        VERBATIM
    )

    add_custom_target(format-check
        COMMAND "${CMAKE_COMMAND}"
            -DCLANG_FORMAT_EXECUTABLE=${CLANG_FORMAT_EXECUTABLE}
            -DEKA2L1_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DEKA2L1_FORMAT_MODE=check
            -P "${PROJECT_SOURCE_DIR}/cmake/RunClangFormat.cmake"
        COMMENT "Checking EKA2L1 source formatting with clang-format"
        VERBATIM
    )
else()
    message(STATUS "clang-format not found; format and format-check targets will not be available")
endif()

find_program(CLANG_TIDY_EXECUTABLE
    NAMES clang-tidy clang-tidy-22 clang-tidy-21 clang-tidy-20 clang-tidy-19 clang-tidy-18 clang-tidy-17
    HINTS ${EKA2L1_LLVM_HINTS}
)

find_program(RUN_CLANG_TIDY_EXECUTABLE
    NAMES run-clang-tidy run-clang-tidy.py
    HINTS ${EKA2L1_LLVM_HINTS}
)

if (CLANG_TIDY_EXECUTABLE AND RUN_CLANG_TIDY_EXECUTABLE)
    add_custom_target(lint
        COMMAND "${RUN_CLANG_TIDY_EXECUTABLE}"
            "-p=${CMAKE_BINARY_DIR}"
            "-clang-tidy-binary=${CLANG_TIDY_EXECUTABLE}"
            "-config-file=${PROJECT_SOURCE_DIR}/.clang-tidy"
            "-header-filter=^${PROJECT_SOURCE_DIR}/(src/emu|src/patch|src/tests|src/tools|native)/"
            "-extra-arg=-Wno-everything"
            "-warnings-as-errors=*"
            "-j=${EKA2L1_CLANG_TIDY_JOBS}"
            -quiet
            "^${PROJECT_SOURCE_DIR}/src/emu/.*"
            "^${PROJECT_SOURCE_DIR}/src/patch/.*"
            "^${PROJECT_SOURCE_DIR}/src/tests/.*"
            "^${PROJECT_SOURCE_DIR}/src/tools/.*"
            "^${PROJECT_SOURCE_DIR}/native/.*"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Linting EKA2L1 source code with clang-tidy"
        VERBATIM
    )
else()
    message(STATUS "clang-tidy or run-clang-tidy not found; lint target will not be available")
endif()
