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
#include <drivers/graphics/backend/vulkan/fb_vulkan.h>
#include <drivers/graphics/backend/vulkan/graphics_vulkan.h>
#include <drivers/graphics/backend/vulkan/texture_vulkan.h>

#include <algorithm>
#include <stdexcept>

namespace eka2l1::drivers {
    static vk::AccessFlags access_for_layout(const vk::ImageLayout layout) {
        switch (layout) {
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            return vk::AccessFlagBits::eShaderRead;

        case vk::ImageLayout::eColorAttachmentOptimal:
            return vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

        case vk::ImageLayout::eTransferDstOptimal:
            return vk::AccessFlagBits::eTransferWrite;

        case vk::ImageLayout::eTransferSrcOptimal:
            return vk::AccessFlagBits::eTransferRead;

        case vk::ImageLayout::eUndefined:
            return {};

        default:
            return vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
        }
    }

    static vk::PipelineStageFlags stage_for_layout(const vk::ImageLayout layout) {
        switch (layout) {
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            return vk::PipelineStageFlagBits::eFragmentShader;

        case vk::ImageLayout::eColorAttachmentOptimal:
            return vk::PipelineStageFlagBits::eColorAttachmentOutput;

        case vk::ImageLayout::eTransferDstOptimal:
        case vk::ImageLayout::eTransferSrcOptimal:
            return vk::PipelineStageFlagBits::eTransfer;

        case vk::ImageLayout::eUndefined:
            return vk::PipelineStageFlagBits::eTopOfPipe;

        default:
            return vk::PipelineStageFlagBits::eAllCommands;
        }
    }

    static void transition_image(vk::CommandBuffer command_buffer, vulkan_texture *texture,
        const vk::ImageLayout new_layout, const vk::AccessFlags dst_access,
        const vk::PipelineStageFlags dst_stage) {
        if (!texture || (texture->layout() == new_layout)) {
            return;
        }

        vk::ImageMemoryBarrier barrier;
        barrier.srcAccessMask = access_for_layout(texture->layout());
        barrier.dstAccessMask = dst_access;
        barrier.oldLayout = texture->layout();
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture->image();
        barrier.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

        command_buffer.pipelineBarrier(
            stage_for_layout(texture->layout()),
            dst_stage,
            vk::DependencyFlags{},
            nullptr,
            nullptr,
            barrier);
        texture->set_layout(new_layout);
    }

    static vulkan_texture *texture_for_attachment(drawable *attachment) {
        if (!attachment) {
            return nullptr;
        }

        if (attachment->get_drawable_type() == DRAWABLE_TYPE_TEXTURE) {
            return reinterpret_cast<vulkan_texture *>(attachment);
        }

        if (attachment->get_drawable_type() == DRAWABLE_TYPE_RENDERBUFFER) {
            return reinterpret_cast<vulkan_renderbuffer *>(attachment)->texture();
        }

        return nullptr;
    }

    vulkan_framebuffer::vulkan_framebuffer(const std::vector<drawable *> &color_buffer_list,
        const std::vector<int> &face_indicies, drawable *depth_buffer, drawable *stencil_buffer,
        const int depth_face_index, const int stencil_face_index)
        : framebuffer(color_buffer_list, depth_buffer, stencil_buffer)
        , draw_attachment_(0)
        , read_attachment_(0)
        , bound_driver_(nullptr)
        , needs_clear_(true)
        , needs_depth_stencil_clear_(true) {
        (void)face_indicies;
        (void)depth_face_index;
        (void)stencil_face_index;
    }

    vulkan_texture *vulkan_framebuffer::attachment_texture(const std::int32_t attachment_id) const {
        if ((attachment_id < 0) || (attachment_id >= static_cast<std::int32_t>(color_buffers.size()))) {
            return nullptr;
        }

        drawable *attachment = color_buffers[attachment_id];
        return texture_for_attachment(attachment);
    }

    vulkan_texture *vulkan_framebuffer::depth_stencil_texture() const {
        drawable *attachment = depth_buffer ? depth_buffer : stencil_buffer;
        return texture_for_attachment(attachment);
    }

    void vulkan_framebuffer::bind(graphics_driver *driver, const framebuffer_bind_type type_bind) {
        bound_driver_ = reinterpret_cast<vulkan_graphics_driver *>(driver);
        if (bound_driver_) {
            bound_driver_->set_bound_framebuffer(this, type_bind);
        }
    }

    void vulkan_framebuffer::unbind(graphics_driver *driver) {
        vulkan_graphics_driver *vulkan_driver = driver ? reinterpret_cast<vulkan_graphics_driver *>(driver) : bound_driver_;
        if (vulkan_driver) {
            vulkan_driver->clear_bound_framebuffer(this);
        }

        if (!driver || (driver == bound_driver_)) {
            bound_driver_ = nullptr;
        }
    }

    bool vulkan_framebuffer::set_draw_buffer(const std::int32_t attachment_id) {
        if ((attachment_id < 0) || !is_attachment_id_valid(attachment_id)) {
            return false;
        }

        draw_attachment_ = attachment_id;
        return true;
    }

    bool vulkan_framebuffer::set_read_buffer(const std::int32_t attachment_id) {
        if ((attachment_id < 0) || !is_attachment_id_valid(attachment_id)) {
            return false;
        }

        read_attachment_ = attachment_id;
        return true;
    }

    bool vulkan_framebuffer::set_depth_stencil_buffer(drawable *depth, drawable *stencil,
        const int depth_face_index, const int stencil_face_index) {
        (void)depth_face_index;
        (void)stencil_face_index;
        depth_buffer = depth;
        stencil_buffer = stencil;
        needs_depth_stencil_clear_ = true;
        return true;
    }

    std::int32_t vulkan_framebuffer::set_color_buffer(drawable *tex, const int face_index,
        const std::int32_t position) {
        if (face_index != 0) {
            LOG_WARN(DRIVER_GRAPHICS, "Vulkan framebuffer only supports 2D color attachments");
            return -1;
        }

        if (!tex) {
            if ((position >= 0) && (position < static_cast<std::int32_t>(color_buffers.size()))) {
                if (drawable *old_attachment = color_buffers[position]) {
                    if (vulkan_texture *old_texture = texture_for_attachment(old_attachment)) {
                        if (old_texture->framebuffer_owner() == this) {
                            old_texture->set_framebuffer_owner(nullptr);
                        }
                    }
                }

                color_buffers[position] = nullptr;
                needs_clear_ = true;
                return position;
            }

            return -1;
        }

        vulkan_texture *attachment_texture = texture_for_attachment(tex);
        if (tex && !attachment_texture) {
            LOG_WARN(DRIVER_GRAPHICS, "Vulkan framebuffer color attachment has no backing image");
            return -1;
        }

        std::int32_t attachment_id = position;
        if (attachment_id < 0) {
            const auto free_slot = std::find(color_buffers.begin(), color_buffers.end(), nullptr);
            if (free_slot != color_buffers.end()) {
                attachment_id = static_cast<std::int32_t>(std::distance(color_buffers.begin(), free_slot));
            } else {
                attachment_id = static_cast<std::int32_t>(color_buffers.size());
            }
        }

        if (attachment_id >= static_cast<std::int32_t>(color_buffers.size())) {
            color_buffers.resize(static_cast<std::size_t>(attachment_id) + 1, nullptr);
        }

        color_buffers[attachment_id] = tex;
        attachment_texture->set_framebuffer_owner(this);
        needs_clear_ = true;
        return attachment_id;
    }

    bool vulkan_framebuffer::blit(const eka2l1::rect &source_rect, const eka2l1::rect &dest_rect,
        const std::uint32_t flags, const filter_option copy_filter) {
        if ((flags & draw_buffer_bit_color_buffer) == 0) {
            return true;
        }

        if (!bound_driver_) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan framebuffer blit has no bound driver");
            return false;
        }

        vulkan_framebuffer *destination_framebuffer = bound_driver_->bound_draw_framebuffer();
        vulkan_texture *source = read_texture();
        vulkan_texture *destination = destination_framebuffer ? destination_framebuffer->draw_texture() : nullptr;
        if (!source || !destination || (source == destination)) {
            return false;
        }

        const vk::Filter filter = (copy_filter == filter_option::nearest) ? vk::Filter::eNearest : vk::Filter::eLinear;
        const vk::ImageLayout source_old_layout = source->layout();
        const vk::ImageLayout destination_old_layout = destination->layout();

        const bool submitted = bound_driver_->submit_immediate([&](vk::CommandBuffer command_buffer) {
            transition_image(command_buffer, source, vk::ImageLayout::eTransferSrcOptimal,
                vk::AccessFlagBits::eTransferRead, vk::PipelineStageFlagBits::eTransfer);
            transition_image(command_buffer, destination, vk::ImageLayout::eTransferDstOptimal,
                vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTransfer);

            vk::ImageBlit blit_region;
            blit_region.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
            blit_region.srcOffsets.data()[0] = vk::Offset3D(source_rect.top.x, source_rect.top.y, 0);
            blit_region.srcOffsets.data()[1] = vk::Offset3D(source_rect.top.x + source_rect.size.x, source_rect.top.y + source_rect.size.y, 1);
            blit_region.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
            blit_region.dstOffsets.data()[0] = vk::Offset3D(dest_rect.top.x, dest_rect.top.y, 0);
            blit_region.dstOffsets.data()[1] = vk::Offset3D(dest_rect.top.x + dest_rect.size.x, dest_rect.top.y + dest_rect.size.y, 1);

            command_buffer.blitImage(source->image(), vk::ImageLayout::eTransferSrcOptimal,
                destination->image(), vk::ImageLayout::eTransferDstOptimal, blit_region, filter);

            transition_image(command_buffer, source, source_old_layout == vk::ImageLayout::eUndefined ? vk::ImageLayout::eShaderReadOnlyOptimal : source_old_layout,
                source_old_layout == vk::ImageLayout::eColorAttachmentOptimal
                    ? vk::AccessFlagBits::eColorAttachmentWrite
                    : vk::AccessFlagBits::eShaderRead,
                source_old_layout == vk::ImageLayout::eColorAttachmentOptimal
                    ? vk::PipelineStageFlagBits::eColorAttachmentOutput
                    : vk::PipelineStageFlagBits::eFragmentShader);
            transition_image(command_buffer, destination, destination_old_layout == vk::ImageLayout::eUndefined ? vk::ImageLayout::eShaderReadOnlyOptimal : destination_old_layout,
                destination_old_layout == vk::ImageLayout::eColorAttachmentOptimal
                    ? vk::AccessFlagBits::eColorAttachmentWrite
                    : vk::AccessFlagBits::eShaderRead,
                destination_old_layout == vk::ImageLayout::eColorAttachmentOptimal
                    ? vk::PipelineStageFlagBits::eColorAttachmentOutput
                    : vk::PipelineStageFlagBits::eFragmentShader);
        });

        if (submitted) {
            source->set_layout(source_old_layout == vk::ImageLayout::eUndefined
                    ? vk::ImageLayout::eShaderReadOnlyOptimal
                    : source_old_layout);
            destination->set_layout(destination_old_layout == vk::ImageLayout::eUndefined
                    ? vk::ImageLayout::eShaderReadOnlyOptimal
                    : destination_old_layout);
            if (destination_framebuffer) {
                destination_framebuffer->mark_cleared();
            }
        }

        return submitted;
    }

    bool vulkan_framebuffer::remove_color_buffer(const std::int32_t position) {
        if ((position < 0) || (position >= static_cast<std::int32_t>(color_buffers.size()))) {
            return false;
        }

        if (drawable *old_attachment = color_buffers[position]) {
            if (vulkan_texture *old_texture = texture_for_attachment(old_attachment)) {
                if (old_texture->framebuffer_owner() == this) {
                    old_texture->set_framebuffer_owner(nullptr);
                }
            }
        }

        color_buffers[position] = nullptr;
        needs_clear_ = true;
        return true;
    }

    bool vulkan_framebuffer::read(const texture_format type, const texture_data_type dest_format,
        const eka2l1::point &pos, const eka2l1::object_size &size, std::uint8_t *buffer_ptr) {
        vulkan_texture *texture = read_texture();
        if (!texture || !bound_driver_) {
            return false;
        }

        return texture->read_data(bound_driver_, type, dest_format, pos, size, buffer_ptr);
    }
}

#endif

#undef EKA2L1_USE_VULKAN_BACKEND
