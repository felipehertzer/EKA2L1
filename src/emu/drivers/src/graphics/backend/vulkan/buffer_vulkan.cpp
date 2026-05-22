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

#include <common/configure.h>
#include <common/platform.h>

#define EKA2L1_USE_VULKAN_BACKEND (BUILD_WITH_VULKAN && (EKA2L1_PLATFORM(WIN32) || EKA2L1_PLATFORM(ANDROID) || EKA2L1_PLATFORM(MACOS) || EKA2L1_PLATFORM(IOS) || EKA2L1_PLATFORM(UNIX)))

#if EKA2L1_USE_VULKAN_BACKEND

#include <common/log.h>
#include <drivers/graphics/backend/vulkan/buffer_vulkan.h>
#include <drivers/graphics/backend/vulkan/graphics_vulkan.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace eka2l1::drivers {
    vulkan_buffer::vulkan_buffer()
        : size_(0)
        , upload_hint_(buffer_upload_static)
        , owner_(nullptr) {
    }

    vulkan_buffer::~vulkan_buffer() = default;

    bool vulkan_buffer::allocate(vulkan_graphics_driver *driver, const std::size_t size) {
        if (!driver || !driver->device()) {
            return false;
        }

        try {
            vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;

            vk::BufferCreateInfo buffer_create_info(
                vk::BufferCreateFlags{},
                static_cast<vk::DeviceSize>(std::max<std::size_t>(1, size)),
                usage,
                vk::SharingMode::eExclusive);
            buffer_ = driver->device().createBufferUnique(buffer_create_info);

            const vk::MemoryRequirements memory_requirements = driver->device().getBufferMemoryRequirements(buffer_.get());
            vk::MemoryAllocateInfo allocate_info(
                memory_requirements.size,
                driver->find_memory_type(memory_requirements.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
            memory_ = driver->device().allocateMemoryUnique(allocate_info);
            driver->device().bindBufferMemory(buffer_.get(), memory_.get(), 0);
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan buffer allocation failed: {}", e.what());
            buffer_.reset();
            memory_.reset();
            size_ = 0;
            return false;
        }

        size_ = size;
        return true;
    }

    void vulkan_buffer::bind(graphics_driver *driver) {
        (void)driver;
    }

    void vulkan_buffer::unbind(graphics_driver *driver) {
        (void)driver;
    }

    bool vulkan_buffer::create(graphics_driver *driver, const void *data, const std::size_t initial_size,
        const buffer_upload_hint use_hint) {
        vulkan_graphics_driver *vulkan_driver = reinterpret_cast<vulkan_graphics_driver *>(driver);
        owner_ = vulkan_driver;
        upload_hint_ = use_hint;

        if (!allocate(vulkan_driver, initial_size)) {
            return false;
        }

        if (data && initial_size) {
            update_data(driver, data, 0, initial_size);
        }

        return true;
    }

    void vulkan_buffer::update_data(graphics_driver *driver, const void *data, const std::size_t offset,
        const std::size_t size) {
        vulkan_graphics_driver *vulkan_driver = reinterpret_cast<vulkan_graphics_driver *>(driver ? driver : owner_);
        if (!vulkan_driver || !data || (size == 0)) {
            return;
        }

        const std::size_t required_size = offset + size;
        if (!buffer_ || (required_size > size_)) {
            std::vector<std::uint8_t> old_data;
            if (buffer_ && memory_ && size_) {
                old_data.resize(size_);
                try {
                    void *mapped = vulkan_driver->device().mapMemory(memory_.get(), 0, static_cast<vk::DeviceSize>(size_));
                    std::memcpy(old_data.data(), mapped, size_);
                    vulkan_driver->device().unmapMemory(memory_.get());
                } catch (std::exception &e) {
                    LOG_WARN(DRIVER_GRAPHICS, "Failed to preserve old Vulkan buffer contents during resize: {}", e.what());
                    old_data.clear();
                }
            }

            const std::size_t new_size = std::max(required_size, std::max<std::size_t>(size_ * 2, 1));
            if (!allocate(vulkan_driver, new_size)) {
                return;
            }

            if (!old_data.empty()) {
                update_data(vulkan_driver, old_data.data(), 0, old_data.size());
            }
        }

        try {
            void *mapped = vulkan_driver->device().mapMemory(memory_.get(), static_cast<vk::DeviceSize>(offset), static_cast<vk::DeviceSize>(size));
            std::memcpy(mapped, data, size);
            vulkan_driver->device().unmapMemory(memory_.get());
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan buffer update failed: {}", e.what());
        }
    }
}

#endif

#undef EKA2L1_USE_VULKAN_BACKEND
