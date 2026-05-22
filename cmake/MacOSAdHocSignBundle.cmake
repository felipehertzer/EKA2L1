if(NOT DEFINED EKA2L1_BUNDLE_PATH)
    message(FATAL_ERROR "EKA2L1_BUNDLE_PATH is required")
endif()

get_filename_component(EKA2L1_BUNDLE_PATH "${EKA2L1_BUNDLE_PATH}" ABSOLUTE)

if(DEFINED EKA2L1_MAIN_EXECUTABLE_PATH AND EKA2L1_MAIN_EXECUTABLE_PATH)
    get_filename_component(EKA2L1_MAIN_EXECUTABLE_PATH "${EKA2L1_MAIN_EXECUTABLE_PATH}" ABSOLUTE)
endif()

if(DEFINED EKA2L1_ENTITLEMENTS_PATH AND EKA2L1_ENTITLEMENTS_PATH)
    get_filename_component(EKA2L1_ENTITLEMENTS_PATH "${EKA2L1_ENTITLEMENTS_PATH}" ABSOLUTE)
    if(NOT EXISTS "${EKA2L1_ENTITLEMENTS_PATH}")
        message(WARNING "Entitlements file not found: ${EKA2L1_ENTITLEMENTS_PATH}")
        unset(EKA2L1_ENTITLEMENTS_PATH)
    endif()
endif()

find_program(CODESIGN_EXECUTABLE codesign)
find_program(FILE_EXECUTABLE file)

if(NOT CODESIGN_EXECUTABLE OR NOT FILE_EXECUTABLE)
    message(STATUS "codesign or file not found; skipping macOS ad-hoc bundle signing")
    return()
endif()

message(STATUS "Ad-hoc signing Mach-O files in bundle: ${EKA2L1_BUNDLE_PATH}")

set(SIGN_TEMP_DIR "${CMAKE_BINARY_DIR}/macos-adhoc-sign")
file(REMOVE_RECURSE "${SIGN_TEMP_DIR}")
file(MAKE_DIRECTORY "${SIGN_TEMP_DIR}")

file(GLOB_RECURSE BUNDLE_FILES LIST_DIRECTORIES false "${EKA2L1_BUNDLE_PATH}/Contents/*")

foreach(BUNDLE_FILE IN LISTS BUNDLE_FILES)
    if(IS_SYMLINK "${BUNDLE_FILE}")
        continue()
    endif()

    execute_process(
        COMMAND "${FILE_EXECUTABLE}" -b "${BUNDLE_FILE}"
        OUTPUT_VARIABLE FILE_KIND
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    if(NOT FILE_KIND MATCHES "Mach-O")
        continue()
    endif()

    string(SHA256 SIGN_FILE_HASH "${BUNDLE_FILE}")
    set(SIGN_TEMP_FILE "${SIGN_TEMP_DIR}/${SIGN_FILE_HASH}")

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${BUNDLE_FILE}" "${SIGN_TEMP_FILE}"
        RESULT_VARIABLE COPY_RESULT
        ERROR_VARIABLE COPY_ERROR)
    if(COPY_RESULT)
        message(FATAL_ERROR "Failed to stage ${BUNDLE_FILE} for signing: ${COPY_ERROR}")
    endif()

    set(CODESIGN_ARGS --force --sign - --timestamp=none)
    if(DEFINED EKA2L1_ENTITLEMENTS_PATH
            AND DEFINED EKA2L1_MAIN_EXECUTABLE_PATH
            AND BUNDLE_FILE STREQUAL EKA2L1_MAIN_EXECUTABLE_PATH)
        list(APPEND CODESIGN_ARGS --entitlements "${EKA2L1_ENTITLEMENTS_PATH}")
    endif()

    execute_process(
        COMMAND "${CODESIGN_EXECUTABLE}" ${CODESIGN_ARGS} "${SIGN_TEMP_FILE}"
        RESULT_VARIABLE SIGN_RESULT
        OUTPUT_VARIABLE SIGN_OUTPUT
        ERROR_VARIABLE SIGN_ERROR)
    if(SIGN_RESULT)
        message(FATAL_ERROR "Failed to ad-hoc sign ${BUNDLE_FILE}: ${SIGN_OUTPUT}${SIGN_ERROR}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SIGN_TEMP_FILE}" "${BUNDLE_FILE}"
        RESULT_VARIABLE COPY_BACK_RESULT
        ERROR_VARIABLE COPY_BACK_ERROR)
    if(COPY_BACK_RESULT)
        message(FATAL_ERROR "Failed to copy signed file back to ${BUNDLE_FILE}: ${COPY_BACK_ERROR}")
    endif()
endforeach()

file(GLOB_RECURSE FRAMEWORK_DIRS LIST_DIRECTORIES true
    "${EKA2L1_BUNDLE_PATH}/Contents/Frameworks/*.framework"
    "${EKA2L1_BUNDLE_PATH}/Contents/PlugIns/*.framework")

foreach(FRAMEWORK_DIR IN LISTS FRAMEWORK_DIRS)
    if(NOT FRAMEWORK_DIR MATCHES "\\.framework$")
        continue()
    endif()

    if(NOT IS_DIRECTORY "${FRAMEWORK_DIR}" OR IS_SYMLINK "${FRAMEWORK_DIR}")
        continue()
    endif()

    execute_process(
        COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none "${FRAMEWORK_DIR}"
        RESULT_VARIABLE FRAMEWORK_SIGN_RESULT
        OUTPUT_VARIABLE FRAMEWORK_SIGN_OUTPUT
        ERROR_VARIABLE FRAMEWORK_SIGN_ERROR)
    if(FRAMEWORK_SIGN_RESULT)
        message(FATAL_ERROR "Failed to ad-hoc sign framework ${FRAMEWORK_DIR}: ${FRAMEWORK_SIGN_OUTPUT}${FRAMEWORK_SIGN_ERROR}")
    endif()
endforeach()

set(BUNDLE_CODESIGN_ARGS --force --sign - --timestamp=none)
if(DEFINED EKA2L1_ENTITLEMENTS_PATH AND EKA2L1_ENTITLEMENTS_PATH)
    list(APPEND BUNDLE_CODESIGN_ARGS --entitlements "${EKA2L1_ENTITLEMENTS_PATH}")
endif()

execute_process(
    COMMAND "${CODESIGN_EXECUTABLE}" ${BUNDLE_CODESIGN_ARGS} "${EKA2L1_BUNDLE_PATH}"
    RESULT_VARIABLE BUNDLE_SIGN_RESULT
    OUTPUT_VARIABLE BUNDLE_SIGN_OUTPUT
    ERROR_VARIABLE BUNDLE_SIGN_ERROR)
if(BUNDLE_SIGN_RESULT)
    message(FATAL_ERROR "Failed to ad-hoc sign bundle ${EKA2L1_BUNDLE_PATH}: ${BUNDLE_SIGN_OUTPUT}${BUNDLE_SIGN_ERROR}")
endif()

file(REMOVE_RECURSE "${SIGN_TEMP_DIR}")
