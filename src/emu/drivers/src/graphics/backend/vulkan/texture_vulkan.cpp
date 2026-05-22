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

#include <common/bytes.h>
#include <common/log.h>
#include <drivers/graphics/backend/vulkan/graphics_vulkan.h>
#include <drivers/graphics/backend/vulkan/texture_vulkan.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

void decompressBlockETC2(unsigned int block_part1, unsigned int block_part2, std::uint8_t *img, int width, int height, int startx, int starty);
uint32_t PVRTDecompressPVRTC(const void *compressedData, uint32_t do2bitMode, uint32_t xDim, uint32_t yDim, uint32_t doPvrtType, uint8_t *outResultImage);

namespace eka2l1::drivers {
    static vk::Filter to_vulkan_filter(const filter_option option) {
        switch (option) {
        case filter_option::nearest:
        case filter_option::nearest_mipmap_nearest:
        case filter_option::nearest_mipmap_linear:
            return vk::Filter::eNearest;

        default:
            return vk::Filter::eLinear;
        }
    }

    static vk::SamplerMipmapMode to_vulkan_mipmap_mode(const filter_option option) {
        switch (option) {
        case filter_option::nearest_mipmap_nearest:
        case filter_option::linear_mipmap_nearest:
            return vk::SamplerMipmapMode::eNearest;

        default:
            return vk::SamplerMipmapMode::eLinear;
        }
    }

    static vk::SamplerAddressMode to_vulkan_address_mode(const addressing_option option) {
        switch (option) {
        case addressing_option::repeat:
            return vk::SamplerAddressMode::eRepeat;

        case addressing_option::mirrored_repeat:
            return vk::SamplerAddressMode::eMirroredRepeat;

        case addressing_option::clamp_to_edge:
        default:
            return vk::SamplerAddressMode::eClampToEdge;
        }
    }

    static vk::ComponentSwizzle to_vulkan_swizzle(const channel_swizzle swizzle) {
        switch (swizzle) {
        case channel_swizzle::red:
            return vk::ComponentSwizzle::eR;

        case channel_swizzle::green:
            return vk::ComponentSwizzle::eG;

        case channel_swizzle::blue:
            return vk::ComponentSwizzle::eB;

        case channel_swizzle::alpha:
            return vk::ComponentSwizzle::eA;

        case channel_swizzle::zero:
            return vk::ComponentSwizzle::eZero;

        case channel_swizzle::one:
            return vk::ComponentSwizzle::eOne;

        default:
            return vk::ComponentSwizzle::eIdentity;
        }
    }

    static bool is_depth_stencil_format(const texture_format format) {
        return (format == texture_format::depth16) || (format == texture_format::stencil8) || (format == texture_format::depth24_stencil8) || (format == texture_format::depth_stencil);
    }

    static vk::Format choose_depth_stencil_format(vulkan_graphics_driver *driver) {
        static constexpr vk::Format candidates[] = {
            vk::Format::eD24UnormS8Uint,
            vk::Format::eD32SfloatS8Uint
        };

        if (!driver || !driver->physical_device()) {
            return candidates[0];
        }

        for (const vk::Format candidate : candidates) {
            const vk::FormatProperties properties = driver->physical_device().getFormatProperties(candidate);
            if (properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
                return candidate;
            }
        }

        return candidates[0];
    }

    static vk::Format to_vulkan_storage_format(vulkan_graphics_driver *driver, const texture_format internal_format) {
        if (is_depth_stencil_format(internal_format)) {
            return choose_depth_stencil_format(driver);
        }

        return vk::Format::eR8G8B8A8Unorm;
    }

    static std::size_t bytes_per_pixel(const texture_format format, const texture_data_type data_type) {
        if (data_type == texture_data_type::ubyte) {
            switch (format) {
            case texture_format::r:
            case texture_format::r8:
                return 1;

            case texture_format::rg:
            case texture_format::rg8:
                return 2;

            case texture_format::rgb:
            case texture_format::bgr:
                return 3;

            case texture_format::rgba:
            case texture_format::bgra:
                return 4;

            default:
                break;
            }
        }

        switch (data_type) {
        case texture_data_type::ushort_4_4_4_4:
        case texture_data_type::ushort_5_6_5:
        case texture_data_type::ushort_5_5_5_1:
        case texture_data_type::ushort:
            return 2;

        case texture_data_type::uint_24_8:
            return 4;

        default:
            break;
        }

        return 0;
    }

    static std::uint8_t expand_bits(const std::uint32_t value, const std::uint32_t bits) {
        const std::uint32_t max_value = (1U << bits) - 1U;
        return static_cast<std::uint8_t>((value * 255U + (max_value / 2U)) / max_value);
    }

    static std::uint32_t aligned_row_stride(const std::uint32_t width, const std::uint32_t bytes_per_pixel) {
        return ((width * bytes_per_pixel) + 3U) & ~3U;
    }

    static std::size_t aligned_upload_row_stride(const std::uint32_t width, const std::size_t bytes_per_pixel,
        const std::size_t pixels_per_line, const std::uint32_t unpack_alignment) {
        const std::size_t row_pixels = pixels_per_line ? pixels_per_line : width;
        const std::size_t row_bytes = row_pixels * bytes_per_pixel;
        const std::uint32_t alignment = (unpack_alignment == 1 || unpack_alignment == 2 || unpack_alignment == 4 || unpack_alignment == 8) ? unpack_alignment : 4;

        return (row_bytes + alignment - 1U) & ~(static_cast<std::size_t>(alignment) - 1U);
    }

    static bool is_pvrtc_format(const texture_format format) {
        return (format == texture_format::pvrtc_4bppv1_rgba) || (format == texture_format::pvrtc_2bppv1_rgba) || (format == texture_format::pvrtc_4bppv1_rgb) || (format == texture_format::pvrtc_2bppv1_rgb);
    }

    static bool decode_etc2_rgb8_to_rgba8(std::vector<std::uint8_t> &dest, const void *data,
        const std::size_t data_size, const std::uint32_t width, const std::uint32_t height) {
        const std::uint32_t block_width = std::max(1U, (width + 3U) / 4U);
        const std::uint32_t block_height = std::max(1U, (height + 3U) / 4U);
        const std::size_t required_size = static_cast<std::size_t>(block_width) * block_height * 8U;
        if ((data_size != 0) && (data_size < required_size)) {
            LOG_WARN(DRIVER_GRAPHICS, "Vulkan ETC2 texture upload data is smaller than expected");
            dest.clear();
            return false;
        }

        const std::uint32_t padded_width = block_width * 4U;
        const std::uint32_t padded_height = block_height * 4U;
        std::vector<std::uint8_t> decoded_rgb(static_cast<std::size_t>(padded_width) * padded_height * 3U);
        const std::uint32_t *source_u32 = reinterpret_cast<const std::uint32_t *>(data);

        std::uint32_t block_read = 0;
        for (std::uint32_t y = 0; y < block_height; y++) {
            for (std::uint32_t x = 0; x < block_width; x++) {
                const std::uint32_t block_part1 = common::byte_swap(source_u32[block_read++]);
                const std::uint32_t block_part2 = common::byte_swap(source_u32[block_read++]);
                decompressBlockETC2(block_part1, block_part2, decoded_rgb.data(), padded_width, padded_height,
                    static_cast<int>(4U * x), static_cast<int>(4U * y));
            }
        }

        dest.resize(static_cast<std::size_t>(width) * height * 4U);
        for (std::uint32_t y = 0; y < height; y++) {
            const std::uint8_t *source_row = decoded_rgb.data() + (static_cast<std::size_t>(y) * padded_width * 3U);
            std::uint8_t *dest_row = dest.data() + (static_cast<std::size_t>(y) * width * 4U);
            for (std::uint32_t x = 0; x < width; x++) {
                dest_row[x * 4U] = source_row[x * 3U];
                dest_row[x * 4U + 1U] = source_row[x * 3U + 1U];
                dest_row[x * 4U + 2U] = source_row[x * 3U + 2U];
                dest_row[x * 4U + 3U] = 255;
            }
        }

        return true;
    }

    static bool decode_pvrtc_to_rgba8(std::vector<std::uint8_t> &dest, const texture_format format,
        const void *data, const std::uint32_t width, const std::uint32_t height) {
        const std::uint32_t is_2bit = ((format == texture_format::pvrtc_2bppv1_rgba) || (format == texture_format::pvrtc_2bppv1_rgb)) ? 1U : 0U;

        dest.resize(static_cast<std::size_t>(width) * height * 4U);
        PVRTDecompressPVRTC(data, is_2bit, width, height, 0, dest.data());

        if ((format == texture_format::pvrtc_4bppv1_rgb) || (format == texture_format::pvrtc_2bppv1_rgb)) {
            for (std::size_t i = 3; i < dest.size(); i += 4) {
                dest[i] = 255;
            }
        }

        return true;
    }

    static bool convert_to_rgba8(std::vector<std::uint8_t> &dest, const texture_format internal_format,
        const texture_format format, const texture_data_type data_type, const void *data,
        const std::size_t data_size, const std::uint32_t width, const std::uint32_t height,
        const std::size_t pixels_per_line, const std::uint32_t unpack_alignment) {
        if (!data || (width == 0) || (height == 0)) {
            dest.clear();
            return false;
        }

        if (data_type == texture_data_type::compressed) {
            const texture_format compressed_format = internal_format != texture_format::none ? internal_format : format;
            if (compressed_format == texture_format::etc2_rgb8) {
                return decode_etc2_rgb8_to_rgba8(dest, data, data_size, width, height);
            }

            if (is_pvrtc_format(compressed_format)) {
                return decode_pvrtc_to_rgba8(dest, compressed_format, data, width, height);
            }

            LOG_WARN(DRIVER_GRAPHICS, "Unsupported Vulkan compressed texture format {}", static_cast<int>(compressed_format));
            dest.clear();
            return false;
        }

        const std::size_t bpp = bytes_per_pixel(format, data_type);
        if (bpp == 0) {
            LOG_WARN(DRIVER_GRAPHICS, "Unsupported Vulkan texture upload format/type pair ({}, {})",
                static_cast<int>(format), static_cast<int>(data_type));
            dest.clear();
            return false;
        }

        const std::size_t compact_row_stride = (pixels_per_line ? pixels_per_line : width) * bpp;
        std::size_t source_row_stride = aligned_upload_row_stride(width, bpp, pixels_per_line, unpack_alignment);
        const std::size_t required_upload_size = (height > 1)
            ? (source_row_stride * (height - 1U) + (static_cast<std::size_t>(width) * bpp))
            : (static_cast<std::size_t>(width) * bpp);
        if ((data_size != 0) && (data_size < required_upload_size)) {
            source_row_stride = compact_row_stride;
        }
        const std::uint8_t *source = reinterpret_cast<const std::uint8_t *>(data);
        dest.resize(static_cast<std::size_t>(width) * height * 4);

        for (std::uint32_t y = 0; y < height; y++) {
            const std::uint8_t *source_row = source + (source_row_stride * y);
            std::uint8_t *dest_row = dest.data() + (static_cast<std::size_t>(width) * y * 4);

            for (std::uint32_t x = 0; x < width; x++) {
                const std::uint8_t *pixel = source_row + (x * bpp);
                std::uint8_t *out = dest_row + (x * 4);

                if (data_type == texture_data_type::ubyte) {
                    switch (format) {
                    case texture_format::r:
                    case texture_format::r8:
                        out[0] = pixel[0];
                        out[1] = pixel[0];
                        out[2] = pixel[0];
                        out[3] = pixel[0];
                        break;

                    case texture_format::rg:
                    case texture_format::rg8:
                        out[0] = pixel[0];
                        out[1] = pixel[0];
                        out[2] = pixel[0];
                        out[3] = pixel[1];
                        break;

                    case texture_format::rgb:
                        out[0] = pixel[0];
                        out[1] = pixel[1];
                        out[2] = pixel[2];
                        out[3] = 255;
                        break;

                    case texture_format::bgr:
                        out[0] = pixel[2];
                        out[1] = pixel[1];
                        out[2] = pixel[0];
                        out[3] = 255;
                        break;

                    case texture_format::rgba:
                        out[0] = pixel[0];
                        out[1] = pixel[1];
                        out[2] = pixel[2];
                        out[3] = pixel[3];
                        break;

                    case texture_format::bgra:
                        out[0] = pixel[2];
                        out[1] = pixel[1];
                        out[2] = pixel[0];
                        out[3] = pixel[3];
                        break;

                    default:
                        return false;
                    }

                    continue;
                }

                const std::uint16_t packed = static_cast<std::uint16_t>(pixel[0] | (pixel[1] << 8));
                switch (data_type) {
                case texture_data_type::ushort_4_4_4_4:
                    out[0] = expand_bits((packed >> 12) & 0xF, 4);
                    out[1] = expand_bits((packed >> 8) & 0xF, 4);
                    out[2] = expand_bits((packed >> 4) & 0xF, 4);
                    out[3] = expand_bits(packed & 0xF, 4);
                    break;

                case texture_data_type::ushort_5_6_5:
                    out[0] = expand_bits((packed >> 11) & 0x1F, 5);
                    out[1] = expand_bits((packed >> 5) & 0x3F, 6);
                    out[2] = expand_bits(packed & 0x1F, 5);
                    out[3] = 255;
                    break;

                case texture_data_type::ushort_5_5_5_1:
                    out[0] = expand_bits((packed >> 11) & 0x1F, 5);
                    out[1] = expand_bits((packed >> 6) & 0x1F, 5);
                    out[2] = expand_bits((packed >> 1) & 0x1F, 5);
                    out[3] = (packed & 0x1) ? 255 : 0;
                    break;

                case texture_data_type::ushort:
                    out[0] = static_cast<std::uint8_t>(packed >> 8);
                    out[1] = 0;
                    out[2] = 0;
                    out[3] = 255;
                    break;

                default:
                    return false;
                }
            }
        }

        return true;
    }

    static bool convert_from_rgba8(const std::uint8_t *source, const texture_format format,
        const texture_data_type data_type, const std::uint32_t width, const std::uint32_t height,
        std::uint8_t *dest) {
        if (!source || !dest || (width == 0) || (height == 0)) {
            return false;
        }

        if ((format == texture_format::rgba) && (data_type == texture_data_type::ubyte)) {
            const std::uint32_t stride = aligned_row_stride(width, 4);
            for (std::uint32_t y = 0; y < height; y++) {
                std::memcpy(dest + (static_cast<std::size_t>(y) * stride),
                    source + (static_cast<std::size_t>(y) * width * 4), static_cast<std::size_t>(width) * 4);
            }

            return true;
        }

        if ((format == texture_format::rgb) && (data_type == texture_data_type::ubyte)) {
            const std::uint32_t stride = aligned_row_stride(width, 3);
            for (std::uint32_t y = 0; y < height; y++) {
                std::uint8_t *dest_row = dest + (static_cast<std::size_t>(y) * stride);
                const std::uint8_t *source_row = source + (static_cast<std::size_t>(y) * width * 4);

                for (std::uint32_t x = 0; x < width; x++) {
                    dest_row[x * 3] = source_row[x * 4];
                    dest_row[x * 3 + 1] = source_row[x * 4 + 1];
                    dest_row[x * 3 + 2] = source_row[x * 4 + 2];
                }
            }

            return true;
        }

        if ((format == texture_format::rgba4) && (data_type == texture_data_type::ushort_4_4_4_4)) {
            const std::uint32_t stride = aligned_row_stride(width, 2);
            for (std::uint32_t y = 0; y < height; y++) {
                std::uint16_t *dest_row = reinterpret_cast<std::uint16_t *>(dest + (static_cast<std::size_t>(y) * stride));
                const std::uint8_t *source_row = source + (static_cast<std::size_t>(y) * width * 4);

                for (std::uint32_t x = 0; x < width; x++) {
                    const std::uint8_t *pixel = source_row + (x * 4);
                    dest_row[x] = static_cast<std::uint16_t>(
                        (((pixel[0] / 17U) & 0xFU) << 12) | (((pixel[1] / 17U) & 0xFU) << 8) | (((pixel[2] / 17U) & 0xFU) << 4) | ((pixel[3] / 17U) & 0xFU));
                }
            }

            return true;
        }

        if ((format == texture_format::rgb) && (data_type == texture_data_type::ushort_5_6_5)) {
            const std::uint32_t stride = aligned_row_stride(width, 2);
            for (std::uint32_t y = 0; y < height; y++) {
                std::uint16_t *dest_row = reinterpret_cast<std::uint16_t *>(dest + (static_cast<std::size_t>(y) * stride));
                const std::uint8_t *source_row = source + (static_cast<std::size_t>(y) * width * 4);

                for (std::uint32_t x = 0; x < width; x++) {
                    const std::uint8_t *pixel = source_row + (x * 4);
                    dest_row[x] = static_cast<std::uint16_t>(
                        ((pixel[0] & 0xF8U) << 8) | ((pixel[1] & 0xFCU) << 3) | ((pixel[2] & 0xF8U) >> 3));
                }
            }

            return true;
        }

        LOG_ERROR(DRIVER_GRAPHICS, "Unsupported Vulkan texture read format/type pair ({}, {})",
            static_cast<int>(format), static_cast<int>(data_type));
        return false;
    }

    vulkan_texture::vulkan_texture()
        : dimensions_(0)
        , tex_size_(0, 0, 0)
        , internal_format_(texture_format::none)
        , format_(texture_format::none)
        , tex_data_type_(texture_data_type::ubyte)
        , swizzle_({ channel_swizzle::red, channel_swizzle::green, channel_swizzle::blue, channel_swizzle::alpha })
        , min_filter_(filter_option::linear)
        , mag_filter_(filter_option::linear)
        , address_s_(addressing_option::clamp_to_edge)
        , address_t_(addressing_option::clamp_to_edge)
        , address_r_(addressing_option::clamp_to_edge)
        , anisotropy_(1.0f)
        , vk_format_(vk::Format::eUndefined)
        , layout_(vk::ImageLayout::eUndefined)
        , descriptor_set_(nullptr)
        , upload_staging_capacity_(0)
        , owner_(nullptr)
        , framebuffer_owner_(nullptr)
        , framebuffer_target_(false)
        , upload_staging_in_use_(false) {
    }

    vulkan_texture::~vulkan_texture() = default;

    bool vulkan_texture::recreate_image_view(vulkan_graphics_driver *driver) {
        if (!driver || !image_) {
            return false;
        }

        const vk::ImageAspectFlags aspect = is_depth_stencil_format(internal_format_)
            ? (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil)
            : vk::ImageAspectFlagBits::eColor;

        vk::ImageViewCreateInfo view_create_info;
        view_create_info.image = image_.get();
        view_create_info.viewType = vk::ImageViewType::e2D;
        view_create_info.format = vk_format_;
        view_create_info.components = vk::ComponentMapping(
            to_vulkan_swizzle(swizzle_[0]),
            to_vulkan_swizzle(swizzle_[1]),
            to_vulkan_swizzle(swizzle_[2]),
            to_vulkan_swizzle(swizzle_[3]));
        view_create_info.subresourceRange = vk::ImageSubresourceRange(aspect, 0, 1, 0, 1);

        try {
            image_view_ = driver->device().createImageViewUnique(view_create_info);
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan texture image view creation failed: {}", e.what());
            image_view_.reset();
            return false;
        }

        update_descriptor(driver);
        return true;
    }

    bool vulkan_texture::recreate_sampler(vulkan_graphics_driver *driver) {
        if (!driver) {
            return false;
        }

        vk::SamplerCreateInfo sampler_create_info;
        sampler_create_info.magFilter = to_vulkan_filter(mag_filter_);
        sampler_create_info.minFilter = to_vulkan_filter(min_filter_);
        sampler_create_info.mipmapMode = to_vulkan_mipmap_mode(min_filter_);
        sampler_create_info.addressModeU = to_vulkan_address_mode(address_s_);
        sampler_create_info.addressModeV = to_vulkan_address_mode(address_t_);
        sampler_create_info.addressModeW = to_vulkan_address_mode(address_r_);
        sampler_create_info.mipLodBias = 0.0f;
        sampler_create_info.anisotropyEnable = driver->support_extension(graphics_driver_extension_anisotrophy_filtering) && (anisotropy_ > 1.0f);
        sampler_create_info.maxAnisotropy = sampler_create_info.anisotropyEnable ? anisotropy_ : 1.0f;
        sampler_create_info.compareEnable = false;
        sampler_create_info.minLod = 0.0f;
        sampler_create_info.maxLod = 0.0f;
        sampler_create_info.borderColor = vk::BorderColor::eFloatTransparentBlack;

        try {
            sampler_ = driver->device().createSamplerUnique(sampler_create_info);
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan texture sampler creation failed: {}", e.what());
            sampler_.reset();
            return false;
        }

        update_descriptor(driver);
        return true;
    }

    bool vulkan_texture::ensure_upload_staging_buffer(vulkan_graphics_driver *driver, const vk::DeviceSize minimum_size) {
        if (!driver || !driver->device() || (minimum_size == 0)) {
            return false;
        }

        if (upload_staging_buffer_ && upload_staging_memory_ && (upload_staging_capacity_ >= minimum_size)) {
            return true;
        }

        vk::DeviceSize new_capacity = std::max<vk::DeviceSize>(minimum_size, upload_staging_capacity_ * 2);
        new_capacity = std::max<vk::DeviceSize>(new_capacity, 4096);

        try {
            vk::BufferCreateInfo buffer_create_info(
                vk::BufferCreateFlags{},
                new_capacity,
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::SharingMode::eExclusive);

            vk::UniqueBuffer staging_buffer = driver->device().createBufferUnique(buffer_create_info);
            const vk::MemoryRequirements memory_requirements = driver->device().getBufferMemoryRequirements(staging_buffer.get());
            vk::MemoryAllocateInfo allocate_info(
                memory_requirements.size,
                driver->find_memory_type(memory_requirements.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
            vk::UniqueDeviceMemory staging_memory = driver->device().allocateMemoryUnique(allocate_info);
            driver->device().bindBufferMemory(staging_buffer.get(), staging_memory.get(), 0);

            upload_staging_buffer_ = std::move(staging_buffer);
            upload_staging_memory_ = std::move(staging_memory);
            upload_staging_capacity_ = new_capacity;
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan staging texture upload setup failed: {}", e.what());
            upload_staging_buffer_.reset();
            upload_staging_memory_.reset();
            upload_staging_capacity_ = 0;
            return false;
        }

        return true;
    }

    bool vulkan_texture::upload_rgba8(vulkan_graphics_driver *driver, const vec3 &offset, const vec3 &size,
        const void *data, const std::size_t data_size) {
        if (!driver || !image_ || !data || (size.x <= 0) || (size.y <= 0)) {
            return false;
        }

        const vk::DeviceSize upload_size = static_cast<vk::DeviceSize>(size.x) * size.y * 4;
        if ((data_size != 0) && (data_size < upload_size)) {
            LOG_WARN(DRIVER_GRAPHICS, "Vulkan texture upload data is smaller than the requested upload region");
        }

        if (!ensure_upload_staging_buffer(driver, upload_size)) {
            return false;
        }

        if (upload_staging_in_use_ && !driver->flush_pending_texture_uploads()) {
            return false;
        }

        try {
            void *mapped = driver->device().mapMemory(upload_staging_memory_.get(), 0, upload_size);
            std::memcpy(mapped, data, static_cast<std::size_t>(upload_size));
            driver->device().unmapMemory(upload_staging_memory_.get());
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan staging texture upload update failed: {}", e.what());
            return false;
        }

        const vk::ImageLayout old_layout = layout_;
        driver->queue_texture_upload(this, upload_staging_buffer_.get(), offset, size, old_layout);
        upload_staging_in_use_ = true;
        layout_ = vk::ImageLayout::eShaderReadOnlyOptimal;

        return true;
    }

    void vulkan_texture::record_upload_rgba8(vk::CommandBuffer command_buffer, vk::Buffer staging_buffer,
        const vec3 &offset, const vec3 &size, const vk::ImageLayout old_layout) {
        if (!command_buffer || !image_ || !staging_buffer) {
            return;
        }

        const vk::ImageSubresourceRange color_range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

        vk::PipelineStageFlags source_stage = vk::PipelineStageFlagBits::eFragmentShader;
        if (old_layout == vk::ImageLayout::eUndefined) {
            source_stage = vk::PipelineStageFlagBits::eTopOfPipe;
        } else if (old_layout == vk::ImageLayout::eTransferDstOptimal) {
            source_stage = vk::PipelineStageFlagBits::eTransfer;
        } else if (old_layout == vk::ImageLayout::eColorAttachmentOptimal) {
            source_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        }

        vk::ImageMemoryBarrier to_transfer;
        to_transfer.oldLayout = old_layout;
        to_transfer.newLayout = vk::ImageLayout::eTransferDstOptimal;
        to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.image = image_.get();
        to_transfer.subresourceRange = color_range;
        to_transfer.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        command_buffer.pipelineBarrier(
            source_stage,
            vk::PipelineStageFlagBits::eTransfer,
            vk::DependencyFlags{},
            nullptr,
            nullptr,
            to_transfer);

        vk::BufferImageCopy copy_region;
        copy_region.bufferOffset = 0;
        copy_region.bufferRowLength = 0;
        copy_region.bufferImageHeight = 0;
        copy_region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
        copy_region.imageOffset = vk::Offset3D(offset.x, offset.y, 0);
        copy_region.imageExtent = vk::Extent3D(size.x, size.y, 1);
        command_buffer.copyBufferToImage(staging_buffer, image_.get(), vk::ImageLayout::eTransferDstOptimal, copy_region);

        vk::ImageMemoryBarrier to_sample;
        to_sample.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        to_sample.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        to_sample.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        to_sample.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        to_sample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_sample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_sample.image = image_.get();
        to_sample.subresourceRange = color_range;

        command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader,
            vk::DependencyFlags{},
            nullptr,
            nullptr,
            to_sample);
    }

    void vulkan_texture::update_descriptor(vulkan_graphics_driver *driver) {
        if (!driver || !descriptor_set_ || !image_view_ || !sampler_) {
            return;
        }

        driver->update_bitmap_descriptor_set(descriptor_set_, image_view_.get(), sampler_.get());
    }

    bool vulkan_texture::create(graphics_driver *driver, const int dim, const int miplvl, const vec3 &size,
        const texture_format internal_format, const texture_format format, const texture_data_type data_type,
        void *data, const std::size_t data_size, const std::size_t pixels_per_line,
        const std::uint32_t unpack_alignment) {
        (void)miplvl;

        vulkan_graphics_driver *vulkan_driver = reinterpret_cast<vulkan_graphics_driver *>(driver);
        if (!vulkan_driver || !vulkan_driver->device()) {
            return false;
        }
        owner_ = vulkan_driver;

        if (dim != 2) {
            LOG_WARN(DRIVER_GRAPHICS, "Vulkan texture backend currently supports only 2D textures");
            return false;
        }

        dimensions_ = dim;
        tex_size_ = size;
        internal_format_ = internal_format;
        format_ = format;
        tex_data_type_ = data_type;
        vk_format_ = to_vulkan_storage_format(vulkan_driver, internal_format);
        layout_ = vk::ImageLayout::eUndefined;

        image_view_.reset();
        sampler_.reset();
        memory_.reset();
        image_.reset();
        upload_staging_buffer_.reset();
        upload_staging_memory_.reset();
        upload_staging_capacity_ = 0;

        const bool depth_stencil = is_depth_stencil_format(internal_format);
        const vk::ImageUsageFlags usage = depth_stencil
            ? (vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferDst)
            : (vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment);

        try {
            vk::ImageCreateInfo image_create_info;
            image_create_info.imageType = vk::ImageType::e2D;
            image_create_info.format = vk_format_;
            image_create_info.extent = vk::Extent3D(
                std::max(1, size.x),
                std::max(1, size.y),
                1);
            image_create_info.mipLevels = 1;
            image_create_info.arrayLayers = 1;
            image_create_info.samples = vk::SampleCountFlagBits::e1;
            image_create_info.tiling = vk::ImageTiling::eOptimal;
            image_create_info.usage = usage;
            image_create_info.sharingMode = vk::SharingMode::eExclusive;
            image_create_info.initialLayout = vk::ImageLayout::eUndefined;
            image_ = vulkan_driver->device().createImageUnique(image_create_info);

            const vk::MemoryRequirements memory_requirements = vulkan_driver->device().getImageMemoryRequirements(image_.get());
            vk::MemoryAllocateInfo allocate_info(
                memory_requirements.size,
                vulkan_driver->find_memory_type(memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal));
            memory_ = vulkan_driver->device().allocateMemoryUnique(allocate_info);
            vulkan_driver->device().bindImageMemory(image_.get(), memory_.get(), 0);
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan texture image creation failed: {}", e.what());
            return false;
        }

        if (!recreate_image_view(vulkan_driver) || !recreate_sampler(vulkan_driver)) {
            return false;
        }

        if (!depth_stencil) {
            if (!descriptor_set_) {
                descriptor_set_ = vulkan_driver->allocate_bitmap_descriptor_set();
                update_descriptor(vulkan_driver);
            }

            if (data) {
                std::vector<std::uint8_t> converted;
                if (convert_to_rgba8(converted, internal_format, format, data_type, data, data_size, size.x, size.y,
                        pixels_per_line, unpack_alignment)) {
                    upload_rgba8(vulkan_driver, vec3(0, 0, 0), vec3(size.x, size.y, 1), converted.data(), converted.size());
                }
            } else {
                vulkan_driver->submit_immediate([&](vk::CommandBuffer command_buffer) {
                    const vk::ImageSubresourceRange color_range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
                    vk::ImageMemoryBarrier to_sample;
                    to_sample.oldLayout = vk::ImageLayout::eUndefined;
                    to_sample.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                    to_sample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    to_sample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    to_sample.image = image_.get();
                    to_sample.subresourceRange = color_range;
                    to_sample.dstAccessMask = vk::AccessFlagBits::eShaderRead;

                    command_buffer.pipelineBarrier(
                        vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eFragmentShader,
                        vk::DependencyFlags{},
                        nullptr,
                        nullptr,
                        to_sample);
                });
                layout_ = vk::ImageLayout::eShaderReadOnlyOptimal;
            }
        }

        return true;
    }

    void vulkan_texture::set_filter_minmag(const bool min, const filter_option op) {
        if (min) {
            min_filter_ = op;
        } else {
            mag_filter_ = op;
        }

        if (owner_ && image_) {
            recreate_sampler(owner_);
        }
    }

    void vulkan_texture::set_addressing_mode(const addressing_direction dir, const addressing_option op) {
        switch (dir) {
        case addressing_direction::s:
            address_s_ = op;
            break;

        case addressing_direction::t:
            address_t_ = op;
            break;

        case addressing_direction::r:
            address_r_ = op;
            break;

        default:
            break;
        }

        if (owner_ && image_) {
            recreate_sampler(owner_);
        }
    }

    void vulkan_texture::set_channel_swizzle(channel_swizzles swizz) {
        swizzle_ = swizz;

        if (owner_ && image_) {
            recreate_image_view(owner_);
        }
    }

    void vulkan_texture::generate_mips() {
    }

    void vulkan_texture::set_max_mip_level(const std::uint32_t max_mip) {
        (void)max_mip;
    }

    void vulkan_texture::set_anisotropy(vulkan_graphics_driver *driver, const float anisotropy) {
        anisotropy_ = std::max(1.0f, anisotropy);

        if (driver && image_) {
            recreate_sampler(driver);
        }
    }

    void vulkan_texture::bind(graphics_driver *driver, const int binding) {
        (void)driver;
        (void)binding;
    }

    void vulkan_texture::unbind(graphics_driver *driver) {
        (void)driver;
    }

    void vulkan_texture::update_data(graphics_driver *driver, const int mip_lvl, const vec3 &offset, const vec3 &size,
        const std::size_t pixels_per_line, const texture_format data_format, const texture_data_type data_type,
        const void *data, const std::size_t data_size, const std::uint32_t unpack_alignment) {
        (void)mip_lvl;

        vulkan_graphics_driver *vulkan_driver = reinterpret_cast<vulkan_graphics_driver *>(driver);
        if (!vulkan_driver || !image_ || !data || is_depth_stencil_format(internal_format_)) {
            return;
        }

        std::vector<std::uint8_t> converted;
        if (!convert_to_rgba8(converted, data_format, data_format, data_type, data, data_size, size.x, size.y,
                pixels_per_line, unpack_alignment)) {
            return;
        }

        upload_rgba8(vulkan_driver, offset, vec3(size.x, size.y, 1), converted.data(), converted.size());
    }

    bool vulkan_texture::read_data(vulkan_graphics_driver *driver, const texture_format data_format,
        const texture_data_type data_type, const eka2l1::point &pos, const eka2l1::object_size &size,
        std::uint8_t *data) {
        if (!driver || !image_ || !data || is_depth_stencil_format(internal_format_)) {
            return false;
        }

        if (!driver->flush_pending_texture_uploads()) {
            return false;
        }

        if ((pos.x < 0) || (pos.y < 0) || (size.x <= 0) || (size.y <= 0) || ((pos.x + size.x) > tex_size_.x) || ((pos.y + size.y) > tex_size_.y)) {
            LOG_ERROR(DRIVER_GRAPHICS, "Invalid Vulkan texture read region ({}, {} {}x{}) for texture {}x{}",
                pos.x, pos.y, size.x, size.y, tex_size_.x, tex_size_.y);
            return false;
        }

        const vk::DeviceSize read_size = static_cast<vk::DeviceSize>(size.x) * size.y * 4;
        vk::UniqueBuffer staging_buffer;
        vk::UniqueDeviceMemory staging_memory;

        try {
            vk::BufferCreateInfo buffer_create_info(
                vk::BufferCreateFlags{},
                read_size,
                vk::BufferUsageFlagBits::eTransferDst,
                vk::SharingMode::eExclusive);
            staging_buffer = driver->device().createBufferUnique(buffer_create_info);

            const vk::MemoryRequirements memory_requirements = driver->device().getBufferMemoryRequirements(staging_buffer.get());
            vk::MemoryAllocateInfo allocate_info(
                memory_requirements.size,
                driver->find_memory_type(memory_requirements.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
            staging_memory = driver->device().allocateMemoryUnique(allocate_info);
            driver->device().bindBufferMemory(staging_buffer.get(), staging_memory.get(), 0);
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan staging texture read setup failed: {}", e.what());
            return false;
        }

        const vk::ImageLayout old_layout = layout_;
        vk::AccessFlags old_access = {};
        vk::PipelineStageFlags old_stage = vk::PipelineStageFlagBits::eTopOfPipe;

        switch (old_layout) {
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            old_access = vk::AccessFlagBits::eShaderRead;
            old_stage = vk::PipelineStageFlagBits::eFragmentShader;
            break;

        case vk::ImageLayout::eColorAttachmentOptimal:
            old_access = vk::AccessFlagBits::eColorAttachmentWrite;
            old_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            break;

        case vk::ImageLayout::eTransferDstOptimal:
            old_access = vk::AccessFlagBits::eTransferWrite;
            old_stage = vk::PipelineStageFlagBits::eTransfer;
            break;

        case vk::ImageLayout::eTransferSrcOptimal:
            old_access = vk::AccessFlagBits::eTransferRead;
            old_stage = vk::PipelineStageFlagBits::eTransfer;
            break;

        case vk::ImageLayout::eUndefined:
            break;

        default:
            old_access = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
            old_stage = vk::PipelineStageFlagBits::eAllCommands;
            break;
        }

        const vk::ImageLayout final_layout = (old_layout == vk::ImageLayout::eUndefined)
            ? vk::ImageLayout::eShaderReadOnlyOptimal
            : old_layout;

        const bool submitted = driver->submit_immediate([&](vk::CommandBuffer command_buffer) {
            const vk::ImageSubresourceRange color_range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

            if (old_layout != vk::ImageLayout::eTransferSrcOptimal) {
                vk::ImageMemoryBarrier to_transfer;
                to_transfer.srcAccessMask = old_access;
                to_transfer.dstAccessMask = vk::AccessFlagBits::eTransferRead;
                to_transfer.oldLayout = old_layout;
                to_transfer.newLayout = vk::ImageLayout::eTransferSrcOptimal;
                to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                to_transfer.image = image_.get();
                to_transfer.subresourceRange = color_range;

                command_buffer.pipelineBarrier(
                    old_stage,
                    vk::PipelineStageFlagBits::eTransfer,
                    vk::DependencyFlags{},
                    nullptr,
                    nullptr,
                    to_transfer);
            }

            vk::BufferImageCopy copy_region;
            copy_region.bufferOffset = 0;
            copy_region.bufferRowLength = 0;
            copy_region.bufferImageHeight = 0;
            copy_region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
            copy_region.imageOffset = vk::Offset3D(pos.x, pos.y, 0);
            copy_region.imageExtent = vk::Extent3D(size.x, size.y, 1);
            command_buffer.copyImageToBuffer(image_.get(), vk::ImageLayout::eTransferSrcOptimal, staging_buffer.get(), copy_region);

            vk::BufferMemoryBarrier buffer_to_host;
            buffer_to_host.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            buffer_to_host.dstAccessMask = vk::AccessFlagBits::eHostRead;
            buffer_to_host.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            buffer_to_host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            buffer_to_host.buffer = staging_buffer.get();
            buffer_to_host.offset = 0;
            buffer_to_host.size = read_size;

            command_buffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eHost,
                vk::DependencyFlags{},
                nullptr,
                buffer_to_host,
                nullptr);

            if (final_layout != vk::ImageLayout::eTransferSrcOptimal) {
                vk::ImageMemoryBarrier to_final;
                to_final.srcAccessMask = vk::AccessFlagBits::eTransferRead;
                to_final.dstAccessMask = (final_layout == vk::ImageLayout::eShaderReadOnlyOptimal)
                    ? vk::AccessFlagBits::eShaderRead
                    : old_access;
                to_final.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
                to_final.newLayout = final_layout;
                to_final.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                to_final.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                to_final.image = image_.get();
                to_final.subresourceRange = color_range;

                command_buffer.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTransfer,
                    (final_layout == vk::ImageLayout::eShaderReadOnlyOptimal) ? vk::PipelineStageFlagBits::eFragmentShader : old_stage,
                    vk::DependencyFlags{},
                    nullptr,
                    nullptr,
                    to_final);
            }
        });

        if (!submitted) {
            return false;
        }

        layout_ = final_layout;

        try {
            void *mapped = driver->device().mapMemory(staging_memory.get(), 0, read_size);
            const bool converted = convert_from_rgba8(reinterpret_cast<const std::uint8_t *>(mapped),
                data_format, data_type, size.x, size.y, data);
            driver->device().unmapMemory(staging_memory.get());
            return converted;
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan texture read mapping failed: {}", e.what());
            return false;
        }
    }

    bool vulkan_renderbuffer::create(graphics_driver *driver, const vec2 &size, const texture_format format) {
        size_ = size;
        format_ = format;

        texture_data_type data_type = texture_data_type::ubyte;
        switch (format) {
        case texture_format::rgba4:
            data_type = texture_data_type::ushort_4_4_4_4;
            break;

        case texture_format::rgb5_a1:
            data_type = texture_data_type::ushort_5_5_5_1;
            break;

        case texture_format::rgb565:
            data_type = texture_data_type::ushort_5_6_5;
            break;

        case texture_format::depth16:
            data_type = texture_data_type::ushort;
            break;

        case texture_format::depth24_stencil8:
        case texture_format::depth_stencil:
            data_type = texture_data_type::uint_24_8;
            break;

        default:
            break;
        }

        texture_ = std::make_unique<vulkan_texture>();
        if (!texture_->create(driver, 2, 1, vec3(size.x, size.y, 1), format, format, data_type, nullptr, 0)) {
            texture_.reset();
            return false;
        }

        texture_->set_framebuffer_target(true);
        return true;
    }

    void vulkan_renderbuffer::bind(graphics_driver *driver, const int binding) {
        if (texture_) {
            texture_->bind(driver, binding);
        }
    }

    void vulkan_renderbuffer::unbind(graphics_driver *driver) {
        if (texture_) {
            texture_->unbind(driver);
        }
    }
}

#endif

#undef EKA2L1_USE_VULKAN_BACKEND
