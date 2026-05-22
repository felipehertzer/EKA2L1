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

#include <drivers/graphics/texture.h>

#include <vulkan/vulkan.hpp>

#include <cstring>
#include <memory>

namespace eka2l1::drivers {
    class vulkan_framebuffer;
    class vulkan_graphics_driver;

    class vulkan_texture : public texture {
        int dimensions_;
        vec3 tex_size_;
        texture_format internal_format_;
        texture_format format_;
        texture_data_type tex_data_type_;
        channel_swizzles swizzle_;
        filter_option min_filter_;
        filter_option mag_filter_;
        addressing_option address_s_;
        addressing_option address_t_;
        addressing_option address_r_;
        float anisotropy_;

        vk::Format vk_format_;
        vk::ImageLayout layout_;
        vk::UniqueImage image_;
        vk::UniqueDeviceMemory memory_;
        vk::UniqueImageView image_view_;
        vk::UniqueSampler sampler_;
        vk::DescriptorSet descriptor_set_;
        vk::UniqueBuffer upload_staging_buffer_;
        vk::UniqueDeviceMemory upload_staging_memory_;
        vk::DeviceSize upload_staging_capacity_;
        vulkan_graphics_driver *owner_;
        vulkan_framebuffer *framebuffer_owner_;
        bool framebuffer_target_;
        bool upload_staging_in_use_;

        bool recreate_image_view(vulkan_graphics_driver *driver);
        bool recreate_sampler(vulkan_graphics_driver *driver);
        bool ensure_upload_staging_buffer(vulkan_graphics_driver *driver, const vk::DeviceSize minimum_size);
        bool upload_rgba8(vulkan_graphics_driver *driver, const vec3 &offset, const vec3 &size,
            const void *data, const std::size_t data_size);
        void update_descriptor(vulkan_graphics_driver *driver);

    public:
        vulkan_texture();
        ~vulkan_texture() override;

        bool create(graphics_driver *driver, const int dim, const int miplvl, const vec3 &size, const texture_format internal_format,
            const texture_format format, const texture_data_type data_type, void *data, const std::size_t data_size,
            const std::size_t pixels_per_line = 0, const std::uint32_t unpack_alignment = 4) override;

        void set_filter_minmag(const bool min, const filter_option op) override;
        void set_addressing_mode(const addressing_direction dir, const addressing_option op) override;
        void set_channel_swizzle(channel_swizzles swizz) override;
        void generate_mips() override;
        void set_max_mip_level(const std::uint32_t max_mip) override;
        void set_anisotropy(vulkan_graphics_driver *driver, const float anisotropy);

        void bind(graphics_driver *driver, const int binding) override;
        void unbind(graphics_driver *driver) override;

        void update_data(graphics_driver *driver, const int mip_lvl, const vec3 &offset, const vec3 &size, const std::size_t pixels_per_line,
            const texture_format data_format, const texture_data_type data_type, const void *data, const std::size_t data_size,
            const std::uint32_t unpack_alignment) override;

        bool read_data(vulkan_graphics_driver *driver, const texture_format data_format, const texture_data_type data_type,
            const eka2l1::point &pos, const eka2l1::object_size &size, std::uint8_t *data);

        vec2 get_size() const override {
            return tex_size_;
        }

        texture_format get_format() const override {
            return internal_format_;
        }

        texture_data_type get_data_type() const override {
            return tex_data_type_;
        }

        int get_total_dimensions() const override {
            return dimensions_;
        }

        std::uint64_t driver_handle() override {
            const VkImage handle = static_cast<VkImage>(image_.get());
            std::uint64_t handle_value = 0;
            static_assert(sizeof(handle) <= sizeof(handle_value), "VkImage handle is larger than driver_handle storage");
            std::memcpy(&handle_value, &handle, sizeof(handle));
            return handle_value;
        }

        vk::DescriptorSet descriptor_set() const {
            return descriptor_set_;
        }

        vk::ImageView image_view() const {
            return image_view_.get();
        }

        vk::Sampler sampler() const {
            return sampler_.get();
        }

        vk::Image image() const {
            return image_.get();
        }

        vk::Format vk_format() const {
            return vk_format_;
        }

        vk::ImageLayout layout() const {
            return layout_;
        }

        void set_layout(const vk::ImageLayout layout) {
            layout_ = layout;
        }

        void record_upload_rgba8(vk::CommandBuffer command_buffer, vk::Buffer staging_buffer,
            const vec3 &offset, const vec3 &size, const vk::ImageLayout old_layout);

        void mark_upload_staging_idle() {
            upload_staging_in_use_ = false;
        }

        bool framebuffer_target() const {
            return framebuffer_target_;
        }

        void set_framebuffer_target(const bool framebuffer_target) {
            framebuffer_target_ = framebuffer_target;
        }

        vulkan_framebuffer *framebuffer_owner() const {
            return framebuffer_owner_;
        }

        void set_framebuffer_owner(vulkan_framebuffer *framebuffer_owner) {
            framebuffer_owner_ = framebuffer_owner;
        }
    };

    class vulkan_renderbuffer : public renderbuffer {
        vec2 size_;
        texture_format format_;
        std::unique_ptr<vulkan_texture> texture_;

    public:
        bool create(graphics_driver *driver, const vec2 &size, const texture_format format) override;

        void bind(graphics_driver *driver, const int binding) override;
        void unbind(graphics_driver *driver) override;

        vec2 get_size() const override {
            return size_;
        }

        texture_format get_format() const override {
            return format_;
        }

        std::uint64_t driver_handle() override {
            return texture_ ? texture_->driver_handle() : 0;
        }

        vulkan_texture *texture() const {
            return texture_.get();
        }
    };
}

#endif

#undef EKA2L1_HAS_VULKAN_BACKEND
