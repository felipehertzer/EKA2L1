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

#include <drivers/graphics/shader.h>

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

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace eka2l1::drivers {
    class vulkan_graphics_driver;
    class vulkan_texture;

    static constexpr std::size_t VULKAN_SHADER_MAX_TEXTURE_SLOTS = 32;

    struct vulkan_shader_descriptor_snapshot {
        vk::UniqueDescriptorPool descriptor_pool;
        vk::UniqueDeviceMemory uniform_memory;
        vk::UniqueBuffer uniform_buffer;
        vk::DescriptorSet descriptor_set = nullptr;
    };

    struct vulkan_shader_descriptor_state {
        std::unordered_map<int, std::vector<std::uint8_t>> uniform_values;
        std::unordered_map<int, int> texture_bindings;
    };

    class vulkan_shader_module : public shader_module {
        shader_module_type type_;
        std::vector<char> source_;
        vk::UniqueShaderModule module_;

    public:
        vulkan_shader_module();
        ~vulkan_shader_module() override;

        bool create(graphics_driver *driver, const char *data, const std::size_t size,
            const shader_module_type type, std::string *compile_log = nullptr) override;

        shader_module_type type() const {
            return type_;
        }

        const std::vector<char> &source() const {
            return source_;
        }

        vk::ShaderModule module() const {
            return module_.get();
        }
    };

    class vulkan_shader_program : public shader_program {
        vulkan_shader_module *vertex_module_;
        vulkan_shader_module *fragment_module_;
        std::vector<std::uint8_t> metadata_;
        std::unordered_map<int, std::vector<std::uint8_t>> uniform_values_;
        std::unordered_map<int, int> texture_bindings_;
        std::unordered_map<int, std::size_t> uniform_offsets_;
        std::unordered_map<int, std::uint32_t> sampler_descriptor_bindings_;
        std::size_t uniform_buffer_size_;
        vk::UniqueShaderModule compiled_vertex_module_;
        vk::UniqueShaderModule compiled_fragment_module_;
        vk::UniqueDescriptorSetLayout descriptor_set_layout_;
        vk::UniqueDescriptorPool descriptor_pool_;
        vk::DescriptorSet descriptor_set_;
        vk::UniqueBuffer uniform_buffer_;
        vk::UniqueDeviceMemory uniform_memory_;

        bool compile_glsl_modules(vulkan_graphics_driver *driver, std::string *link_log);
        bool create_descriptor_resources(vulkan_graphics_driver *driver);

    public:
        vulkan_shader_program();
        ~vulkan_shader_program() override;

        bool create(graphics_driver *driver, shader_module *vertex_module, shader_module *fragment_module,
            std::string *link_log = nullptr) override;
        bool use(graphics_driver *driver) override;

        std::optional<int> get_uniform_location(const std::string &name) override;
        std::optional<int> get_attrib_location(const std::string &name) override;

        void *get_metadata() override;

        void set_uniform_value(const int binding, const std::uint8_t *data, const std::size_t size);
        void set_texture_binding(const int shader_binding, const int texture_slot);
        void capture_descriptor_state(vulkan_shader_descriptor_state &state) const;

        const std::vector<std::uint8_t> *uniform_value(const int binding) const;
        int texture_binding(const int shader_binding) const;
        bool prepare_descriptors(vulkan_graphics_driver *driver,
            const std::array<vulkan_texture *, VULKAN_SHADER_MAX_TEXTURE_SLOTS> &texture_slots,
            const vulkan_shader_descriptor_state *state = nullptr);
        bool prepare_descriptor_snapshot(vulkan_graphics_driver *driver,
            const std::array<vulkan_texture *, VULKAN_SHADER_MAX_TEXTURE_SLOTS> &texture_slots,
            vulkan_shader_descriptor_snapshot &snapshot,
            const vulkan_shader_descriptor_state *state = nullptr);

        vk::ShaderModule executable_vertex_module() const {
            return compiled_vertex_module_ ? compiled_vertex_module_.get() : (vertex_module_ ? vertex_module_->module() : vk::ShaderModule{});
        }

        vk::ShaderModule executable_fragment_module() const {
            return compiled_fragment_module_ ? compiled_fragment_module_.get() : (fragment_module_ ? fragment_module_->module() : vk::ShaderModule{});
        }

        vk::DescriptorSetLayout descriptor_set_layout() const {
            return descriptor_set_layout_.get();
        }

        vk::DescriptorSet descriptor_set() const {
            return descriptor_set_;
        }

        vulkan_shader_module *vertex_module() const {
            return vertex_module_;
        }

        vulkan_shader_module *fragment_module() const {
            return fragment_module_;
        }
    };
}

#endif

#undef EKA2L1_HAS_VULKAN_BACKEND
