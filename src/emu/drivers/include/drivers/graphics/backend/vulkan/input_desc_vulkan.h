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

#include <drivers/graphics/input_desc.h>

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

#include <vector>

namespace eka2l1::drivers {
    class vulkan_input_descriptors : public input_descriptors {
        std::vector<input_descriptor> inputs_;

    public:
        bool modify(drivers::graphics_driver *drv, input_descriptor *descs, const int count) override;

        const std::vector<input_descriptor> &inputs() const {
            return inputs_;
        }

        static std::vector<vk::VertexInputBindingDescription> binding_descriptions(const std::vector<input_descriptor> &inputs);
        static std::vector<vk::VertexInputAttributeDescription> attribute_descriptions(const std::vector<input_descriptor> &inputs);

        std::vector<vk::VertexInputBindingDescription> binding_descriptions() const;
        std::vector<vk::VertexInputAttributeDescription> attribute_descriptions() const;
    };
}

#endif

#undef EKA2L1_HAS_VULKAN_BACKEND
