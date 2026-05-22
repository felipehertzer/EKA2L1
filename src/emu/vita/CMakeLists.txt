set(EKA2L1_VITA_SOURCES
    include/vita/emu_window_vita.h
    include/vita/state.h
    include/vita/thread.h
    ../ios/src/launcher.cpp
    src/emu_window_vita.cpp
    src/main.cpp
    src/state.cpp
    src/thread.cpp)

set(EKA2L1_VITA_TITLE_ID "EKA2L1001" CACHE STRING "PS Vita title id")
set(EKA2L1_VITA_TITLE "EKA2L1" CACHE STRING "PS Vita application title")

add_executable(EKA2L1_Vita ${EKA2L1_VITA_SOURCES})

target_include_directories(EKA2L1_Vita PRIVATE
    include
    ../ios/include
    $<TARGET_PROPERTY:common,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:config,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:drivers,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:epoc,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:epocdispatch,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:epockern,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:epocloader,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:epocpkg,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:epocservs,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:epocutils,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:sqlite3,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:stb,INTERFACE_INCLUDE_DIRECTORIES>)

target_compile_definitions(EKA2L1_Vita PRIVATE
    SDL_MAIN_HANDLED
    FMT_DISABLE_STRING_MACRO
    SPDLOG_DISABLE_FMT_STRING
    SPDLOG_NO_TZ_OFFSET
    SPDLOG_FMT_EXTERNAL)

target_link_libraries(EKA2L1_Vita PRIVATE
    epoc
    SDL2)

if (VITA)
    target_link_libraries(EKA2L1_Vita PRIVATE
        SceLibKernel_stub
        SceNet_stub
        SceNetCtl_stub
        SceSysmodule_stub)
    target_link_options(EKA2L1_Vita PRIVATE -pthread)
endif()

add_dependencies(EKA2L1_Vita
    audiooutputrouting
    avkonfep
    ecam
    scdv
    mediaclientaudio
    mediaclientaudiostream
    mediaclientvideo
    vibractrl
    backlightctrl
    postingsurfacefactory
    goommonitor)

add_custom_command(TARGET EKA2L1_Vita POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/eka2l1_vita_resources/resources"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/eka2l1_vita_resources/resources/upscale"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/eka2l1_vita_resources/compat"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/eka2l1_vita_resources/patch"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/src/emu/drivers/resources/upscale/" "${CMAKE_CURRENT_BINARY_DIR}/eka2l1_vita_resources/resources/upscale"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/miscs/compat/" "${CMAKE_CURRENT_BINARY_DIR}/eka2l1_vita_resources/compat"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/src/scripts/" "${CMAKE_CURRENT_BINARY_DIR}/eka2l1_vita_resources/scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/bin/patch/" "${CMAKE_CURRENT_BINARY_DIR}/eka2l1_vita_resources/patch"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/src/emu/drivers/resources/defaultbank.hsb" "${CMAKE_CURRENT_BINARY_DIR}/eka2l1_vita_resources/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/src/emu/drivers/resources/defaultbank.sf2" "${CMAKE_CURRENT_BINARY_DIR}/eka2l1_vita_resources/resources")

if (COMMAND vita_create_self)
    vita_create_self(EKA2L1_Vita.self EKA2L1_Vita UNSAFE)
endif()

if (COMMAND vita_create_vpk)
    vita_create_vpk(EKA2L1_Vita.vpk "${EKA2L1_VITA_TITLE_ID}" EKA2L1_Vita.self
        VERSION "01.00"
        NAME "${EKA2L1_VITA_TITLE}")
endif()
