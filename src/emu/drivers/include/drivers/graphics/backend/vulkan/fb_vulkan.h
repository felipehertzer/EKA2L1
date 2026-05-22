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

#include <drivers/graphics/fb.h>

namespace eka2l1::drivers {
    class vulkan_graphics_driver;
    class vulkan_texture;

    class vulkan_framebuffer : public framebuffer {
        std::int32_t draw_attachment_;
        std::int32_t read_attachment_;
        vulkan_graphics_driver *bound_driver_;
        bool needs_clear_;
        bool needs_depth_stencil_clear_;

        vulkan_texture *attachment_texture(const std::int32_t attachment_id) const;

    public:
        explicit vulkan_framebuffer(const std::vector<drawable *> &color_buffer_list, const std::vector<int> &face_indicies,
            drawable *depth_buffer, drawable *stencil_buffer, const int depth_face_index, const int stencil_face_index);

        void bind(graphics_driver *driver, const framebuffer_bind_type type_bind) override;
        void unbind(graphics_driver *driver) override;

        bool set_draw_buffer(const std::int32_t attachment_id) override;
        bool set_read_buffer(const std::int32_t attachment_id) override;

        bool set_depth_stencil_buffer(drawable *depth, drawable *stencil,
            const int depth_face_index, const int stencil_face_index) override;
        std::int32_t set_color_buffer(drawable *tex, const int face_index, const std::int32_t position = -1) override;
        bool blit(const eka2l1::rect &source_rect, const eka2l1::rect &dest_rect, const std::uint32_t flags,
            const filter_option copy_filter) override;
        bool remove_color_buffer(const std::int32_t position) override;
        bool read(const texture_format type, const texture_data_type dest_format, const eka2l1::point &pos,
            const eka2l1::object_size &size, std::uint8_t *buffer_ptr) override;

        vulkan_texture *draw_texture() const {
            return attachment_texture(draw_attachment_);
        }

        vulkan_texture *read_texture() const {
            return attachment_texture(read_attachment_);
        }

        bool needs_clear() const {
            return needs_clear_;
        }

        bool needs_depth_stencil_clear() const {
            return needs_depth_stencil_clear_;
        }

        void mark_cleared() {
            needs_clear_ = false;
        }

        void mark_depth_stencil_cleared() {
            needs_depth_stencil_clear_ = false;
        }

        vulkan_texture *depth_stencil_texture() const;
    };
}

#endif

#undef EKA2L1_HAS_VULKAN_BACKEND
