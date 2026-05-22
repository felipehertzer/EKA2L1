if (BUILD_WITH_VULKAN)
    set(DRIVERS_VULKAN_SRC
            include/drivers/graphics/backend/vulkan/buffer_vulkan.h
            include/drivers/graphics/backend/vulkan/fb_vulkan.h
            include/drivers/graphics/backend/vulkan/graphics_vulkan.h
            include/drivers/graphics/backend/vulkan/input_desc_vulkan.h
            include/drivers/graphics/backend/vulkan/shader_vulkan.h
            include/drivers/graphics/backend/vulkan/texture_vulkan.h
            src/graphics/backend/vulkan/buffer_vulkan.cpp
            src/graphics/backend/vulkan/fb_vulkan.cpp
            src/graphics/backend/vulkan/graphics_vulkan.cpp
            src/graphics/backend/vulkan/input_desc_vulkan.cpp
            src/graphics/backend/vulkan/shader_vulkan.cpp
            src/graphics/backend/vulkan/texture_vulkan.cpp
            src/graphics/backend/texture/etcdec.cxx
            src/graphics/backend/texture/pvrt-dec.cpp)
    if (APPLE)
        list(APPEND DRIVERS_VULKAN_SRC
                src/graphics/backend/vulkan/graphics_vulkan_macos.h
                src/graphics/backend/vulkan/graphics_vulkan_macos.mm)
    endif()
endif ()

if (EKA2L1_BUILD_NATIVE_BACKEND)
    set(DRIVERS_NATIVE_SRC
            include/drivers/graphics/backend/native/graphics_native.h
            src/graphics/backend/native/graphics_native.cpp)
endif()

if (EKA2L1_ENABLE_FFMPEG)
    set(DRIVERS_FFMPEG_SRC
        include/drivers/audio/backend/ffmpeg/dsp_ffmpeg.h
        include/drivers/audio/backend/ffmpeg/player_ffmpeg.h
        include/drivers/video/backend/ffmpeg/video_player_ffmpeg.h
        src/audio/backend/ffmpeg/dsp_ffmpeg.cpp
        src/audio/backend/ffmpeg/player_ffmpeg.cpp
        src/video/backend/ffmpeg/video_player_ffmpeg.cpp)
endif()

if (IOS)
    set(DRIVERS_AUDIO_BACKEND_SRC
        include/drivers/audio/backend/ios/audio_ios.h
        src/audio/backend/ios/audio_ios.mm)
    set(DRIVERS_HWRM_BACKEND_SRC
        include/drivers/hwrm/backend/ios/vibration_ios.h
        src/hwrm/backend/ios/vibration_ios.mm)
    set(DRIVERS_SENSOR_BACKEND_SRC
        src/sensor/backend/ios/sensor_ios.h
        src/sensor/backend/ios/sensor_ios.mm)
    set(DRIVERS_CAMERA_BACKEND_SRC
        include/drivers/camera/backend/ios/camera_ios.h
        src/camera/backend/ios/camera_ios.mm)
elseif(VITA)
    set(DRIVERS_AUDIO_BACKEND_SRC
        include/drivers/audio/backend/sdl2/audio_sdl2.h
        src/audio/backend/sdl2/audio_sdl2.cpp)
    set(DRIVERS_UI_BACKEND_SRC
        src/ui/input_dialog_null.cpp)
    set(DRIVERS_HWRM_BACKEND_SRC)
    set(DRIVERS_SENSOR_BACKEND_SRC)
    set(DRIVERS_CAMERA_BACKEND_SRC)
else()
    set(DRIVERS_AUDIO_BACKEND_SRC
        include/drivers/audio/backend/cubeb/audio_cubeb.h
        include/drivers/audio/backend/cubeb/stream_cubeb.h
        src/audio/backend/cubeb/audio_cubeb.cpp
        src/audio/backend/cubeb/stream_cubeb.cpp)
    set(DRIVERS_UI_BACKEND_SRC
        src/ui/input_dialog_null.cpp)
    set(DRIVERS_HWRM_BACKEND_SRC)
    set(DRIVERS_SENSOR_BACKEND_SRC)
    set(DRIVERS_CAMERA_BACKEND_SRC)
endif()

add_library(drivers
        include/drivers/itc.h
        include/drivers/driver.h
        include/drivers/audio/audio.h
        include/drivers/audio/dsp.h
        include/drivers/audio/player.h
        include/drivers/audio/stream.h
        include/drivers/audio/backend/minibae/player_minibae.h
        include/drivers/audio/backend/tinysoundfont/player_tsf.h
        include/drivers/audio/backend/dsp_shared.h
        include/drivers/audio/backend/player_shared.h
        include/drivers/camera/camera_collection.h
        include/drivers/hwrm/backend/vibration_null.h
        include/drivers/hwrm/vibration.h
        include/drivers/graphics/buffer.h
        include/drivers/graphics/emu_window.h
        include/drivers/graphics/fb.h
        include/drivers/graphics/graphics.h
        include/drivers/graphics/input_desc.h
        include/drivers/graphics/shader.h
        include/drivers/graphics/texture.h
        include/drivers/graphics/backend/graphics_driver_shared.h
        include/drivers/input/emu_controller.h
        include/drivers/sensor/sensor.h
        include/drivers/video/video.h
        src/itc.cpp
        src/audio/audio.cpp
        src/audio/dsp.cpp
        src/audio/player.cpp
        src/audio/stream.cpp
        src/audio/backend/minibae/player_minibae.cpp
        src/audio/backend/tinysoundfont/player_tsf.cpp
        src/audio/backend/bae_platimpl.cpp
        src/audio/backend/dsp_shared.cpp
        src/audio/backend/player_shared.cpp
        src/camera/camera_collection.cpp
        src/hwrm/backend/vibration_null.cpp
        src/hwrm/vibration.cpp
        src/input/common.cpp
        src/graphics/buffer.cpp
        src/graphics/fb.cpp
        src/graphics/graphics.cpp
        src/graphics/input_desc.cpp
        src/graphics/shader.cpp
        src/graphics/texture.cpp
        src/graphics/backend/graphics_driver_shared.cpp
        src/sensor/backend/null/sensor_null.cpp
        src/sensor/sensor.cpp
        src/video/video.cpp
        ${DRIVERS_AUDIO_BACKEND_SRC}
        ${DRIVERS_CAMERA_BACKEND_SRC}
        ${DRIVERS_HWRM_BACKEND_SRC}
        ${DRIVERS_SENSOR_BACKEND_SRC}
        ${DRIVERS_UI_BACKEND_SRC}
        ${DRIVERS_FFMPEG_SRC}
        ${DRIVERS_NATIVE_SRC}
        ${DRIVERS_VULKAN_SRC})
if (NOT ANDROID AND NOT IOS AND NOT VITA)
    target_sources(drivers PRIVATE
        include/drivers/input/backend/emu_controller_sdl2.h
        include/drivers/hwrm/backend/vibration_sdl2.h
        src/graphics/emu_window.cpp
        src/input/backend/emu_controller_sdl2.cpp
        src/input/emu_controller.cpp
        src/hwrm/backend/vibration_sdl2.cpp
        src/sdl2_scoping.cpp
        src/sdl2_scoping.h)
elseif(ANDROID)
    target_sources(drivers PRIVATE
        include/drivers/camera/backend/android/camera_android.h
        include/drivers/camera/backend/android/camera_collection_android.h
        include/drivers/hwrm/backend/vibration_jdk.h
        src/camera/backend/android/camera_android.cpp
        src/camera/backend/android/camera_collection_android.cpp
        src/camera/backend/android/emulator_camera_jni.cpp
        src/camera/backend/android/emulator_camera_jni.h
        src/hwrm/backend/vibration_jdk.cpp
        src/sensor/backend/android/sensor_android.cpp
        src/sensor/backend/android/sensor_android.h)
endif()

if(WIN32)
    target_sources(drivers PRIVATE
        include/drivers/audio/backend/wmf/player_wmf.h
        include/drivers/audio/backend/wmf/wmf_loader.h
        src/audio/backend/wmf/wmf_loader.cpp
        src/audio/backend/wmf/player_wmf.cpp
    )

    target_link_libraries(drivers PRIVATE Mfuuid.lib Propsys.lib)
elseif(IOS)
    target_link_libraries(drivers PUBLIC "-framework AudioToolbox" "-framework AVFoundation" "-framework CoreGraphics" "-framework CoreMedia" "-framework CoreMotion" "-framework CoreVideo" "-framework QuartzCore" "-framework UIKit")
elseif(VITA)
    target_link_libraries(drivers PUBLIC SDL2)
elseif(APPLE)
    target_link_libraries(drivers PUBLIC "-framework AppKit" "-framework QuartzCore")
elseif(HAIKU)
elseif(ANDROID)
else()
    find_package(Wayland REQUIRED)
    target_link_libraries(drivers PUBLIC Wayland::Client)
endif()

# Make miniBAE build alone so that its include directories don't messed up system headers
add_library(miniBAE_EMU STATIC ${MINIBAE_SHARED_SOURCES})
target_include_directories(miniBAE_EMU
    PRIVATE ${MINIBAE_INTERNAL_INCLUDE_DIRS})

if (WIN32)
    set(MINIBAE_X_PLATFORM X_WIN95)
elseif (IOS)
    set(MINIBAE_X_PLATFORM X_IOS)
elseif (APPLE)
    set(MINIBAE_X_PLATFORM X_MACINTOSH)
else()
    set(MINIBAE_X_PLATFORM X_ANDROID)
endif()

if (ARCHITECTURE_ARM32 OR ARCHITECTURE_AARCH64)
    set(MINIBAE_CPU_TYPE kARM)
else()
    set(MINIBAE_CPU_TYPE k80X86)
endif()

set(MINIBAE_COMPILE_DEFINITIONS
    X_PLATFORM=${MINIBAE_X_PLATFORM}
    X_ANDROID=18)

if (NOT WIN32)
    list(APPEND MINIBAE_COMPILE_DEFINITIONS CPU_TYPE=${MINIBAE_CPU_TYPE})
endif()

target_compile_definitions(miniBAE_EMU PRIVATE ${MINIBAE_COMPILE_DEFINITIONS})
target_link_libraries(miniBAE_EMU PRIVATE common)

target_link_libraries(drivers PRIVATE common glm miniBAE_EMU)
if (NOT IOS AND NOT VITA)
    target_link_libraries(drivers PRIVATE cubeb)
endif()
if (EKA2L1_ENABLE_FFMPEG)
    target_link_libraries(drivers PRIVATE ffmpeg)
endif()

if (NOT ANDROID AND NOT IOS AND NOT VITA)
    target_link_libraries(drivers PRIVATE SDL2)
elseif (ANDROID)
    target_link_libraries(drivers PRIVATE android)
endif()

target_include_directories(drivers PUBLIC include ${MINIBAE_PUBLIC_INCLUDE_DIRS} ${TSF_INCLUDE})

if (BUILD_WITH_VULKAN)
    target_link_libraries(drivers PRIVATE Vulkan)
    if (NOT ANDROID)
        find_package(PkgConfig QUIET)
    endif()
    if (NOT ANDROID AND PkgConfig_FOUND)
        pkg_check_modules(SHADERC QUIET IMPORTED_TARGET shaderc)
        if (TARGET PkgConfig::SHADERC)
            target_compile_definitions(drivers PRIVATE EKA2L1_VULKAN_USE_SHADERC=1)
            target_link_libraries(drivers PRIVATE PkgConfig::SHADERC)
        endif()
    endif()
endif ()

if (EKA2L1_BUILD_NATIVE_BACKEND)
    target_link_libraries(drivers PRIVATE SDL2)
endif()
