/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <common/configure.h>
#include <common/platform.h>

#define EKA2L1_HAS_VULKAN_BACKEND (BUILD_WITH_VULKAN && (EKA2L1_PLATFORM(WIN32) || EKA2L1_PLATFORM(ANDROID) || EKA2L1_PLATFORM(MACOS) || EKA2L1_PLATFORM(IOS) || EKA2L1_PLATFORM(UNIX)))

#if EKA2L1_HAS_VULKAN_BACKEND

#include <drivers/graphics/buffer.h>

#if EKA2L1_PLATFORM(WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#elif EKA2L1_PLATFORM(ANDROID)
#define VK_USE_PLATFORM_ANDROID_KHR
#elif EKA2L1_PLATFORM(MACOS) || EKA2L1_PLATFORM(IOS)
#define VK_USE_PLATFORM_METAL_EXT
#elif EKA2L1_PLATFORM(UNIX)
#define VK_USE_PLATFORM_XCB_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif

#include <vulkan/vulkan.hpp>

namespace eka2l1::drivers {
    class vulkan_graphics_driver;

    class vulkan_buffer : public buffer {
        vk::UniqueBuffer buffer_;
        vk::UniqueDeviceMemory memory_;
        std::size_t size_;
        buffer_upload_hint upload_hint_;
        vulkan_graphics_driver *owner_;

        bool allocate(vulkan_graphics_driver *driver, const std::size_t size);

    public:
        vulkan_buffer();
        ~vulkan_buffer() override;

        void bind(graphics_driver *driver) override;
        void unbind(graphics_driver *driver) override;

        bool create(graphics_driver *driver, const void *data, const std::size_t initial_size,
            const buffer_upload_hint use_hint) override;
        void update_data(graphics_driver *driver, const void *data, const std::size_t offset,
            const std::size_t size) override;

        vk::Buffer buffer_handle() const {
            return buffer_.get();
        }

        std::size_t size() const {
            return size_;
        }
    };
}

#endif

#undef EKA2L1_HAS_VULKAN_BACKEND
