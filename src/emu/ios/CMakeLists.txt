set(EKA2L1_IOS_SOURCES
    include/ios/emu_window_ios.h
    include/ios/input_dialog.h
    include/ios/launcher.h
    include/ios/state.h
    include/ios/thread.h
    src/emu_window_ios.mm
    src/input_dialog.mm
    src/launcher.cpp
    src/applauncher.mm
    src/main.mm
    src/state.cpp
    src/thread.cpp)

set(EKA2L1_IOS_BUNDLE_IDENTIFIER "com.eka2l1.ios" CACHE STRING "Bundle identifier for the iOS app")
set(EKA2L1_IOS_DEVELOPMENT_TEAM "" CACHE STRING "Apple development team ID used for iOS device signing")

add_executable(EKA2L1_iOS MACOSX_BUNDLE ${EKA2L1_IOS_SOURCES})

target_include_directories(EKA2L1_iOS PRIVATE
    include
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
    $<TARGET_PROPERTY:stb,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(EKA2L1_iOS PRIVATE
    FMT_DISABLE_STRING_MACRO
    SPDLOG_DISABLE_FMT_STRING
    SPDLOG_FMT_EXTERNAL)
target_link_options(EKA2L1_iOS PRIVATE "-Wl,-no_warn_duplicate_libraries")

target_link_libraries(EKA2L1_iOS PRIVATE
    epoc
    "-framework Foundation"
    "-framework CoreGraphics"
    "-framework GameController"
    "-framework UIKit"
    "-framework QuartzCore")

add_dependencies(EKA2L1_iOS
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

set_target_properties(EKA2L1_iOS PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "${EKA2L1_IOS_BUNDLE_IDENTIFIER}"
    MACOSX_BUNDLE_BUNDLE_NAME "EKA2L1"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${EKA2L1_IOS_BUNDLE_IDENTIFIER}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_INFOPLIST_KEY_UIFileSharingEnabled "YES"
    XCODE_ATTRIBUTE_INFOPLIST_KEY_LSSupportsOpeningDocumentsInPlace "YES")

if (EKA2L1_IOS_DEVELOPMENT_TEAM)
    set_target_properties(EKA2L1_iOS PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${EKA2L1_IOS_DEVELOPMENT_TEAM}")
else()
    set_target_properties(EKA2L1_iOS PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED[sdk=iphoneos*] "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED[sdk=iphonesimulator*] "YES"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY[sdk=iphonesimulator*] "-"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE[sdk=iphonesimulator*] "Manual")
endif()

add_custom_command(TARGET EKA2L1_iOS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_BUNDLE_DIR:EKA2L1_iOS>/resources"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_BUNDLE_DIR:EKA2L1_iOS>/eka2l1_resources"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:EKA2L1_iOS>/eka2l1_resources"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:EKA2L1_iOS>/eka2l1_resources/upscale"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:EKA2L1_iOS>/compat"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:EKA2L1_iOS>/patch"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/src/emu/drivers/resources/upscale/" "$<TARGET_BUNDLE_DIR:EKA2L1_iOS>/eka2l1_resources/upscale"
    COMMAND ${CMAKE_COMMAND} -E remove -f "$<TARGET_BUNDLE_DIR:EKA2L1_iOS>/eka2l1_resources/upscale/.git"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/miscs/compat/" "$<TARGET_BUNDLE_DIR:EKA2L1_iOS>/compat"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/src/scripts/" "$<TARGET_BUNDLE_DIR:EKA2L1_iOS>/scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/bin/patch/" "$<TARGET_BUNDLE_DIR:EKA2L1_iOS>/patch"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/src/emu/drivers/resources/defaultbank.hsb" "$<TARGET_BUNDLE_DIR:EKA2L1_iOS>/eka2l1_resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/src/emu/drivers/resources/defaultbank.sf2" "$<TARGET_BUNDLE_DIR:EKA2L1_iOS>/eka2l1_resources")
