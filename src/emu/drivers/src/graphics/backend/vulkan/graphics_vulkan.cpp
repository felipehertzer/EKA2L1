/*
 * Copyright (c) 2019 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <common/configure.h>
#include <common/platform.h>

#define EKA2L1_USE_VULKAN_BACKEND (BUILD_WITH_VULKAN && (EKA2L1_PLATFORM(WIN32) || EKA2L1_PLATFORM(ANDROID) || EKA2L1_PLATFORM(MACOS) || EKA2L1_PLATFORM(IOS) || EKA2L1_PLATFORM(UNIX)))

#if EKA2L1_USE_VULKAN_BACKEND

#include <common/log.h>
#include <drivers/graphics/backend/vulkan/buffer_vulkan.h>
#include <drivers/graphics/backend/vulkan/fb_vulkan.h>
#include <drivers/graphics/backend/vulkan/graphics_vulkan.h>
#include <drivers/graphics/backend/vulkan/input_desc_vulkan.h>
#include <drivers/graphics/backend/vulkan/shader_vulkan.h>
#include <drivers/graphics/backend/vulkan/texture_vulkan.h>

#if EKA2L1_PLATFORM(MACOS) || EKA2L1_PLATFORM(IOS)
#include "graphics_vulkan_macos.h"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

#if EKA2L1_VULKAN_USE_SHADERC
#include <shaderc/shaderc.h>
#endif

#if EKA2L1_PLATFORM(MACOS) || EKA2L1_PLATFORM(IOS)
#include <mach-o/dyld.h>
#endif

PFN_vkCreateDebugReportCallbackEXT create_debug_report_callback_ext_;
PFN_vkDestroyDebugReportCallbackEXT destroy_debug_report_callback_ext_;

VkResult vkCreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator,
    VkDebugReportCallbackEXT *pCallback) {
    return create_debug_report_callback_ext_(instance, pCreateInfo, pAllocator, pCallback);
}

void vkDestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks *pAllocator) {
    destroy_debug_report_callback_ext_(instance, callback, pAllocator);
}

namespace eka2l1::drivers {
    struct vulkan_rectangle_push_constants {
        float rectangle[4];
        float color[4];
        float viewport[4];
    };

    struct vulkan_bitmap_push_constants {
        float positions[16];
        float uv_rect[4];
        float color[4];
        float options[4];
    };

    static bool env_flag_enabled(const char *name) {
        const char *value = std::getenv(name);
        if (!value || !value[0]) {
            return false;
        }

        std::string normalized(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        return (normalized != "0") && (normalized != "false") && (normalized != "off") && (normalized != "no");
    }

    static std::uint32_t env_u32_value(const char *name, const std::uint32_t fallback) {
        const char *value = std::getenv(name);
        if (!value || !value[0]) {
            return fallback;
        }

        char *end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        if ((end == value) || (parsed > std::numeric_limits<std::uint32_t>::max())) {
            return fallback;
        }

        return static_cast<std::uint32_t>(parsed);
    }

    static std::string env_string_value(const char *name, const std::string &fallback) {
        const char *value = std::getenv(name);
        return (value && value[0]) ? std::string(value) : fallback;
    }

    static bool capture_mode_contains(const std::string_view modes, const std::string_view needle) {
        std::string normalized(modes);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        return (normalized.find("all") != std::string::npos) || (normalized.find(needle) != std::string::npos);
    }

    static bool write_rgba_ppm(const std::filesystem::path &path, const std::uint8_t *rgba,
        const std::uint32_t width, const std::uint32_t height, const bool bgra_source) {
        if (!rgba || (width == 0) || (height == 0)) {
            return false;
        }

        std::ofstream out(path, std::ios::binary);
        if (!out) {
            return false;
        }

        out << "P6\n"
            << width << " " << height << "\n255\n";

        std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * height * 3);
        for (std::uint32_t y = 0; y < height; y++) {
            for (std::uint32_t x = 0; x < width; x++) {
                const std::size_t src_index = (static_cast<std::size_t>(y) * width + x) * 4;
                const std::size_t dst_index = (static_cast<std::size_t>(y) * width + x) * 3;
                rgb[dst_index + 0] = rgba[src_index + (bgra_source ? 2 : 0)];
                rgb[dst_index + 1] = rgba[src_index + 1];
                rgb[dst_index + 2] = rgba[src_index + (bgra_source ? 0 : 2)];
            }
        }

        out.write(reinterpret_cast<const char *>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
        return out.good();
    }

    static bool is_supported_debug_capture_format(const vk::Format format, bool &bgra_source) {
        switch (format) {
        case vk::Format::eB8G8R8A8Unorm:
        case vk::Format::eB8G8R8A8Srgb:
            bgra_source = true;
            return true;

        case vk::Format::eR8G8B8A8Unorm:
        case vk::Format::eR8G8B8A8Srgb:
            bgra_source = false;
            return true;

        default:
            return false;
        }
    }

    static std::string debug_capture_name(const std::string &label, const std::uint64_t index) {
        std::ostringstream name;
        name << label << "-" << std::setw(6) << std::setfill('0') << index << ".ppm";
        return name.str();
    }

    static vk::Rect2D make_scissor_from_rect(const eka2l1::rect &source, const vk::Extent2D &extent) {
        const int extent_width = static_cast<int>(extent.width);
        const int extent_height = static_cast<int>(extent.height);
        int left = source.top.x;
        int right = source.top.x + source.size.x;
        int top = source.top.y;
        int bottom = source.top.y + source.size.y;

        if (source.size.y < 0) {
            top = extent_height - source.top.y + source.size.y;
            bottom = extent_height - source.top.y;
        }

        if (right < left) {
            std::swap(left, right);
        }

        if (bottom < top) {
            std::swap(top, bottom);
        }

        left = std::clamp(left, 0, extent_width);
        right = std::clamp(right, 0, extent_width);
        top = std::clamp(top, 0, extent_height);
        bottom = std::clamp(bottom, 0, extent_height);

        const std::uint32_t width = static_cast<std::uint32_t>(std::max(0, right - left));
        const std::uint32_t height = static_cast<std::uint32_t>(std::max(0, bottom - top));
        return vk::Rect2D(vk::Offset2D(left, top), vk::Extent2D(width, height));
    }

    static vk::Viewport make_viewport_from_rect(const eka2l1::rect &source, const vk::Extent2D &extent,
        const float depth_range_near, const float depth_range_far) {
        if ((source.size.x == 0) || (source.size.y == 0)) {
            return vk::Viewport(
                0.0f,
                0.0f,
                static_cast<float>(extent.width),
                static_cast<float>(extent.height),
                depth_range_near,
                depth_range_far);
        }

        const float x = static_cast<float>(source.top.x);
        const float width = static_cast<float>(std::abs(source.size.x));
        float y = static_cast<float>(source.top.y);
        float height = static_cast<float>(source.size.y);

        if (source.size.y < 0) {
            y = static_cast<float>(static_cast<int>(extent.height) - source.top.y);
        }

        return vk::Viewport(x, y, width, height, depth_range_near, depth_range_far);
    }

    static vulkan_framebuffer *pending_draw_target(const vulkan_pending_draw &draw) {
        return std::visit([](const auto &entry) -> vulkan_framebuffer * {
            return entry.target;
        },
            draw);
    }

    template <typename DrawFunc>
    static void for_each_scissor(const vulkan_draw_state &state, const vk::Extent2D &extent, DrawFunc draw_func) {
        if (!state.clipping_enabled || state.clip_rects.empty()) {
            draw_func(vk::Rect2D(vk::Offset2D(0, 0), extent));
            return;
        }

        for (const eka2l1::rect &clip_rect : state.clip_rects) {
            const vk::Rect2D scissor = make_scissor_from_rect(clip_rect, extent);
            if ((scissor.extent.width == 0) || (scissor.extent.height == 0)) {
                continue;
            }

            draw_func(scissor);
        }
    }

    static vulkan_blend_state_key make_blend_state_key(const vulkan_draw_state &state) {
        vulkan_blend_state_key key;
        key.blend_enabled = state.blend_enabled;
        key.color_write_mask = state.color_write_mask;
        key.rgb_blend_equation = state.rgb_blend_equation;
        key.alpha_blend_equation = state.alpha_blend_equation;
        key.rgb_source_factor = state.rgb_source_factor;
        key.rgb_dest_factor = state.rgb_dest_factor;
        key.alpha_source_factor = state.alpha_source_factor;
        key.alpha_dest_factor = state.alpha_dest_factor;
        return key;
    }

    static bool operator==(const vulkan_blend_state_key &lhs, const vulkan_blend_state_key &rhs) {
        return (lhs.blend_enabled == rhs.blend_enabled) && (lhs.color_write_mask == rhs.color_write_mask) && (lhs.rgb_blend_equation == rhs.rgb_blend_equation) && (lhs.alpha_blend_equation == rhs.alpha_blend_equation) && (lhs.rgb_source_factor == rhs.rgb_source_factor) && (lhs.rgb_dest_factor == rhs.rgb_dest_factor) && (lhs.alpha_source_factor == rhs.alpha_source_factor) && (lhs.alpha_dest_factor == rhs.alpha_dest_factor);
    }

    static vk::BlendOp to_vulkan_blend_op(const blend_equation equation) {
        switch (equation) {
        case blend_equation::add:
            return vk::BlendOp::eAdd;

        case blend_equation::sub:
            return vk::BlendOp::eSubtract;

        case blend_equation::isub:
            return vk::BlendOp::eReverseSubtract;

        default:
            return vk::BlendOp::eAdd;
        }
    }

    static vk::BlendFactor to_vulkan_blend_factor(const blend_factor factor) {
        switch (factor) {
        case blend_factor::one:
            return vk::BlendFactor::eOne;

        case blend_factor::zero:
            return vk::BlendFactor::eZero;

        case blend_factor::frag_out_alpha:
            return vk::BlendFactor::eSrcAlpha;

        case blend_factor::one_minus_frag_out_alpha:
            return vk::BlendFactor::eOneMinusSrcAlpha;

        case blend_factor::current_alpha:
            return vk::BlendFactor::eDstAlpha;

        case blend_factor::one_minus_current_alpha:
            return vk::BlendFactor::eOneMinusDstAlpha;

        case blend_factor::frag_out_color:
            return vk::BlendFactor::eSrcColor;

        case blend_factor::one_minus_frag_out_color:
            return vk::BlendFactor::eOneMinusSrcColor;

        case blend_factor::current_color:
            return vk::BlendFactor::eDstColor;

        case blend_factor::one_minus_current_color:
            return vk::BlendFactor::eOneMinusDstColor;

        case blend_factor::frag_out_alpha_saturate:
            return vk::BlendFactor::eSrcAlphaSaturate;

        case blend_factor::constant_colour:
            return vk::BlendFactor::eConstantColor;

        case blend_factor::one_minus_constant_colour:
            return vk::BlendFactor::eOneMinusConstantColor;

        case blend_factor::constant_alpha:
            return vk::BlendFactor::eConstantAlpha;

        case blend_factor::one_minus_constant_alpha:
            return vk::BlendFactor::eOneMinusConstantAlpha;

        default:
            return vk::BlendFactor::eOne;
        }
    }

    static vk::ColorComponentFlags make_color_write_mask(const std::uint8_t mask) {
        vk::ColorComponentFlags result{};
        if (mask & 0x1) {
            result |= vk::ColorComponentFlagBits::eR;
        }
        if (mask & 0x2) {
            result |= vk::ColorComponentFlagBits::eG;
        }
        if (mask & 0x4) {
            result |= vk::ColorComponentFlagBits::eB;
        }
        if (mask & 0x8) {
            result |= vk::ColorComponentFlagBits::eA;
        }
        return result;
    }

    static vk::PipelineColorBlendAttachmentState make_blend_attachment(const vulkan_blend_state_key &blend_state) {
        vk::PipelineColorBlendAttachmentState attachment;
        attachment.blendEnable = blend_state.blend_enabled;
        attachment.srcColorBlendFactor = to_vulkan_blend_factor(blend_state.rgb_source_factor);
        attachment.dstColorBlendFactor = to_vulkan_blend_factor(blend_state.rgb_dest_factor);
        attachment.colorBlendOp = to_vulkan_blend_op(blend_state.rgb_blend_equation);
        attachment.srcAlphaBlendFactor = to_vulkan_blend_factor(blend_state.alpha_source_factor);
        attachment.dstAlphaBlendFactor = to_vulkan_blend_factor(blend_state.alpha_dest_factor);
        attachment.alphaBlendOp = to_vulkan_blend_op(blend_state.alpha_blend_equation);
        attachment.colorWriteMask = make_color_write_mask(blend_state.color_write_mask);
        return attachment;
    }

    template <typename UpdateFunc>
    static void update_stencil_faces(vulkan_draw_state &state, const rendering_face face, UpdateFunc update_func) {
        if ((face == rendering_face::front) || (face == rendering_face::back_and_front)) {
            update_func(state.front_stencil);
        }

        if ((face == rendering_face::back) || (face == rendering_face::back_and_front)) {
            update_func(state.back_stencil);
        }
    }

    static bool operator==(const vulkan_stencil_face_state &lhs, const vulkan_stencil_face_state &rhs) {
        return (lhs.compare == rhs.compare) && (lhs.reference == rhs.reference) && (lhs.compare_mask == rhs.compare_mask) && (lhs.write_mask == rhs.write_mask) && (lhs.stencil_fail == rhs.stencil_fail) && (lhs.depth_fail == rhs.depth_fail) && (lhs.depth_pass == rhs.depth_pass);
    }

    static bool same_input_descriptor(const input_descriptor &lhs, const input_descriptor &rhs) {
        return (lhs.location == rhs.location)
            && (lhs.offset == rhs.offset)
            && (lhs.format == rhs.format)
            && (lhs.stride == rhs.stride)
            && (lhs.buffer_slot == rhs.buffer_slot);
    }

    static bool same_input_descriptors(const std::vector<input_descriptor> &lhs, const std::vector<input_descriptor> &rhs) {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        for (std::size_t i = 0; i < lhs.size(); i++) {
            if (!same_input_descriptor(lhs[i], rhs[i])) {
                return false;
            }
        }

        return true;
    }

    static const std::vector<input_descriptor> &advanced_draw_inputs(const vulkan_advanced_draw &draw) {
        if (!draw.input_descriptor_snapshot.empty()) {
            return draw.input_descriptor_snapshot;
        }

        if (draw.input_descriptors) {
            return draw.input_descriptors->inputs();
        }

        static const std::vector<input_descriptor> empty_inputs;
        return empty_inputs;
    }

    static bool operator==(const vulkan_advanced_pipeline_key &lhs, const vulkan_advanced_pipeline_key &rhs) {
        return (lhs.program == rhs.program) && same_input_descriptors(lhs.input_descriptors, rhs.input_descriptors) && (lhs.primitive_mode == rhs.primitive_mode) && (lhs.blend_state == rhs.blend_state) && (lhs.cull_enabled == rhs.cull_enabled) && (lhs.cull_face == rhs.cull_face) && (lhs.front_face_rule == rhs.front_face_rule) && (lhs.depth_test_enabled == rhs.depth_test_enabled) && (lhs.depth_write_enabled == rhs.depth_write_enabled) && (lhs.depth_compare == rhs.depth_compare) && (lhs.depth_bias_enabled == rhs.depth_bias_enabled) && (lhs.stencil_test_enabled == rhs.stencil_test_enabled) && (lhs.front_stencil == rhs.front_stencil) && (lhs.back_stencil == rhs.back_stencil) && (lhs.offscreen == rhs.offscreen);
    }

    static vk::PrimitiveTopology to_vulkan_primitive_topology(const graphics_primitive_mode primitive_mode) {
        switch (primitive_mode) {
        case graphics_primitive_mode::points:
            return vk::PrimitiveTopology::ePointList;

        case graphics_primitive_mode::lines:
            return vk::PrimitiveTopology::eLineList;

        case graphics_primitive_mode::line_loop:
        case graphics_primitive_mode::line_strip:
            return vk::PrimitiveTopology::eLineStrip;

        case graphics_primitive_mode::triangles:
            return vk::PrimitiveTopology::eTriangleList;

        case graphics_primitive_mode::triangle_strip:
            return vk::PrimitiveTopology::eTriangleStrip;

        case graphics_primitive_mode::triangle_fan:
            return vk::PrimitiveTopology::eTriangleFan;

        default:
            return vk::PrimitiveTopology::eTriangleList;
        }
    }

    static vk::CullModeFlags to_vulkan_cull_mode(const vulkan_draw_state &state) {
        if (!state.cull_enabled) {
            return vk::CullModeFlagBits::eNone;
        }

        switch (state.cull_face) {
        case rendering_face::front:
            return vk::CullModeFlagBits::eFront;

        case rendering_face::back:
            return vk::CullModeFlagBits::eBack;

        case rendering_face::back_and_front:
            return vk::CullModeFlagBits::eFrontAndBack;

        default:
            return vk::CullModeFlagBits::eBack;
        }
    }

    static vk::FrontFace to_vulkan_front_face(const rendering_face_determine_rule rule) {
        return (rule == rendering_face_determine_rule::vertices_counter_clockwise)
            ? vk::FrontFace::eCounterClockwise
            : vk::FrontFace::eClockwise;
    }

    static bool to_vulkan_index_type(const data_format format, vk::IndexType &index_type) {
        switch (format) {
        case data_format::word:
            index_type = vk::IndexType::eUint16;
            return true;

        case data_format::uint:
            index_type = vk::IndexType::eUint32;
            return true;

        default:
            return false;
        }
    }

    static vk::CompareOp to_vulkan_compare_op(const condition_func func) {
        switch (func) {
        case condition_func::never:
            return vk::CompareOp::eNever;
        case condition_func::less:
            return vk::CompareOp::eLess;
        case condition_func::less_or_equal:
            return vk::CompareOp::eLessOrEqual;
        case condition_func::greater:
            return vk::CompareOp::eGreater;
        case condition_func::greater_or_equal:
            return vk::CompareOp::eGreaterOrEqual;
        case condition_func::equal:
            return vk::CompareOp::eEqual;
        case condition_func::not_equal:
            return vk::CompareOp::eNotEqual;
        case condition_func::always:
        default:
            return vk::CompareOp::eAlways;
        }
    }

    static vk::StencilOp to_vulkan_stencil_op(const stencil_action action) {
        switch (action) {
        case stencil_action::keep:
            return vk::StencilOp::eKeep;
        case stencil_action::replace:
            return vk::StencilOp::eReplace;
        case stencil_action::invert:
            return vk::StencilOp::eInvert;
        case stencil_action::increment:
            return vk::StencilOp::eIncrementAndClamp;
        case stencil_action::increment_wrap:
            return vk::StencilOp::eIncrementAndWrap;
        case stencil_action::decrement:
            return vk::StencilOp::eDecrementAndClamp;
        case stencil_action::decrement_wrap:
            return vk::StencilOp::eDecrementAndWrap;
        case stencil_action::set_to_zero:
        default:
            return vk::StencilOp::eZero;
        }
    }

    static vk::StencilOpState to_vulkan_stencil_state(const vulkan_stencil_face_state &state) {
        vk::StencilOpState result;
        result.failOp = to_vulkan_stencil_op(state.stencil_fail);
        result.passOp = to_vulkan_stencil_op(state.depth_pass);
        result.depthFailOp = to_vulkan_stencil_op(state.depth_fail);
        result.compareOp = to_vulkan_compare_op(state.compare);
        result.compareMask = state.compare_mask;
        result.writeMask = state.write_mask;
        result.reference = static_cast<std::uint32_t>(state.reference);
        return result;
    }

    static vk::Format choose_depth_stencil_format(const vk::PhysicalDevice physical_device) {
        static constexpr vk::Format candidates[] = {
            vk::Format::eD24UnormS8Uint,
            vk::Format::eD32SfloatS8Uint
        };

        for (const vk::Format candidate : candidates) {
            const vk::FormatProperties properties = physical_device.getFormatProperties(candidate);
            if (properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
                return candidate;
            }
        }

        return candidates[0];
    }

    static std::uint32_t pen_style_to_bit_pattern(const pen_style style) {
        switch (style) {
        case pen_style_solid:
            return 0xFFFF;

        case pen_style_dotted:
            return 0x6666;

        case pen_style_dashed:
            return 0x3F3F;

        case pen_style_dashed_dot:
            return 0xFF18;

        case pen_style_dashed_dot_dot:
            return 0x7E66;

        default:
            return 0;
        }
    }

    static constexpr std::uint32_t rectangle_vertex_spirv[] = {
        0x07230203, 0x00010000, 0x000d000b, 0x00000050,
        0x00000000, 0x00020011, 0x00000001, 0x0006000b,
        0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
        0x00000000, 0x0003000e, 0x00000000, 0x00000001,
        0x0008000f, 0x00000000, 0x00000004, 0x6e69616d,
        0x00000000, 0x00000021, 0x00000044, 0x0000004b,
        0x00030047, 0x00000017, 0x00000002, 0x00050048,
        0x00000017, 0x00000000, 0x00000023, 0x00000000,
        0x00050048, 0x00000017, 0x00000001, 0x00000023,
        0x00000010, 0x00050048, 0x00000017, 0x00000002,
        0x00000023, 0x00000020, 0x00040047, 0x00000021,
        0x0000000b, 0x0000002a, 0x00030047, 0x00000042,
        0x00000002, 0x00050048, 0x00000042, 0x00000000,
        0x0000000b, 0x00000000, 0x00050048, 0x00000042,
        0x00000001, 0x0000000b, 0x00000001, 0x00050048,
        0x00000042, 0x00000002, 0x0000000b, 0x00000003,
        0x00050048, 0x00000042, 0x00000003, 0x0000000b,
        0x00000004, 0x00040047, 0x0000004b, 0x0000001e,
        0x00000000, 0x00020013, 0x00000002, 0x00030021,
        0x00000003, 0x00000002, 0x00030016, 0x00000006,
        0x00000020, 0x00040017, 0x00000007, 0x00000006,
        0x00000002, 0x00040015, 0x00000008, 0x00000020,
        0x00000000, 0x0004002b, 0x00000008, 0x00000009,
        0x00000006, 0x0004001c, 0x0000000a, 0x00000007,
        0x00000009, 0x0004002b, 0x00000006, 0x0000000d,
        0x00000000, 0x0005002c, 0x00000007, 0x0000000e,
        0x0000000d, 0x0000000d, 0x0004002b, 0x00000006,
        0x0000000f, 0x3f800000, 0x0005002c, 0x00000007,
        0x00000010, 0x0000000f, 0x0000000d, 0x0005002c,
        0x00000007, 0x00000011, 0x0000000f, 0x0000000f,
        0x0005002c, 0x00000007, 0x00000012, 0x0000000d,
        0x0000000f, 0x0009002c, 0x0000000a, 0x00000013,
        0x0000000e, 0x00000010, 0x00000011, 0x0000000e,
        0x00000011, 0x00000012, 0x00040020, 0x00000014,
        0x00000007, 0x00000007, 0x00040017, 0x00000016,
        0x00000006, 0x00000004, 0x0005001e, 0x00000017,
        0x00000016, 0x00000016, 0x00000016, 0x00040020,
        0x00000018, 0x00000009, 0x00000017, 0x0004003b,
        0x00000018, 0x00000019, 0x00000009, 0x00040015,
        0x0000001a, 0x00000020, 0x00000001, 0x0004002b,
        0x0000001a, 0x0000001b, 0x00000000, 0x00040020,
        0x0000001c, 0x00000009, 0x00000016, 0x00040020,
        0x00000020, 0x00000001, 0x0000001a, 0x0004003b,
        0x00000020, 0x00000021, 0x00000001, 0x0004002b,
        0x00000008, 0x0000002c, 0x00000000, 0x0004002b,
        0x0000001a, 0x00000030, 0x00000002, 0x00040020,
        0x00000031, 0x00000009, 0x00000006, 0x0004002b,
        0x00000006, 0x00000035, 0x40000000, 0x0004002b,
        0x00000008, 0x00000038, 0x00000001, 0x0004001c,
        0x00000041, 0x00000006, 0x00000038, 0x0006001e,
        0x00000042, 0x00000016, 0x00000006, 0x00000041,
        0x00000041, 0x00040020, 0x00000043, 0x00000003,
        0x00000042, 0x0004003b, 0x00000043, 0x00000044,
        0x00000003, 0x00040020, 0x00000049, 0x00000003,
        0x00000016, 0x0004003b, 0x00000049, 0x0000004b,
        0x00000003, 0x0004002b, 0x0000001a, 0x0000004c,
        0x00000001, 0x00040020, 0x0000004f, 0x00000007,
        0x0000000a, 0x00050036, 0x00000002, 0x00000004,
        0x00000000, 0x00000003, 0x000200f8, 0x00000005,
        0x0004003b, 0x0000004f, 0x0000000c, 0x00000007,
        0x0003003e, 0x0000000c, 0x00000013, 0x00050041,
        0x0000001c, 0x0000001d, 0x00000019, 0x0000001b,
        0x0004003d, 0x00000016, 0x0000001e, 0x0000001d,
        0x0007004f, 0x00000007, 0x0000001f, 0x0000001e,
        0x0000001e, 0x00000000, 0x00000001, 0x0004003d,
        0x0000001a, 0x00000022, 0x00000021, 0x00050041,
        0x00000014, 0x00000024, 0x0000000c, 0x00000022,
        0x0004003d, 0x00000007, 0x00000025, 0x00000024,
        0x0007004f, 0x00000007, 0x00000028, 0x0000001e,
        0x0000001e, 0x00000002, 0x00000003, 0x00050085,
        0x00000007, 0x00000029, 0x00000025, 0x00000028,
        0x00050081, 0x00000007, 0x0000002a, 0x0000001f,
        0x00000029, 0x00050051, 0x00000006, 0x0000002f,
        0x0000002a, 0x00000000, 0x00060041, 0x00000031,
        0x00000032, 0x00000019, 0x00000030, 0x0000002c,
        0x0004003d, 0x00000006, 0x00000033, 0x00000032,
        0x00050088, 0x00000006, 0x00000034, 0x0000002f,
        0x00000033, 0x00050085, 0x00000006, 0x00000036,
        0x00000034, 0x00000035, 0x00050083, 0x00000006,
        0x00000037, 0x00000036, 0x0000000f, 0x00050051,
        0x00000006, 0x0000003a, 0x0000002a, 0x00000001,
        0x00060041, 0x00000031, 0x0000003b, 0x00000019,
        0x00000030, 0x00000038, 0x0004003d, 0x00000006,
        0x0000003c, 0x0000003b, 0x00050088, 0x00000006,
        0x0000003d, 0x0000003a, 0x0000003c, 0x00050085,
        0x00000006, 0x0000003e, 0x0000003d, 0x00000035,
        0x00050083, 0x00000006, 0x0000003f, 0x0000000f,
        0x0000003e, 0x00070050, 0x00000016, 0x00000048,
        0x00000037, 0x0000003f, 0x0000000d, 0x0000000f,
        0x00050041, 0x00000049, 0x0000004a, 0x00000044,
        0x0000001b, 0x0003003e, 0x0000004a, 0x00000048,
        0x00050041, 0x0000001c, 0x0000004d, 0x00000019,
        0x0000004c, 0x0004003d, 0x00000016, 0x0000004e,
        0x0000004d, 0x0003003e, 0x0000004b, 0x0000004e,
        0x000100fd, 0x00010038
    };

    static constexpr std::uint32_t rectangle_fragment_spirv[] = {
        0x07230203, 0x00010000, 0x000d000b, 0x0000000d,
        0x00000000, 0x00020011, 0x00000001, 0x0006000b,
        0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
        0x00000000, 0x0003000e, 0x00000000, 0x00000001,
        0x0007000f, 0x00000004, 0x00000004, 0x6e69616d,
        0x00000000, 0x00000009, 0x0000000b, 0x00030010,
        0x00000004, 0x00000007, 0x00040047, 0x00000009,
        0x0000001e, 0x00000000, 0x00040047, 0x0000000b,
        0x0000001e, 0x00000000, 0x00020013, 0x00000002,
        0x00030021, 0x00000003, 0x00000002, 0x00030016,
        0x00000006, 0x00000020, 0x00040017, 0x00000007,
        0x00000006, 0x00000004, 0x00040020, 0x00000008,
        0x00000003, 0x00000007, 0x0004003b, 0x00000008,
        0x00000009, 0x00000003, 0x00040020, 0x0000000a,
        0x00000001, 0x00000007, 0x0004003b, 0x0000000a,
        0x0000000b, 0x00000001, 0x00050036, 0x00000002,
        0x00000004, 0x00000000, 0x00000003, 0x000200f8,
        0x00000005, 0x0004003d, 0x00000007, 0x0000000c,
        0x0000000b, 0x0003003e, 0x00000009, 0x0000000c,
        0x000100fd, 0x00010038
    };

    static constexpr std::uint32_t bitmap_vertex_spirv[] = {
        0x07230203,
        0x00010000,
        0x0008000b,
        0x0000004d,
        0x00000000,
        0x00020011,
        0x00000001,
        0x0006000b,
        0x00000001,
        0x4c534c47,
        0x6474732e,
        0x3035342e,
        0x00000000,
        0x0003000e,
        0x00000000,
        0x00000001,
        0x0008000f,
        0x00000000,
        0x00000004,
        0x6e69616d,
        0x00000000,
        0x00000012,
        0x00000040,
        0x00000048,
        0x00030003,
        0x00000002,
        0x000001c2,
        0x00040005,
        0x00000004,
        0x6e69616d,
        0x00000000,
        0x00040005,
        0x00000008,
        0x65646e69,
        0x00000078,
        0x00060005,
        0x00000012,
        0x565f6c67,
        0x65747265,
        0x646e4978,
        0x00007865,
        0x00050005,
        0x00000015,
        0x65646e69,
        0x6c626178,
        0x00000065,
        0x00030005,
        0x0000001d,
        0x00737675,
        0x00060005,
        0x00000020,
        0x68737550,
        0x736e6f43,
        0x746e6174,
        0x00000073,
        0x00060006,
        0x00000020,
        0x00000000,
        0x69736f70,
        0x6e6f6974,
        0x00000073,
        0x00050006,
        0x00000020,
        0x00000001,
        0x725f7675,
        0x00746365,
        0x00050006,
        0x00000020,
        0x00000002,
        0x6f6c6f63,
        0x00000072,
        0x00050006,
        0x00000020,
        0x00000003,
        0x6974706f,
        0x00736e6f,
        0x00030005,
        0x00000022,
        0x00006370,
        0x00060005,
        0x0000003e,
        0x505f6c67,
        0x65567265,
        0x78657472,
        0x00000000,
        0x00060006,
        0x0000003e,
        0x00000000,
        0x505f6c67,
        0x7469736f,
        0x006e6f69,
        0x00070006,
        0x0000003e,
        0x00000001,
        0x505f6c67,
        0x746e696f,
        0x657a6953,
        0x00000000,
        0x00070006,
        0x0000003e,
        0x00000002,
        0x435f6c67,
        0x4470696c,
        0x61747369,
        0x0065636e,
        0x00070006,
        0x0000003e,
        0x00000003,
        0x435f6c67,
        0x446c6c75,
        0x61747369,
        0x0065636e,
        0x00030005,
        0x00000040,
        0x00000000,
        0x00040005,
        0x00000048,
        0x5f74756f,
        0x00007675,
        0x00040047,
        0x00000012,
        0x0000000b,
        0x0000002a,
        0x00040047,
        0x0000001f,
        0x00000006,
        0x00000010,
        0x00030047,
        0x00000020,
        0x00000002,
        0x00050048,
        0x00000020,
        0x00000000,
        0x00000023,
        0x00000000,
        0x00050048,
        0x00000020,
        0x00000001,
        0x00000023,
        0x00000040,
        0x00050048,
        0x00000020,
        0x00000002,
        0x00000023,
        0x00000050,
        0x00050048,
        0x00000020,
        0x00000003,
        0x00000023,
        0x00000060,
        0x00030047,
        0x0000003e,
        0x00000002,
        0x00050048,
        0x0000003e,
        0x00000000,
        0x0000000b,
        0x00000000,
        0x00050048,
        0x0000003e,
        0x00000001,
        0x0000000b,
        0x00000001,
        0x00050048,
        0x0000003e,
        0x00000002,
        0x0000000b,
        0x00000003,
        0x00050048,
        0x0000003e,
        0x00000003,
        0x0000000b,
        0x00000004,
        0x00040047,
        0x00000048,
        0x0000001e,
        0x00000000,
        0x00020013,
        0x00000002,
        0x00030021,
        0x00000003,
        0x00000002,
        0x00040015,
        0x00000006,
        0x00000020,
        0x00000001,
        0x00040020,
        0x00000007,
        0x00000007,
        0x00000006,
        0x00040015,
        0x00000009,
        0x00000020,
        0x00000000,
        0x0004002b,
        0x00000009,
        0x0000000a,
        0x00000006,
        0x0004001c,
        0x0000000b,
        0x00000006,
        0x0000000a,
        0x0004002b,
        0x00000006,
        0x0000000c,
        0x00000000,
        0x0004002b,
        0x00000006,
        0x0000000d,
        0x00000001,
        0x0004002b,
        0x00000006,
        0x0000000e,
        0x00000002,
        0x0004002b,
        0x00000006,
        0x0000000f,
        0x00000003,
        0x0009002c,
        0x0000000b,
        0x00000010,
        0x0000000c,
        0x0000000d,
        0x0000000e,
        0x0000000c,
        0x0000000e,
        0x0000000f,
        0x00040020,
        0x00000011,
        0x00000001,
        0x00000006,
        0x0004003b,
        0x00000011,
        0x00000012,
        0x00000001,
        0x00040020,
        0x00000014,
        0x00000007,
        0x0000000b,
        0x00030016,
        0x00000018,
        0x00000020,
        0x00040017,
        0x00000019,
        0x00000018,
        0x00000002,
        0x0004002b,
        0x00000009,
        0x0000001a,
        0x00000004,
        0x0004001c,
        0x0000001b,
        0x00000019,
        0x0000001a,
        0x00040020,
        0x0000001c,
        0x00000007,
        0x0000001b,
        0x00040017,
        0x0000001e,
        0x00000018,
        0x00000004,
        0x0004001c,
        0x0000001f,
        0x0000001e,
        0x0000001a,
        0x0006001e,
        0x00000020,
        0x0000001f,
        0x0000001e,
        0x0000001e,
        0x0000001e,
        0x00040020,
        0x00000021,
        0x00000009,
        0x00000020,
        0x0004003b,
        0x00000021,
        0x00000022,
        0x00000009,
        0x0004002b,
        0x00000009,
        0x00000023,
        0x00000000,
        0x00040020,
        0x00000024,
        0x00000009,
        0x00000018,
        0x0004002b,
        0x00000009,
        0x00000027,
        0x00000001,
        0x0004002b,
        0x00000009,
        0x0000002b,
        0x00000002,
        0x0004002b,
        0x00000009,
        0x00000033,
        0x00000003,
        0x0004001c,
        0x0000003d,
        0x00000018,
        0x00000027,
        0x0006001e,
        0x0000003e,
        0x0000001e,
        0x00000018,
        0x0000003d,
        0x0000003d,
        0x00040020,
        0x0000003f,
        0x00000003,
        0x0000003e,
        0x0004003b,
        0x0000003f,
        0x00000040,
        0x00000003,
        0x00040020,
        0x00000042,
        0x00000009,
        0x0000001e,
        0x00040020,
        0x00000045,
        0x00000003,
        0x0000001e,
        0x00040020,
        0x00000047,
        0x00000003,
        0x00000019,
        0x0004003b,
        0x00000047,
        0x00000048,
        0x00000003,
        0x00040020,
        0x0000004a,
        0x00000007,
        0x00000019,
        0x00050036,
        0x00000002,
        0x00000004,
        0x00000000,
        0x00000003,
        0x000200f8,
        0x00000005,
        0x0004003b,
        0x00000007,
        0x00000008,
        0x00000007,
        0x0004003b,
        0x00000014,
        0x00000015,
        0x00000007,
        0x0004003b,
        0x0000001c,
        0x0000001d,
        0x00000007,
        0x0004003d,
        0x00000006,
        0x00000013,
        0x00000012,
        0x0003003e,
        0x00000015,
        0x00000010,
        0x00050041,
        0x00000007,
        0x00000016,
        0x00000015,
        0x00000013,
        0x0004003d,
        0x00000006,
        0x00000017,
        0x00000016,
        0x0003003e,
        0x00000008,
        0x00000017,
        0x00060041,
        0x00000024,
        0x00000025,
        0x00000022,
        0x0000000d,
        0x00000023,
        0x0004003d,
        0x00000018,
        0x00000026,
        0x00000025,
        0x00060041,
        0x00000024,
        0x00000028,
        0x00000022,
        0x0000000d,
        0x00000027,
        0x0004003d,
        0x00000018,
        0x00000029,
        0x00000028,
        0x00050050,
        0x00000019,
        0x0000002a,
        0x00000026,
        0x00000029,
        0x00060041,
        0x00000024,
        0x0000002c,
        0x00000022,
        0x0000000d,
        0x0000002b,
        0x0004003d,
        0x00000018,
        0x0000002d,
        0x0000002c,
        0x00060041,
        0x00000024,
        0x0000002e,
        0x00000022,
        0x0000000d,
        0x00000027,
        0x0004003d,
        0x00000018,
        0x0000002f,
        0x0000002e,
        0x00050050,
        0x00000019,
        0x00000030,
        0x0000002d,
        0x0000002f,
        0x00060041,
        0x00000024,
        0x00000031,
        0x00000022,
        0x0000000d,
        0x0000002b,
        0x0004003d,
        0x00000018,
        0x00000032,
        0x00000031,
        0x00060041,
        0x00000024,
        0x00000034,
        0x00000022,
        0x0000000d,
        0x00000033,
        0x0004003d,
        0x00000018,
        0x00000035,
        0x00000034,
        0x00050050,
        0x00000019,
        0x00000036,
        0x00000032,
        0x00000035,
        0x00060041,
        0x00000024,
        0x00000037,
        0x00000022,
        0x0000000d,
        0x00000023,
        0x0004003d,
        0x00000018,
        0x00000038,
        0x00000037,
        0x00060041,
        0x00000024,
        0x00000039,
        0x00000022,
        0x0000000d,
        0x00000033,
        0x0004003d,
        0x00000018,
        0x0000003a,
        0x00000039,
        0x00050050,
        0x00000019,
        0x0000003b,
        0x00000038,
        0x0000003a,
        0x00070050,
        0x0000001b,
        0x0000003c,
        0x0000002a,
        0x00000030,
        0x00000036,
        0x0000003b,
        0x0003003e,
        0x0000001d,
        0x0000003c,
        0x0004003d,
        0x00000006,
        0x00000041,
        0x00000008,
        0x00060041,
        0x00000042,
        0x00000043,
        0x00000022,
        0x0000000c,
        0x00000041,
        0x0004003d,
        0x0000001e,
        0x00000044,
        0x00000043,
        0x00050041,
        0x00000045,
        0x00000046,
        0x00000040,
        0x0000000c,
        0x0003003e,
        0x00000046,
        0x00000044,
        0x0004003d,
        0x00000006,
        0x00000049,
        0x00000008,
        0x00050041,
        0x0000004a,
        0x0000004b,
        0x0000001d,
        0x00000049,
        0x0004003d,
        0x00000019,
        0x0000004c,
        0x0000004b,
        0x0003003e,
        0x00000048,
        0x0000004c,
        0x000100fd,
        0x00010038,
    };

    static constexpr std::uint32_t bitmap_fragment_spirv[] = {
        0x07230203,
        0x00010000,
        0x0008000b,
        0x0000004e,
        0x00000000,
        0x00020011,
        0x00000001,
        0x0006000b,
        0x00000001,
        0x4c534c47,
        0x6474732e,
        0x3035342e,
        0x00000000,
        0x0003000e,
        0x00000000,
        0x00000001,
        0x0007000f,
        0x00000004,
        0x00000004,
        0x6e69616d,
        0x00000000,
        0x00000011,
        0x0000004c,
        0x00030010,
        0x00000004,
        0x00000007,
        0x00030003,
        0x00000002,
        0x000001c2,
        0x00040005,
        0x00000004,
        0x6e69616d,
        0x00000000,
        0x00060005,
        0x00000009,
        0x6f6c6f63,
        0x726f5f72,
        0x6e696769,
        0x00006c61,
        0x00060005,
        0x0000000d,
        0x72756f73,
        0x745f6563,
        0x75747865,
        0x00006572,
        0x00040005,
        0x00000011,
        0x755f6e69,
        0x00000076,
        0x00060005,
        0x00000017,
        0x68737550,
        0x736e6f43,
        0x746e6174,
        0x00000073,
        0x00060006,
        0x00000017,
        0x00000000,
        0x69736f70,
        0x6e6f6974,
        0x00000073,
        0x00050006,
        0x00000017,
        0x00000001,
        0x725f7675,
        0x00746365,
        0x00050006,
        0x00000017,
        0x00000002,
        0x6f6c6f63,
        0x00000072,
        0x00050006,
        0x00000017,
        0x00000003,
        0x6974706f,
        0x00736e6f,
        0x00030005,
        0x00000019,
        0x00006370,
        0x00050005,
        0x0000002d,
        0x6b73616d,
        0x6c61765f,
        0x00006575,
        0x00060005,
        0x0000002e,
        0x6b73616d,
        0x7865745f,
        0x65727574,
        0x00000000,
        0x00050005,
        0x0000004c,
        0x5f74756f,
        0x6f6c6f63,
        0x00000072,
        0x00040047,
        0x0000000d,
        0x00000021,
        0x00000000,
        0x00040047,
        0x0000000d,
        0x00000022,
        0x00000000,
        0x00040047,
        0x00000011,
        0x0000001e,
        0x00000000,
        0x00040047,
        0x00000016,
        0x00000006,
        0x00000010,
        0x00030047,
        0x00000017,
        0x00000002,
        0x00050048,
        0x00000017,
        0x00000000,
        0x00000023,
        0x00000000,
        0x00050048,
        0x00000017,
        0x00000001,
        0x00000023,
        0x00000040,
        0x00050048,
        0x00000017,
        0x00000002,
        0x00000023,
        0x00000050,
        0x00050048,
        0x00000017,
        0x00000003,
        0x00000023,
        0x00000060,
        0x00040047,
        0x0000002e,
        0x00000021,
        0x00000000,
        0x00040047,
        0x0000002e,
        0x00000022,
        0x00000001,
        0x00040047,
        0x0000004c,
        0x0000001e,
        0x00000000,
        0x00020013,
        0x00000002,
        0x00030021,
        0x00000003,
        0x00000002,
        0x00030016,
        0x00000006,
        0x00000020,
        0x00040017,
        0x00000007,
        0x00000006,
        0x00000004,
        0x00040020,
        0x00000008,
        0x00000007,
        0x00000007,
        0x00090019,
        0x0000000a,
        0x00000006,
        0x00000001,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000001,
        0x00000000,
        0x0003001b,
        0x0000000b,
        0x0000000a,
        0x00040020,
        0x0000000c,
        0x00000000,
        0x0000000b,
        0x0004003b,
        0x0000000c,
        0x0000000d,
        0x00000000,
        0x00040017,
        0x0000000f,
        0x00000006,
        0x00000002,
        0x00040020,
        0x00000010,
        0x00000001,
        0x0000000f,
        0x0004003b,
        0x00000010,
        0x00000011,
        0x00000001,
        0x00040015,
        0x00000014,
        0x00000020,
        0x00000000,
        0x0004002b,
        0x00000014,
        0x00000015,
        0x00000004,
        0x0004001c,
        0x00000016,
        0x00000007,
        0x00000015,
        0x0006001e,
        0x00000017,
        0x00000016,
        0x00000007,
        0x00000007,
        0x00000007,
        0x00040020,
        0x00000018,
        0x00000009,
        0x00000017,
        0x0004003b,
        0x00000018,
        0x00000019,
        0x00000009,
        0x00040015,
        0x0000001a,
        0x00000020,
        0x00000001,
        0x0004002b,
        0x0000001a,
        0x0000001b,
        0x00000002,
        0x00040020,
        0x0000001c,
        0x00000009,
        0x00000007,
        0x0004002b,
        0x00000006,
        0x0000001f,
        0x437f0000,
        0x0004002b,
        0x0000001a,
        0x00000023,
        0x00000003,
        0x0004002b,
        0x00000014,
        0x00000024,
        0x00000000,
        0x00040020,
        0x00000025,
        0x00000009,
        0x00000006,
        0x0004002b,
        0x00000006,
        0x00000028,
        0x3f000000,
        0x00020014,
        0x00000029,
        0x0004003b,
        0x0000000c,
        0x0000002e,
        0x00000000,
        0x0004002b,
        0x00000006,
        0x00000033,
        0x3f800000,
        0x0007002c,
        0x00000007,
        0x00000034,
        0x00000033,
        0x00000033,
        0x00000033,
        0x00000033,
        0x0004002b,
        0x00000014,
        0x00000037,
        0x00000001,
        0x00040020,
        0x0000003c,
        0x00000007,
        0x00000006,
        0x0004002b,
        0x00000006,
        0x00000041,
        0x00000000,
        0x0004002b,
        0x00000014,
        0x00000044,
        0x00000002,
        0x0004002b,
        0x00000014,
        0x00000049,
        0x00000003,
        0x00040020,
        0x0000004b,
        0x00000003,
        0x00000007,
        0x0004003b,
        0x0000004b,
        0x0000004c,
        0x00000003,
        0x00050036,
        0x00000002,
        0x00000004,
        0x00000000,
        0x00000003,
        0x000200f8,
        0x00000005,
        0x0004003b,
        0x00000008,
        0x00000009,
        0x00000007,
        0x0004003b,
        0x00000008,
        0x0000002d,
        0x00000007,
        0x0004003d,
        0x0000000b,
        0x0000000e,
        0x0000000d,
        0x0004003d,
        0x0000000f,
        0x00000012,
        0x00000011,
        0x00050057,
        0x00000007,
        0x00000013,
        0x0000000e,
        0x00000012,
        0x00050041,
        0x0000001c,
        0x0000001d,
        0x00000019,
        0x0000001b,
        0x0004003d,
        0x00000007,
        0x0000001e,
        0x0000001d,
        0x00070050,
        0x00000007,
        0x00000020,
        0x0000001f,
        0x0000001f,
        0x0000001f,
        0x0000001f,
        0x00050088,
        0x00000007,
        0x00000021,
        0x0000001e,
        0x00000020,
        0x00050085,
        0x00000007,
        0x00000022,
        0x00000013,
        0x00000021,
        0x0003003e,
        0x00000009,
        0x00000022,
        0x00060041,
        0x00000025,
        0x00000026,
        0x00000019,
        0x00000023,
        0x00000024,
        0x0004003d,
        0x00000006,
        0x00000027,
        0x00000026,
        0x000500ba,
        0x00000029,
        0x0000002a,
        0x00000027,
        0x00000028,
        0x000300f7,
        0x0000002c,
        0x00000000,
        0x000400fa,
        0x0000002a,
        0x0000002b,
        0x0000002c,
        0x000200f8,
        0x0000002b,
        0x0004003d,
        0x0000000b,
        0x0000002f,
        0x0000002e,
        0x0004003d,
        0x0000000f,
        0x00000030,
        0x00000011,
        0x00050057,
        0x00000007,
        0x00000031,
        0x0000002f,
        0x00000030,
        0x0003003e,
        0x0000002d,
        0x00000031,
        0x0004003d,
        0x00000007,
        0x00000032,
        0x0000002d,
        0x0004003d,
        0x00000007,
        0x00000035,
        0x0000002d,
        0x00050083,
        0x00000007,
        0x00000036,
        0x00000034,
        0x00000035,
        0x00060041,
        0x00000025,
        0x00000038,
        0x00000019,
        0x00000023,
        0x00000037,
        0x0004003d,
        0x00000006,
        0x00000039,
        0x00000038,
        0x00070050,
        0x00000007,
        0x0000003a,
        0x00000039,
        0x00000039,
        0x00000039,
        0x00000039,
        0x0008000c,
        0x00000007,
        0x0000003b,
        0x00000001,
        0x0000002e,
        0x00000032,
        0x00000036,
        0x0000003a,
        0x0003003e,
        0x0000002d,
        0x0000003b,
        0x00050041,
        0x0000003c,
        0x0000003d,
        0x0000002d,
        0x00000024,
        0x0004003d,
        0x00000006,
        0x0000003e,
        0x0000003d,
        0x00050041,
        0x0000003c,
        0x0000003f,
        0x0000002d,
        0x00000024,
        0x0004003d,
        0x00000006,
        0x00000040,
        0x0000003f,
        0x0007000c,
        0x00000006,
        0x00000042,
        0x00000001,
        0x00000030,
        0x00000040,
        0x00000041,
        0x00050083,
        0x00000006,
        0x00000043,
        0x00000033,
        0x00000042,
        0x00060041,
        0x00000025,
        0x00000045,
        0x00000019,
        0x00000023,
        0x00000044,
        0x0004003d,
        0x00000006,
        0x00000046,
        0x00000045,
        0x0007000c,
        0x00000006,
        0x00000047,
        0x00000001,
        0x00000030,
        0x00000033,
        0x00000046,
        0x0008000c,
        0x00000006,
        0x00000048,
        0x00000001,
        0x0000002e,
        0x0000003e,
        0x00000043,
        0x00000047,
        0x00050041,
        0x0000003c,
        0x0000004a,
        0x00000009,
        0x00000049,
        0x0003003e,
        0x0000004a,
        0x00000048,
        0x000200f9,
        0x0000002c,
        0x000200f8,
        0x0000002c,
        0x0004003d,
        0x00000007,
        0x0000004d,
        0x00000009,
        0x0003003e,
        0x0000004c,
        0x0000004d,
        0x000100fd,
        0x00010038,
    };

#if EKA2L1_PLATFORM(MACOS)
    static void set_bundled_moltenvk_icd_path() {
        if (std::getenv("VK_ICD_FILENAMES")) {
            return;
        }

        std::uint32_t path_size = 0;
        _NSGetExecutablePath(nullptr, &path_size);
        if (path_size == 0) {
            return;
        }

        std::vector<char> executable_path(path_size);
        if (_NSGetExecutablePath(executable_path.data(), &path_size) != 0) {
            return;
        }

        const std::filesystem::path exe_path = std::filesystem::weakly_canonical(executable_path.data());
        const std::filesystem::path icd_path = exe_path.parent_path().parent_path() / "Resources" / "vulkan" / "icd.d" / "MoltenVK_icd.json";
        if (std::filesystem::exists(icd_path)) {
            setenv("VK_ICD_FILENAMES", icd_path.string().c_str(), 0);
        }
    }
#endif

    static std::string trim_copy(const std::string &value) {
        const std::size_t begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) {
            return "";
        }

        const std::size_t end = value.find_last_not_of(" \t\r\n");
        return value.substr(begin, end - begin + 1);
    }

    static bool read_text_file(const std::filesystem::path &path, std::string &contents) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return false;
        }

        std::ostringstream stream;
        stream << input.rdbuf();
        contents = stream.str();
        return true;
    }

    static std::string transform_upscale_fragment_shader_for_vulkan(const std::string &source) {
        std::ostringstream transformed;
        transformed
            << "#version 450\n"
            << "layout(push_constant) uniform PushConstants {\n"
            << "    vec4 positions[4];\n"
            << "    vec4 uv_rect;\n"
            << "    vec4 color;\n"
            << "    vec4 options;\n"
            << "} pc;\n"
            << "#define u_texelDelta pc.options.xy\n"
            << "#define u_pixelDelta pc.options.zw\n";

        std::istringstream input(source);
        std::string line;
        while (std::getline(input, line)) {
            const std::string trimmed = trim_copy(line);
            if (trimmed.rfind("#version", 0) == 0) {
                continue;
            }

            if ((trimmed == "uniform sampler2D u_tex;") || (trimmed == "uniform sampler2D sampler0;")) {
                const char *sampler_name = (trimmed.find("sampler0") != std::string::npos) ? "sampler0" : "u_tex";
                transformed << "layout(set = 0, binding = 0) uniform sampler2D " << sampler_name << ";\n";
                continue;
            }

            if ((trimmed == "uniform vec2 u_texelDelta;") || (trimmed == "uniform vec2 u_pixelDelta;")) {
                continue;
            }

            if (trimmed == "in vec2 r_texcoord;") {
                transformed << "layout(location = 0) in vec2 r_texcoord;\n";
                continue;
            }

            if (trimmed == "out vec4 o_color;") {
                transformed << "layout(location = 0) out vec4 o_color;\n";
                continue;
            }

            transformed << line << "\n";
        }

        return transformed.str();
    }

#if EKA2L1_VULKAN_USE_SHADERC
    static bool compile_upscale_fragment_shader(vk::Device device, const std::string &shader_name,
        vk::UniqueShaderModule &module) {
        const std::filesystem::path shader_path = std::filesystem::path("resources") / "upscale" / (shader_name + ".frag");

        std::string source;
        if (!read_text_file(shader_path, source)) {
            LOG_WARN(DRIVER_GRAPHICS, "Unable to load Vulkan upscale shader {}", shader_path.string());
            return false;
        }

        const std::string transformed_source = transform_upscale_fragment_shader_for_vulkan(source);
        shaderc_compiler_t compiler = shaderc_compiler_initialize();
        shaderc_compile_options_t options = shaderc_compile_options_initialize();
        if (!compiler || !options) {
            LOG_WARN(DRIVER_GRAPHICS, "Unable to initialize shaderc for Vulkan upscale shader {}", shader_name);
            if (options) {
                shaderc_compile_options_release(options);
            }
            if (compiler) {
                shaderc_compiler_release(compiler);
            }
            return false;
        }

        shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
        shaderc_compile_options_set_source_language(options, shaderc_source_language_glsl);

        shaderc_compilation_result_t result = shaderc_compile_into_spv(
            compiler,
            transformed_source.data(),
            transformed_source.size(),
            shaderc_fragment_shader,
            shader_path.string().c_str(),
            "main",
            options);

        const shaderc_compilation_status status = result
            ? shaderc_result_get_compilation_status(result)
            : shaderc_compilation_status_internal_error;
        if (status != shaderc_compilation_status_success) {
            LOG_WARN(DRIVER_GRAPHICS,
                "Failed to compile Vulkan upscale shader {}: {}",
                shader_name,
                result ? shaderc_result_get_error_message(result) : "shaderc returned no result");
            if (result) {
                shaderc_result_release(result);
            }
            shaderc_compile_options_release(options);
            shaderc_compiler_release(compiler);
            return false;
        }

        try {
            vk::ShaderModuleCreateInfo create_info(
                vk::ShaderModuleCreateFlags{},
                shaderc_result_get_length(result),
                reinterpret_cast<const std::uint32_t *>(shaderc_result_get_bytes(result)));
            module = device.createShaderModuleUnique(create_info);
        } catch (std::exception &e) {
            LOG_WARN(DRIVER_GRAPHICS, "Failed to create Vulkan upscale shader module {}: {}", shader_name, e.what());
            shaderc_result_release(result);
            shaderc_compile_options_release(options);
            shaderc_compiler_release(compiler);
            return false;
        }

        shaderc_result_release(result);
        shaderc_compile_options_release(options);
        shaderc_compiler_release(compiler);
        return true;
    }
#else
    static bool compile_upscale_fragment_shader(vk::Device, const std::string &shader_name,
        vk::UniqueShaderModule &) {
        LOG_WARN(DRIVER_GRAPHICS, "Vulkan upscale shader {} requires shaderc support", shader_name);
        return false;
    }
#endif

    static bool has_extension(const std::vector<vk::ExtensionProperties> &extensions, const char *name) {
        return std::any_of(extensions.begin(), extensions.end(), [name](const vk::ExtensionProperties &extension) {
            return std::strcmp(extension.extensionName, name) == 0;
        });
    }

    static vk::SurfaceFormatKHR choose_surface_format(const std::vector<vk::SurfaceFormatKHR> &formats) {
        if (formats.empty()) {
            return {};
        }

        if ((formats.size() == 1) && (formats[0].format == vk::Format::eUndefined)) {
            return vk::SurfaceFormatKHR(vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear);
        }

        static constexpr vk::Format preferred_formats[] = {
            vk::Format::eB8G8R8A8Unorm,
            vk::Format::eR8G8B8A8Unorm,
            vk::Format::eB8G8R8A8Srgb,
            vk::Format::eR8G8B8A8Srgb
        };

        for (const vk::Format preferred_format : preferred_formats) {
            const auto preferred = std::find_if(formats.begin(), formats.end(), [preferred_format](const vk::SurfaceFormatKHR &format) {
                return (format.format == preferred_format) && (format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear);
            });

            if (preferred != formats.end()) {
                return *preferred;
            }
        }

        return formats[0];
    }

    static vk::PresentModeKHR choose_present_mode(const std::vector<vk::PresentModeKHR> &present_modes) {
        if (std::find(present_modes.begin(), present_modes.end(), vk::PresentModeKHR::eMailbox) != present_modes.end()) {
            return vk::PresentModeKHR::eMailbox;
        }

        return vk::PresentModeKHR::eFifo;
    }

    static vk::Extent2D choose_swapchain_extent(const vk::SurfaceCapabilitiesKHR &caps, std::uint32_t width, std::uint32_t height) {
        if (caps.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
            return caps.currentExtent;
        }

        width = std::max<std::uint32_t>(1, width);
        height = std::max<std::uint32_t>(1, height);

        return vk::Extent2D(
            std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width),
            std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height));
    }

    static vk::CompositeAlphaFlagBitsKHR choose_composite_alpha(const vk::CompositeAlphaFlagsKHR supported) {
        static constexpr vk::CompositeAlphaFlagBitsKHR preferred_modes[] = {
            vk::CompositeAlphaFlagBitsKHR::eOpaque,
            vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
            vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
            vk::CompositeAlphaFlagBitsKHR::eInherit
        };

        for (const vk::CompositeAlphaFlagBitsKHR mode : preferred_modes) {
            if (supported & mode) {
                return mode;
            }
        }

        return vk::CompositeAlphaFlagBitsKHR::eOpaque;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_reporter(vk::DebugReportFlagsEXT flags, vk::DebugReportObjectTypeEXT /*objectType*/, uint64_t /*object*/, size_t /*location*/,
        int32_t /*messageCode*/, const char * /*pLayerPrefix*/, const char *pMessage, void * /*pUserData*/) {
        if (flags & vk::DebugReportFlagBitsEXT::eInformation) {
            LOG_INFO(DRIVER_GRAPHICS, "{}", pMessage);
        } else if (flags & vk::DebugReportFlagBitsEXT::eWarning) {
            LOG_WARN(DRIVER_GRAPHICS, "{}", pMessage);
        } else if (flags & vk::DebugReportFlagBitsEXT::ePerformanceWarning) {
            LOG_WARN(DRIVER_GRAPHICS, "Performance: {}", pMessage);
        } else if (flags & vk::DebugReportFlagBitsEXT::eError) {
            if (pMessage && std::strstr(pMessage, "Using VkFormat") && std::strstr(pMessage, "instead")) {
                LOG_WARN(DRIVER_GRAPHICS, "{}", pMessage);
            } else {
                LOG_ERROR(DRIVER_GRAPHICS, "{}", pMessage);
            }
        } else if (flags & vk::DebugReportFlagBitsEXT::eDebug) {
            LOG_TRACE(DRIVER_GRAPHICS, "{}", pMessage);
        } else {
            LOG_INFO(DRIVER_GRAPHICS, "{}", pMessage);
        }

        return false;
    }

    static vk::AccessFlags access_for_layout(const vk::ImageLayout layout) {
        switch (layout) {
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            return vk::AccessFlagBits::eShaderRead;

        case vk::ImageLayout::eColorAttachmentOptimal:
            return vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

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

        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            return vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;

        case vk::ImageLayout::eTransferDstOptimal:
        case vk::ImageLayout::eTransferSrcOptimal:
            return vk::PipelineStageFlagBits::eTransfer;

        case vk::ImageLayout::eUndefined:
            return vk::PipelineStageFlagBits::eTopOfPipe;

        default:
            return vk::PipelineStageFlagBits::eAllCommands;
        }
    }

    static bool is_vulkan_depth_stencil_format(const vk::Format format) {
        switch (format) {
        case vk::Format::eD16UnormS8Uint:
        case vk::Format::eD24UnormS8Uint:
        case vk::Format::eD32SfloatS8Uint:
            return true;

        default:
            return false;
        }
    }

    static vk::ImageAspectFlags aspect_for_format(const vk::Format format) {
        return is_vulkan_depth_stencil_format(format)
            ? (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil)
            : vk::ImageAspectFlagBits::eColor;
    }

    static void transition_texture(vk::CommandBuffer command_buffer, vulkan_texture *texture,
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
        barrier.subresourceRange = vk::ImageSubresourceRange(aspect_for_format(texture->vk_format()), 0, 1, 0, 1);

        command_buffer.pipelineBarrier(
            stage_for_layout(texture->layout()),
            dst_stage,
            vk::DependencyFlags{},
            nullptr,
            nullptr,
            barrier);
        texture->set_layout(new_layout);
    }

    void vulkan_graphics_driver::configure_debug_options() {
        const std::string capture_modes = env_string_value("EKA2L1_VULKAN_DEBUG_CAPTURE", "");
        const bool capture_requested_as_bool = env_flag_enabled("EKA2L1_VULKAN_DEBUG_CAPTURE");
        const bool capture_names_present = capture_mode_contains(capture_modes, "present") || capture_mode_contains(capture_modes, "swapchain") || capture_mode_contains(capture_modes, "frame") || capture_mode_contains(capture_modes, "offscreen") || capture_mode_contains(capture_modes, "target") || capture_mode_contains(capture_modes, "all");
        const bool capture_all = capture_requested_as_bool && !capture_names_present;

        debug_options_.trace_draws = env_flag_enabled("EKA2L1_VULKAN_DEBUG_TRACE") || env_flag_enabled("EKA2L1_VULKAN_DEBUG_DRAWS");
        debug_options_.capture_present = env_flag_enabled("EKA2L1_VULKAN_DEBUG_CAPTURE_PRESENT") || capture_mode_contains(capture_modes, "present") || capture_mode_contains(capture_modes, "swapchain") || capture_mode_contains(capture_modes, "frame") || capture_all;
        debug_options_.capture_offscreen = env_flag_enabled("EKA2L1_VULKAN_DEBUG_CAPTURE_OFFSCREEN") || capture_mode_contains(capture_modes, "offscreen") || capture_mode_contains(capture_modes, "target") || capture_all;
        debug_options_.enabled = env_flag_enabled("EKA2L1_VULKAN_DEBUG") || debug_options_.trace_draws || debug_options_.capture_present || debug_options_.capture_offscreen;

        debug_options_.present_frame_limit = env_u32_value("EKA2L1_VULKAN_DEBUG_PRESENT_FRAMES",
            env_u32_value("EKA2L1_VULKAN_DEBUG_FRAMES", debug_options_.capture_present ? 60 : 0));
        debug_options_.offscreen_capture_limit = env_u32_value("EKA2L1_VULKAN_DEBUG_OFFSCREEN_FRAMES",
            debug_options_.capture_offscreen ? 120 : 0);
        debug_options_.present_frame_skip = env_u32_value("EKA2L1_VULKAN_DEBUG_PRESENT_SKIP",
            env_u32_value("EKA2L1_VULKAN_DEBUG_SKIP_FRAMES", 0));
        debug_options_.present_frame_interval = std::max<std::uint32_t>(1, env_u32_value("EKA2L1_VULKAN_DEBUG_PRESENT_INTERVAL", env_u32_value("EKA2L1_VULKAN_DEBUG_FRAME_INTERVAL", 1)));
        debug_options_.offscreen_capture_skip = env_u32_value("EKA2L1_VULKAN_DEBUG_OFFSCREEN_SKIP", 0);
        debug_options_.offscreen_capture_interval = std::max<std::uint32_t>(1,
            env_u32_value("EKA2L1_VULKAN_DEBUG_OFFSCREEN_INTERVAL", 1));

        std::filesystem::path default_output_dir;
        try {
            default_output_dir = std::filesystem::temp_directory_path() / "eka2l1-vulkan-debug";
        } catch (const std::exception &) {
            default_output_dir = std::filesystem::path("eka2l1-vulkan-debug");
        }

        debug_options_.output_directory = env_string_value("EKA2L1_VULKAN_DEBUG_DIR", default_output_dir.string());

        if (debug_options_.capture_present || debug_options_.capture_offscreen) {
            try {
                std::filesystem::create_directories(debug_options_.output_directory);
            } catch (const std::exception &e) {
                LOG_WARN(DRIVER_GRAPHICS, "Disabling Vulkan debug captures because output directory {} can not be created: {}",
                    debug_options_.output_directory, e.what());
                debug_options_.capture_present = false;
                debug_options_.capture_offscreen = false;
            }
        }

        if (debug_options_.enabled) {
            LOG_INFO(DRIVER_GRAPHICS,
                "Vulkan debug enabled: trace_draws={}, capture_present={} ({} frames, skip={}, interval={}), capture_offscreen={} ({} targets, skip={}, interval={}), output={}",
                debug_options_.trace_draws,
                debug_options_.capture_present,
                debug_options_.present_frame_limit,
                debug_options_.present_frame_skip,
                debug_options_.present_frame_interval,
                debug_options_.capture_offscreen,
                debug_options_.offscreen_capture_limit,
                debug_options_.offscreen_capture_skip,
                debug_options_.offscreen_capture_interval,
                debug_options_.output_directory);
        }
    }

    bool vulkan_graphics_driver::create_debug_readback_buffer(const vk::DeviceSize size, vk::UniqueBuffer &buffer,
        vk::UniqueDeviceMemory &memory) {
        if ((size == 0) || !dvc_) {
            return false;
        }

        try {
            vk::BufferCreateInfo buffer_create_info(
                vk::BufferCreateFlags{},
                size,
                vk::BufferUsageFlagBits::eTransferDst,
                vk::SharingMode::eExclusive);
            buffer = dvc_->createBufferUnique(buffer_create_info);

            const vk::MemoryRequirements memory_requirements = dvc_->getBufferMemoryRequirements(buffer.get());
            vk::MemoryAllocateInfo allocate_info(
                memory_requirements.size,
                find_memory_type(memory_requirements.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
            memory = dvc_->allocateMemoryUnique(allocate_info);
            dvc_->bindBufferMemory(buffer.get(), memory.get(), 0);
            return true;
        } catch (const std::exception &e) {
            LOG_WARN(DRIVER_GRAPHICS, "Vulkan debug readback buffer creation failed: {}", e.what());
            return false;
        }
    }

    bool vulkan_graphics_driver::debug_capture_texture(vulkan_texture *texture, const std::string &label,
        const std::uint64_t index) {
        if (!texture || !texture->image()) {
            return false;
        }

        bool unused_bgra_source = false;
        if (!is_supported_debug_capture_format(texture->vk_format(), unused_bgra_source)) {
            LOG_WARN(DRIVER_GRAPHICS, "Skipping Vulkan debug texture capture for unsupported format {}",
                vk::to_string(texture->vk_format()));
            return false;
        }

        const eka2l1::vec2 texture_size = texture->get_size();
        if ((texture_size.x <= 0) || (texture_size.y <= 0)) {
            return false;
        }

        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(texture_size.x) * texture_size.y * 4);
        if (!texture->read_data(this, texture_format::rgba, texture_data_type::ubyte,
                eka2l1::point(0, 0), eka2l1::object_size(texture_size.x, texture_size.y), rgba.data())) {
            LOG_WARN(DRIVER_GRAPHICS, "Failed to read Vulkan debug texture capture {}", label);
            return false;
        }

        const std::filesystem::path output_path = std::filesystem::path(debug_options_.output_directory) / debug_capture_name(label, index);
        if (!write_rgba_ppm(output_path, rgba.data(), static_cast<std::uint32_t>(texture_size.x),
                static_cast<std::uint32_t>(texture_size.y), false)) {
            LOG_WARN(DRIVER_GRAPHICS, "Failed to write Vulkan debug texture capture {}", output_path.string());
            return false;
        }

        LOG_INFO(DRIVER_GRAPHICS, "Wrote Vulkan debug texture capture {}", output_path.string());
        return true;
    }

    bool vulkan_graphics_driver::debug_record_swapchain_capture(vk::CommandBuffer command_buffer, vk::Image image,
        vk::Buffer readback_buffer, const vk::Extent2D &extent) {
        if (!command_buffer || !image || !readback_buffer || (extent.width == 0) || (extent.height == 0)) {
            return false;
        }

        const vk::ImageSubresourceRange color_range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

        vk::ImageMemoryBarrier to_transfer;
        to_transfer.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        to_transfer.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        to_transfer.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
        to_transfer.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.image = image;
        to_transfer.subresourceRange = color_range;

        command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
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
        copy_region.imageOffset = vk::Offset3D(0, 0, 0);
        copy_region.imageExtent = vk::Extent3D(extent.width, extent.height, 1);
        command_buffer.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, readback_buffer, copy_region);

        vk::BufferMemoryBarrier buffer_to_host;
        buffer_to_host.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        buffer_to_host.dstAccessMask = vk::AccessFlagBits::eHostRead;
        buffer_to_host.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        buffer_to_host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        buffer_to_host.buffer = readback_buffer;
        buffer_to_host.offset = 0;
        buffer_to_host.size = static_cast<vk::DeviceSize>(extent.width) * extent.height * 4;

        command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eHost,
            vk::DependencyFlags{},
            nullptr,
            buffer_to_host,
            nullptr);

        vk::ImageMemoryBarrier to_present;
        to_present.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        to_present.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        to_present.newLayout = vk::ImageLayout::ePresentSrcKHR;
        to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present.image = image;
        to_present.subresourceRange = color_range;

        command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eBottomOfPipe,
            vk::DependencyFlags{},
            nullptr,
            nullptr,
            to_present);
        return true;
    }

    bool vulkan_graphics_driver::debug_write_swapchain_capture(vk::DeviceMemory readback_memory, const vk::Extent2D &extent,
        const std::string &label, const std::uint64_t index) {
        if (!readback_memory || (extent.width == 0) || (extent.height == 0)) {
            return false;
        }

        bool bgra_source = false;
        if (!is_supported_debug_capture_format(swapchain_format_, bgra_source)) {
            LOG_WARN(DRIVER_GRAPHICS, "Skipping Vulkan swapchain debug capture for unsupported format {}",
                vk::to_string(swapchain_format_));
            return false;
        }

        const vk::DeviceSize capture_size = static_cast<vk::DeviceSize>(extent.width) * extent.height * 4;
        try {
            void *mapped = dvc_->mapMemory(readback_memory, 0, capture_size);
            const std::filesystem::path output_path = std::filesystem::path(debug_options_.output_directory) / debug_capture_name(label, index);
            const bool written = write_rgba_ppm(output_path, reinterpret_cast<const std::uint8_t *>(mapped),
                extent.width, extent.height, bgra_source);
            dvc_->unmapMemory(readback_memory);

            if (!written) {
                LOG_WARN(DRIVER_GRAPHICS, "Failed to write Vulkan swapchain debug capture {}", output_path.string());
                return false;
            }

            LOG_INFO(DRIVER_GRAPHICS, "Wrote Vulkan swapchain debug capture {}", output_path.string());
            return true;
        } catch (const std::exception &e) {
            LOG_WARN(DRIVER_GRAPHICS, "Failed to map Vulkan swapchain debug capture: {}", e.what());
            return false;
        }
    }

    void vulkan_graphics_driver::debug_log_pending_draws(vulkan_framebuffer *target, const char *stage) const {
        if (!debug_options_.trace_draws) {
            return;
        }

        std::uint32_t rectangle_count = 0;
        std::uint32_t bitmap_count = 0;
        std::uint32_t advanced_count = 0;
        std::uint32_t clipped_count = 0;

        for (const vulkan_pending_draw &draw : pending_draws_) {
            if (pending_draw_target(draw) != target) {
                continue;
            }

            std::visit([&](const auto &entry) {
                using draw_type = std::decay_t<decltype(entry)>;
                if constexpr (std::is_same_v<draw_type, vulkan_rectangle_draw>) {
                    rectangle_count++;
                } else if constexpr (std::is_same_v<draw_type, vulkan_bitmap_draw>) {
                    bitmap_count++;
                } else if constexpr (std::is_same_v<draw_type, vulkan_advanced_draw>) {
                    advanced_count++;
                }

                if (entry.state.clipping_enabled) {
                    clipped_count++;
                }
            },
                draw);
        }

        const std::uint32_t total_count = rectangle_count + bitmap_count + advanced_count;
        if (total_count == 0) {
            return;
        }

        std::string target_label = "swapchain";
        if (target) {
            std::ostringstream out;
            out << "framebuffer@" << reinterpret_cast<std::uintptr_t>(target);
            if (const vulkan_texture *texture = target->draw_texture()) {
                const eka2l1::vec2 size = texture->get_size();
                out << " " << size.x << "x" << size.y;
            }
            target_label = out.str();
        }

        LOG_INFO(DRIVER_GRAPHICS,
            "Vulkan debug draw batch [{}] target={} total={} rectangles={} bitmaps={} advanced={} clipped={}",
            stage ? stage : "unknown",
            target_label,
            total_count,
            rectangle_count,
            bitmap_count,
            advanced_count,
            clipped_count);

        std::uint32_t draw_index = 0;
        for (const vulkan_pending_draw &draw : pending_draws_) {
            if (pending_draw_target(draw) != target) {
                continue;
            }

            std::visit([&](const auto &entry) {
                using draw_type = std::decay_t<decltype(entry)>;
                if constexpr (std::is_same_v<draw_type, vulkan_rectangle_draw>) {
                    LOG_INFO(DRIVER_GRAPHICS,
                        "  [{}] rect pos=({}, {}) size={}x{} color=({}, {}, {}, {}) blend={} clip_rects={}",
                        draw_index,
                        entry.rectangle.top.x,
                        entry.rectangle.top.y,
                        entry.rectangle.size.x,
                        entry.rectangle.size.y,
                        entry.color[0],
                        entry.color[1],
                        entry.color[2],
                        entry.color[3],
                        entry.state.blend_enabled,
                        entry.state.clip_rects.size());
                } else if constexpr (std::is_same_v<draw_type, vulkan_bitmap_draw>) {
                    const eka2l1::vec2 texture_size = entry.texture ? entry.texture->get_size() : eka2l1::vec2(0, 0);
                    const eka2l1::vec2 mask_size = entry.mask_texture ? entry.mask_texture->get_size() : eka2l1::vec2(0, 0);
                    LOG_INFO(DRIVER_GRAPHICS,
                        "  [{}] bitmap tex={}x{} fb_src={} mask={} mask_size={}x{} dest=({}, {}) {}x{} src=({}, {}) {}x{} flags=0x{:X} color=({}, {}, {}, {}) blend={} blend_rgb=({},{},{}) blend_alpha=({},{},{}) clip_rects={}",
                        draw_index,
                        texture_size.x,
                        texture_size.y,
                        entry.texture ? entry.texture->framebuffer_target() : false,
                        entry.mask_texture != nullptr,
                        mask_size.x,
                        mask_size.y,
                        entry.destination.top.x,
                        entry.destination.top.y,
                        entry.destination.size.x,
                        entry.destination.size.y,
                        entry.source.top.x,
                        entry.source.top.y,
                        entry.source.size.x,
                        entry.source.size.y,
                        entry.flags,
                        entry.color[0],
                        entry.color[1],
                        entry.color[2],
                        entry.color[3],
                        entry.state.blend_enabled,
                        static_cast<int>(entry.state.rgb_blend_equation),
                        static_cast<int>(entry.state.rgb_source_factor),
                        static_cast<int>(entry.state.rgb_dest_factor),
                        static_cast<int>(entry.state.alpha_blend_equation),
                        static_cast<int>(entry.state.alpha_source_factor),
                        static_cast<int>(entry.state.alpha_dest_factor),
                        entry.state.clip_rects.size());
                } else if constexpr (std::is_same_v<draw_type, vulkan_advanced_draw>) {
                    const eka2l1::vec2 tex0_size = entry.texture_slots[0] ? entry.texture_slots[0]->get_size() : eka2l1::vec2(0, 0);
                    const eka2l1::vec2 tex1_size = entry.texture_slots[1] ? entry.texture_slots[1]->get_size() : eka2l1::vec2(0, 0);
                    const eka2l1::vec2 tex2_size = entry.texture_slots[2] ? entry.texture_slots[2]->get_size() : eka2l1::vec2(0, 0);
                    const std::size_t input_count = advanced_draw_inputs(entry).size();
                    LOG_INFO(DRIVER_GRAPHICS,
                        "  [{}] advanced primitive={} indexed={} first={} vertex_count={} index_count={} index_offset={} vertex_base={} viewport_set={} viewport=({},{} {}x{}) blend={} blend_rgb=({},{},{}) blend_alpha=({},{},{}) depth={} stencil={} cull={} color_mask=0x{:X} clip_rects={} inputs={} vbs=[{},{},{}] tex0={}x{} tex1={}x{} tex2={}x{}",
                        draw_index,
                        static_cast<int>(entry.primitive_mode),
                        entry.indexed,
                        entry.first_vertex,
                        entry.vertex_count,
                        entry.index_count,
                        entry.index_offset,
                        entry.vertex_base,
                        entry.state.viewport_set,
                        entry.state.viewport.top.x,
                        entry.state.viewport.top.y,
                        entry.state.viewport.size.x,
                        entry.state.viewport.size.y,
                        entry.state.blend_enabled,
                        static_cast<int>(entry.state.rgb_blend_equation),
                        static_cast<int>(entry.state.rgb_source_factor),
                        static_cast<int>(entry.state.rgb_dest_factor),
                        static_cast<int>(entry.state.alpha_blend_equation),
                        static_cast<int>(entry.state.alpha_source_factor),
                        static_cast<int>(entry.state.alpha_dest_factor),
                        entry.state.depth_test_enabled,
                        entry.state.stencil_test_enabled,
                        entry.state.cull_enabled,
                        entry.state.color_write_mask,
                        entry.state.clip_rects.size(),
                        input_count,
                        entry.vertex_buffers[0] != nullptr,
                        entry.vertex_buffers[1] != nullptr,
                        entry.vertex_buffers[2] != nullptr,
                        tex0_size.x,
                        tex0_size.y,
                        tex1_size.x,
                        tex1_size.y,
                        tex2_size.x,
                        tex2_size.y);
                }
            },
                draw);

            draw_index++;
        }
    }

    bool vulkan_graphics_driver::create_instance() {
#if EKA2L1_PLATFORM(MACOS)
        set_bundled_moltenvk_icd_path();
#endif

        auto avail_layers = vk::enumerateInstanceLayerProperties();
        auto avail_extensions = vk::enumerateInstanceExtensionProperties();

        std::vector<const char *> enabled_layers;

        auto add_layer_if_avail = [&](const char *name) -> bool {
            for (auto &avail_layer : avail_layers) {
                if (strncmp(avail_layer.layerName, name, strlen(name)) == 0) {
                    enabled_layers.push_back(name);
                    return true;
                }
            }

            return false;
        };

        bool has_khronos_validation = false;
        bool has_lunarg_validation = false;
        if (debug_options_.enabled) {
            has_khronos_validation = add_layer_if_avail("VK_LAYER_KHRONOS_validation");
            has_lunarg_validation = add_layer_if_avail("VK_LAYER_LUNARG_standard_validation");
            if (!has_khronos_validation && !has_lunarg_validation) {
                LOG_WARN(DRIVER_GRAPHICS, "Vulkan debug requested, but no validation layer is available");
            }
        }

        std::vector<const char *> enabled_extensions;
        vk::InstanceCreateFlags instance_flags;

        auto add_extension_if_avail = [&](const char *name, const bool required) -> bool {
            if (has_extension(avail_extensions, name)) {
                enabled_extensions.push_back(name);
                return true;
            }

            if (required) {
                LOG_ERROR(DRIVER_GRAPHICS, "Required Vulkan instance extension {} is not available", name);
                return false;
            }

            return false;
        };

        if (!add_extension_if_avail(VK_KHR_SURFACE_EXTENSION_NAME, true)) {
            return false;
        }

        if (debug_options_.enabled) {
            const bool has_debug_report = add_extension_if_avail(VK_EXT_DEBUG_REPORT_EXTENSION_NAME, false);
            if (!has_debug_report) {
                LOG_WARN(DRIVER_GRAPHICS, "Vulkan debug report extension is not available; validation messages may not be logged");
            }
        }

#if EKA2L1_PLATFORM(WIN32)
        if (!add_extension_if_avail(VK_KHR_WIN32_SURFACE_EXTENSION_NAME, true)) {
            return false;
        }
#elif EKA2L1_PLATFORM(ANDROID)
        if (!add_extension_if_avail(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME, true)) {
            return false;
        }
#elif EKA2L1_PLATFORM(MACOS) || EKA2L1_PLATFORM(IOS)
        if (!add_extension_if_avail(VK_EXT_METAL_SURFACE_EXTENSION_NAME, true)) {
            return false;
        }
        if (add_extension_if_avail(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME, true)) {
            instance_flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
        } else {
            return false;
        }
#elif EKA2L1_PLATFORM(UNIX)
        if (wsi_.type == window_system_type::wayland) {
            if (!add_extension_if_avail(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME, true)) {
                return false;
            }
        } else {
            if (!add_extension_if_avail(VK_KHR_XCB_SURFACE_EXTENSION_NAME, true)) {
                return false;
            }
        }
#endif

        vk::ApplicationInfo app_info("EKA2L1", 1, "EDriver", 1, VK_API_VERSION_1_1);
        vk::InstanceCreateInfo instance_create_info(instance_flags, &app_info, static_cast<std::uint32_t>(enabled_layers.size()),
            enabled_layers.data(), static_cast<std::uint32_t>(enabled_extensions.size()), enabled_extensions.data());

        try {
            inst_ = vk::createInstanceUnique(instance_create_info);
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Instance creation failed with error: {}", e.what());
            return false;
        }

        if (!inst_) {
            return false;
        }

        return true;
    }

    bool vulkan_graphics_driver::create_debug_callback() {
        // We should also get validation functions
        create_debug_report_callback_ext_ = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(inst_->getProcAddr("vkCreateDebugReportCallbackEXT"));
        destroy_debug_report_callback_ext_ = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(inst_->getProcAddr("vkDestroyDebugReportCallbackEXT"));

        if (!create_debug_report_callback_ext_ || !destroy_debug_report_callback_ext_) {
            return false;
        }

        vk::DebugReportFlagsEXT report_callback_flags(vk::DebugReportFlagBitsEXT::eWarning | vk::DebugReportFlagBitsEXT::ePerformanceWarning | vk::DebugReportFlagBitsEXT::eError);
        vk::DebugReportCallbackCreateInfoEXT report_callback_create_info;
        report_callback_create_info.flags = report_callback_flags;
        report_callback_create_info.pfnCallback = vulkan_reporter;

        reporter_ = inst_->createDebugReportCallbackEXTUnique(report_callback_create_info);

        if (!reporter_) {
            return false;
        }

        return true;
    }

    static std::uint64_t score_for_me_the_gpu(const vk::PhysicalDevice &dvc) {
        std::uint64_t scr = 0;

        auto prop = dvc.getProperties();

        switch (prop.deviceType) {
        // Prefer discrete GPU, not integrated.
        case vk::PhysicalDeviceType::eDiscreteGpu: {
            scr += 1000;
            break;
        }

        default: {
            scr += 500;
            break;
        }
        }

        LOG_TRACE(DRIVER_GRAPHICS, "Found device: {}, score: {}", prop.deviceName.data(), scr);

        return scr;
    }

    bool vulkan_graphics_driver::create_device() {
        {
            auto dvcs = inst_->enumeratePhysicalDevices();
            if (dvcs.size() == 0) {
                LOG_ERROR(DRIVER_GRAPHICS, "No physical devices found for Vulkan!");
                return false;
            }

            std::uint64_t last_max_dvc_score = 0;
            std::size_t idx = 0;

            for (std::size_t i = 0; i < dvcs.size(); i++) {
                const std::uint64_t scr = score_for_me_the_gpu(dvcs[i]);
                if (scr > last_max_dvc_score) {
                    last_max_dvc_score = scr;
                    idx = i;
                }
            }

            phys_dvc_ = dvcs[idx];
            physical_device_features_ = phys_dvc_.getFeatures();
            physical_device_properties_ = phys_dvc_.getProperties();
            LOG_TRACE(DRIVER_GRAPHICS, "Choosing device: {}", physical_device_properties_.deviceName.data());
        }

        std::optional<std::uint32_t> queue_index;
        auto fam_queues = phys_dvc_.getQueueFamilyProperties();

        for (std::size_t i = 0; i < fam_queues.size(); i++) {
            const std::uint32_t family_index = static_cast<std::uint32_t>(i);
            const bool supports_graphics = static_cast<bool>(fam_queues[i].queueFlags & vk::QueueFlagBits::eGraphics);
            const bool supports_present = phys_dvc_.getSurfaceSupportKHR(family_index, surface_.get());
            if (supports_graphics && supports_present) {
                queue_index = family_index;
                break;
            }
        }

        if (!queue_index) {
            LOG_ERROR(DRIVER_GRAPHICS, "No Vulkan queue family supports both graphics and presentation");
            return false;
        }

        float queue_pris[1] = { 1.0f };
        vk::DeviceQueueCreateInfo queue_create_info(vk::DeviceQueueCreateFlags{}, *queue_index, 1, queue_pris);

        std::vector<const char *> enabled_extensions;
        const std::vector<vk::ExtensionProperties> device_extensions = phys_dvc_.enumerateDeviceExtensionProperties();

        if (!has_extension(device_extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            LOG_ERROR(DRIVER_GRAPHICS, "Required Vulkan device extension {} is not available", VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            return false;
        }

        enabled_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        static constexpr const char *portability_subset_extension = "VK_KHR_portability_subset";
        if (has_extension(device_extensions, portability_subset_extension)) {
            enabled_extensions.push_back(portability_subset_extension);
        }

        vk::PhysicalDeviceFeatures enabled_features;
        if (physical_device_features_.samplerAnisotropy) {
            enabled_features.samplerAnisotropy = VK_TRUE;
        }

        vk::DeviceCreateInfo device_create_info(vk::DeviceCreateFlags{}, 1, &queue_create_info, 0, nullptr,
            static_cast<std::uint32_t>(enabled_extensions.size()), enabled_extensions.data());
        device_create_info.pEnabledFeatures = &enabled_features;

        try {
            dvc_ = phys_dvc_.createDeviceUnique(device_create_info);
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan device creation failed with error: {}", e.what());
            return false;
        }

        if (dvc_) {
            graphics_queue_family_index_ = *queue_index;
            graphics_queue_ = dvc_->getQueue(graphics_queue_family_index_, 0);
        }

        return static_cast<bool>(dvc_);
    }

    bool vulkan_graphics_driver::create_surface() {
#if EKA2L1_PLATFORM(WIN32)
        vk::Win32SurfaceCreateInfoKHR surface_create_info(vk::Win32SurfaceCreateFlagsKHR{}, nullptr, reinterpret_cast<HWND>(wsi_.render_surface));
        try {
            surface_ = inst_->createWin32SurfaceKHRUnique(surface_create_info);
        } catch (std::exception &ex) {
            LOG_ERROR(DRIVER_GRAPHICS, "Create Win32 Vulkan surface failed with error: {}", ex.what());
            return false;
        }

        return true;
#elif EKA2L1_PLATFORM(ANDROID)
        if (!wsi_.render_surface) {
            LOG_ERROR(DRIVER_GRAPHICS, "Can not create Android Vulkan surface without an ANativeWindow");
            return false;
        }

        vk::AndroidSurfaceCreateInfoKHR surface_create_info(vk::AndroidSurfaceCreateFlagsKHR{}, reinterpret_cast<struct ANativeWindow *>(wsi_.render_surface));
        try {
            surface_ = inst_->createAndroidSurfaceKHRUnique(surface_create_info);
        } catch (std::exception &ex) {
            LOG_ERROR(DRIVER_GRAPHICS, "Create Android Vulkan surface failed with error: {}", ex.what());
            return false;
        }
#elif EKA2L1_PLATFORM(MACOS) || EKA2L1_PLATFORM(IOS)
        void *metal_layer = get_or_create_vulkan_metal_layer(wsi_.render_surface, wsi_.render_surface_scale, wsi_.surface_width, wsi_.surface_height);
        if (!metal_layer) {
            return false;
        }

        vk::MetalSurfaceCreateInfoEXT surface_create_info(vk::MetalSurfaceCreateFlagsEXT{}, static_cast<const CAMetalLayer *>(metal_layer));
        try {
            surface_ = inst_->createMetalSurfaceEXTUnique(surface_create_info);
        } catch (std::exception &ex) {
            LOG_ERROR(DRIVER_GRAPHICS, "Create Metal Vulkan surface failed with error: {}", ex.what());
            return false;
        }
#elif EKA2L1_PLATFORM(UNIX)
        if (wsi_.type == window_system_type::wayland) {
            vk::WaylandSurfaceCreateInfoKHR surface_create_info(vk::WaylandSurfaceCreateFlagsKHR{},
                reinterpret_cast<wl_display *>(wsi_.display_connection), reinterpret_cast<wl_surface *>(wsi_.render_window));
            try {
                surface_ = inst_->createWaylandSurfaceKHRUnique(surface_create_info);
            } catch (std::exception &ex) {
                LOG_ERROR(DRIVER_GRAPHICS, "Create Wayland Vulkan surface failed with error: {}", ex.what());
                return false;
            }
        } else {
            std::uint64_t surface_handle_64 = reinterpret_cast<std::uint64_t>(wsi_.render_surface);
            vk::XcbSurfaceCreateInfoKHR surface_create_info(vk::XcbSurfaceCreateFlagsKHR{},
                reinterpret_cast<xcb_connection_t *>(wsi_.display_connection), static_cast<xcb_window_t>(surface_handle_64));
            try {
                surface_ = inst_->createXcbSurfaceKHRUnique(surface_create_info);
            } catch (std::exception &ex) {
                LOG_ERROR(DRIVER_GRAPHICS, "Create XCB Vulkan surface failed with error: {}", ex.what());
                return false;
            }
        }
#else
        LOG_ERROR(DRIVER_GRAPHICS, "Vulkan surface creation is not implemented for this platform");
        return false;
#endif

        return true;
    }

    bool vulkan_graphics_driver::create_swapchain() {
        if (!surface_ || !dvc_ || !phys_dvc_) {
            LOG_ERROR(DRIVER_GRAPHICS, "Can not create Vulkan swapchain before surface and device are ready");
            return false;
        }

        if ((wsi_.surface_width == 0) || (wsi_.surface_height == 0)) {
            LOG_ERROR(DRIVER_GRAPHICS, "Can not create Vulkan swapchain with an empty surface size");
            return false;
        }

        vk::SurfaceCapabilitiesKHR caps;
        std::vector<vk::SurfaceFormatKHR> formats;
        std::vector<vk::PresentModeKHR> present_modes;

        try {
            caps = phys_dvc_.getSurfaceCapabilitiesKHR(surface_.get());
            formats = phys_dvc_.getSurfaceFormatsKHR(surface_.get());
            present_modes = phys_dvc_.getSurfacePresentModesKHR(surface_.get());
        } catch (vk::SystemError &e) {
            if (e.code().value() == static_cast<int>(vk::Result::eErrorSurfaceLostKHR)) {
                LOG_TRACE(DRIVER_GRAPHICS, "Vulkan surface was lost during swapchain query; caller may recreate it");
            } else {
                LOG_ERROR(DRIVER_GRAPHICS, "Vulkan surface query failed with error: {}", e.what());
            }
            return false;
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan surface query failed with error: {}", e.what());
            return false;
        }

        if (formats.empty()) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan surface has no supported swapchain formats");
            return false;
        }

        if (present_modes.empty()) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan surface has no supported present modes");
            return false;
        }

        if (!(caps.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment)) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan surface images can not be used as color attachments");
            return false;
        }

        debug_swapchain_capture_supported_ = debug_options_.capture_present && static_cast<bool>(caps.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc);
        if (debug_options_.capture_present && !debug_swapchain_capture_supported_) {
            LOG_WARN(DRIVER_GRAPHICS, "Vulkan swapchain debug capture requested, but surface images do not support transfer source usage");
        }

        const vk::SurfaceFormatKHR surface_format = choose_surface_format(formats);
        const vk::PresentModeKHR present_mode = choose_present_mode(present_modes);
        const vk::Extent2D extent = choose_swapchain_extent(caps, wsi_.surface_width, wsi_.surface_height);

        std::uint32_t image_count = caps.minImageCount + 1;
        if ((caps.maxImageCount > 0) && (image_count > caps.maxImageCount)) {
            image_count = caps.maxImageCount;
        }

        const vk::SurfaceTransformFlagBitsKHR pre_transform = (caps.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)
            ? vk::SurfaceTransformFlagBitsKHR::eIdentity
            : caps.currentTransform;

        vk::SwapchainCreateInfoKHR swapchain_create_info;
        swapchain_create_info.surface = surface_.get();
        swapchain_create_info.minImageCount = image_count;
        swapchain_create_info.imageFormat = surface_format.format;
        swapchain_create_info.imageColorSpace = surface_format.colorSpace;
        swapchain_create_info.imageExtent = extent;
        swapchain_create_info.imageArrayLayers = 1;
        swapchain_create_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
        if (debug_swapchain_capture_supported_) {
            swapchain_create_info.imageUsage |= vk::ImageUsageFlagBits::eTransferSrc;
        }
        swapchain_create_info.imageSharingMode = vk::SharingMode::eExclusive;
        swapchain_create_info.preTransform = pre_transform;
        swapchain_create_info.compositeAlpha = choose_composite_alpha(caps.supportedCompositeAlpha);
        swapchain_create_info.presentMode = present_mode;
        swapchain_create_info.clipped = true;
        swapchain_create_info.oldSwapchain = swapchain_.get();

        vk::UniqueSwapchainKHR new_swapchain;
        try {
            new_swapchain = dvc_->createSwapchainKHRUnique(swapchain_create_info);
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan swapchain creation failed with error: {}", e.what());
            return false;
        }

        if (!new_swapchain) {
            return false;
        }

        swapchain_ = std::move(new_swapchain);
        swapchain_images_ = dvc_->getSwapchainImagesKHR(swapchain_.get());
        swapchain_image_views_.clear();
        swapchain_image_views_.reserve(swapchain_images_.size());

        for (const vk::Image image : swapchain_images_) {
            vk::ImageViewCreateInfo image_view_create_info;
            image_view_create_info.image = image;
            image_view_create_info.viewType = vk::ImageViewType::e2D;
            image_view_create_info.format = surface_format.format;
            image_view_create_info.components = vk::ComponentMapping(
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity);
            image_view_create_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            image_view_create_info.subresourceRange.baseMipLevel = 0;
            image_view_create_info.subresourceRange.levelCount = 1;
            image_view_create_info.subresourceRange.baseArrayLayer = 0;
            image_view_create_info.subresourceRange.layerCount = 1;

            try {
                swapchain_image_views_.push_back(dvc_->createImageViewUnique(image_view_create_info));
            } catch (std::exception &e) {
                LOG_ERROR(DRIVER_GRAPHICS, "Vulkan swapchain image view creation failed with error: {}", e.what());
                destroy_swapchain_resources();
                return false;
            }
        }

        swapchain_image_initialized_.assign(swapchain_images_.size(), false);
        swapchain_format_ = surface_format.format;
        swapchain_extent_ = extent;

        LOG_INFO(DRIVER_GRAPHICS, "Vulkan swapchain created: {}x{}, {} images", extent.width, extent.height, swapchain_images_.size());
        return true;
    }

    bool vulkan_graphics_driver::create_command_resources() {
        if (!dvc_ || (graphics_queue_family_index_ == std::numeric_limits<std::uint32_t>::max())) {
            return false;
        }

        vk::CommandPoolCreateInfo command_pool_create_info(
            vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            graphics_queue_family_index_);

        try {
            command_pool_ = dvc_->createCommandPoolUnique(command_pool_create_info);
            vk::CommandBufferAllocateInfo command_buffer_alloc_info(
                command_pool_.get(),
                vk::CommandBufferLevel::ePrimary,
                1);

            auto command_buffers = dvc_->allocateCommandBuffersUnique(command_buffer_alloc_info);
            if (command_buffers.empty()) {
                return false;
            }

            command_buffer_ = std::move(command_buffers[0]);
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan command resource creation failed with error: {}", e.what());
            return false;
        }

        return true;
    }

    bool vulkan_graphics_driver::create_sync_objects() {
        try {
            image_available_semaphore_ = dvc_->createSemaphoreUnique(vk::SemaphoreCreateInfo{});
            render_finished_semaphore_ = dvc_->createSemaphoreUnique(vk::SemaphoreCreateInfo{});
            frame_fence_ = dvc_->createFenceUnique(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan sync object creation failed with error: {}", e.what());
            return false;
        }

        return static_cast<bool>(image_available_semaphore_ && render_finished_semaphore_ && frame_fence_);
    }

    bool vulkan_graphics_driver::create_descriptor_resources() {
        if (bitmap_descriptor_set_layout_ && bitmap_descriptor_pool_) {
            return true;
        }

        try {
            vk::DescriptorSetLayoutBinding source_texture_binding;
            source_texture_binding.binding = 0;
            source_texture_binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            source_texture_binding.descriptorCount = 1;
            source_texture_binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layout_create_info;
            layout_create_info.bindingCount = 1;
            layout_create_info.pBindings = &source_texture_binding;
            bitmap_descriptor_set_layout_ = dvc_->createDescriptorSetLayoutUnique(layout_create_info);

            vk::DescriptorPoolSize pool_size(vk::DescriptorType::eCombinedImageSampler, 1024);
            vk::DescriptorPoolCreateInfo pool_create_info;
            pool_create_info.maxSets = 1024;
            pool_create_info.poolSizeCount = 1;
            pool_create_info.pPoolSizes = &pool_size;
            bitmap_descriptor_pool_ = dvc_->createDescriptorPoolUnique(pool_create_info);
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan descriptor resource creation failed with error: {}", e.what());
            bitmap_descriptor_pool_.reset();
            bitmap_descriptor_set_layout_.reset();
            return false;
        }

        return bitmap_descriptor_set_layout_ && bitmap_descriptor_pool_;
    }

    std::uint32_t vulkan_graphics_driver::find_memory_type(const std::uint32_t type_filter,
        const vk::MemoryPropertyFlags properties) const {
        const vk::PhysicalDeviceMemoryProperties memory_properties = phys_dvc_.getMemoryProperties();

        for (std::uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
            if ((type_filter & (1U << i)) && ((memory_properties.memoryTypes[i].propertyFlags & properties) == properties)) {
                return i;
            }
        }

        throw std::runtime_error("No suitable Vulkan memory type found");
    }

    bool vulkan_graphics_driver::submit_immediate(const std::function<void(vk::CommandBuffer)> &recorder) {
        if (!dvc_ || !command_pool_ || !graphics_queue_) {
            return false;
        }

        try {
            vk::CommandBufferAllocateInfo alloc_info(command_pool_.get(), vk::CommandBufferLevel::ePrimary, 1);
            auto command_buffers = dvc_->allocateCommandBuffersUnique(alloc_info);
            if (command_buffers.empty()) {
                return false;
            }

            vk::UniqueCommandBuffer command_buffer = std::move(command_buffers[0]);
            command_buffer->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
            recorder(command_buffer.get());
            command_buffer->end();

            vk::UniqueFence completion_fence = dvc_->createFenceUnique(vk::FenceCreateInfo{});
            const vk::CommandBuffer raw_command_buffer = command_buffer.get();
            vk::SubmitInfo submit_info;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &raw_command_buffer;
            graphics_queue_.submit(submit_info, completion_fence.get());

            const vk::Result wait_result = dvc_->waitForFences(completion_fence.get(), VK_TRUE, UINT64_MAX);
            if (wait_result != vk::Result::eSuccess) {
                LOG_ERROR(DRIVER_GRAPHICS, "Immediate Vulkan submission fence wait failed with result: {}", vk::to_string(wait_result));
                return false;
            }
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Immediate Vulkan submission failed with error: {}", e.what());
            return false;
        }

        return true;
    }

    void vulkan_graphics_driver::queue_texture_upload(vulkan_texture *texture, vk::Buffer staging_buffer,
        const eka2l1::vec3 &offset, const eka2l1::vec3 &size, const vk::ImageLayout old_layout) {
        if (!texture || !staging_buffer) {
            return;
        }

        pending_texture_uploads_.push_back({ texture, staging_buffer, offset, size, old_layout });
    }

    void vulkan_graphics_driver::record_pending_texture_uploads(vk::CommandBuffer command_buffer) {
        for (const vulkan_pending_texture_upload &upload : pending_texture_uploads_) {
            if (upload.texture) {
                upload.texture->record_upload_rgba8(command_buffer, upload.staging_buffer,
                    upload.offset, upload.size, upload.old_layout);
            }
        }
    }

    void vulkan_graphics_driver::complete_pending_texture_uploads() {
        for (const vulkan_pending_texture_upload &upload : pending_texture_uploads_) {
            if (upload.texture) {
                upload.texture->mark_upload_staging_idle();
            }
        }

        pending_texture_uploads_.clear();
    }

    bool vulkan_graphics_driver::flush_pending_texture_uploads() {
        if (pending_texture_uploads_.empty()) {
            return true;
        }

        const bool submitted = submit_immediate([&](vk::CommandBuffer command_buffer) {
            record_pending_texture_uploads(command_buffer);
        });
        complete_pending_texture_uploads();
        return submitted;
    }

    vk::DescriptorSet vulkan_graphics_driver::allocate_bitmap_descriptor_set() {
        if (!create_descriptor_resources()) {
            return nullptr;
        }

        const vk::DescriptorSetLayout layout = bitmap_descriptor_set_layout_.get();
        vk::DescriptorSetAllocateInfo allocate_info(bitmap_descriptor_pool_.get(), 1, &layout);
        try {
            return dvc_->allocateDescriptorSets(allocate_info).front();
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan bitmap descriptor set allocation failed with error: {}", e.what());
            return nullptr;
        }
    }

    void vulkan_graphics_driver::update_bitmap_descriptor_set(vk::DescriptorSet descriptor_set,
        vk::ImageView image_view, vk::Sampler sampler) {
        if (!descriptor_set || !image_view || !sampler) {
            return;
        }

        vk::DescriptorImageInfo image_info(sampler, image_view, vk::ImageLayout::eShaderReadOnlyOptimal);
        vk::WriteDescriptorSet descriptor_write;
        descriptor_write.dstSet = descriptor_set;
        descriptor_write.dstBinding = 0;
        descriptor_write.descriptorCount = 1;
        descriptor_write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        descriptor_write.pImageInfo = &image_info;

        dvc_->updateDescriptorSets(descriptor_write, nullptr);
    }

    void vulkan_graphics_driver::set_bound_framebuffer(vulkan_framebuffer *framebuffer,
        const framebuffer_bind_type type_bind) {
        if ((type_bind == framebuffer_bind_read) || (type_bind == framebuffer_bind_read_draw)) {
            bound_read_framebuffer_ = framebuffer;
        }

        if ((type_bind == framebuffer_bind_draw) || (type_bind == framebuffer_bind_read_draw)) {
            bound_draw_framebuffer_ = framebuffer;
        }
    }

    void vulkan_graphics_driver::clear_bound_framebuffer(vulkan_framebuffer *framebuffer) {
        if (bound_read_framebuffer_ == framebuffer) {
            bound_read_framebuffer_ = nullptr;
        }

        if (bound_draw_framebuffer_ == framebuffer) {
            bound_draw_framebuffer_ = nullptr;
        }
    }

    vk::UniquePipeline vulkan_graphics_driver::create_2d_pipeline(vk::RenderPass render_pass,
        vk::PipelineLayout pipeline_layout, vk::ShaderModule vertex_shader, vk::ShaderModule fragment_shader,
        const vulkan_blend_state_key &blend_state) {
        vk::PipelineShaderStageCreateInfo shader_stages[2];
        shader_stages[0].stage = vk::ShaderStageFlagBits::eVertex;
        shader_stages[0].module = vertex_shader;
        shader_stages[0].pName = "main";
        shader_stages[1].stage = vk::ShaderStageFlagBits::eFragment;
        shader_stages[1].module = fragment_shader;
        shader_stages[1].pName = "main";

        vk::PipelineVertexInputStateCreateInfo vertex_input_state;
        vk::PipelineInputAssemblyStateCreateInfo input_assembly_state(
            vk::PipelineInputAssemblyStateCreateFlags{},
            vk::PrimitiveTopology::eTriangleList,
            false);

        vk::PipelineViewportStateCreateInfo viewport_state;
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rasterization_state;
        rasterization_state.depthClampEnable = false;
        rasterization_state.rasterizerDiscardEnable = false;
        rasterization_state.polygonMode = vk::PolygonMode::eFill;
        rasterization_state.cullMode = vk::CullModeFlagBits::eNone;
        rasterization_state.frontFace = vk::FrontFace::eCounterClockwise;
        rasterization_state.depthBiasEnable = false;
        rasterization_state.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo multisample_state;
        multisample_state.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depth_stencil_state;
        depth_stencil_state.depthTestEnable = false;
        depth_stencil_state.depthWriteEnable = false;
        depth_stencil_state.depthCompareOp = vk::CompareOp::eAlways;
        depth_stencil_state.depthBoundsTestEnable = false;
        depth_stencil_state.stencilTestEnable = false;

        vk::PipelineColorBlendAttachmentState color_blend_attachment = make_blend_attachment(blend_state);
        vk::PipelineColorBlendStateCreateInfo color_blend_state;
        color_blend_state.logicOpEnable = false;
        color_blend_state.attachmentCount = 1;
        color_blend_state.pAttachments = &color_blend_attachment;

        const vk::DynamicState dynamic_states[] = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor,
            vk::DynamicState::eBlendConstants
        };
        vk::PipelineDynamicStateCreateInfo dynamic_state;
        dynamic_state.dynamicStateCount = static_cast<std::uint32_t>(sizeof(dynamic_states) / sizeof(dynamic_states[0]));
        dynamic_state.pDynamicStates = dynamic_states;

        vk::GraphicsPipelineCreateInfo pipeline_create_info;
        pipeline_create_info.stageCount = 2;
        pipeline_create_info.pStages = shader_stages;
        pipeline_create_info.pVertexInputState = &vertex_input_state;
        pipeline_create_info.pInputAssemblyState = &input_assembly_state;
        pipeline_create_info.pViewportState = &viewport_state;
        pipeline_create_info.pRasterizationState = &rasterization_state;
        pipeline_create_info.pMultisampleState = &multisample_state;
        pipeline_create_info.pDepthStencilState = &depth_stencil_state;
        pipeline_create_info.pColorBlendState = &color_blend_state;
        pipeline_create_info.pDynamicState = &dynamic_state;
        pipeline_create_info.layout = pipeline_layout;
        pipeline_create_info.renderPass = render_pass;
        pipeline_create_info.subpass = 0;

        auto pipeline_result = dvc_->createGraphicsPipelineUnique(nullptr, pipeline_create_info);
        if (pipeline_result.result != vk::Result::eSuccess) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan 2D pipeline creation failed with result {}", vk::to_string(pipeline_result.result));
            return {};
        }

        return std::move(pipeline_result.value);
    }

    vulkan_blend_pipeline_cache_entry *vulkan_graphics_driver::get_or_create_blend_pipeline(const vulkan_draw_state &draw_state) {
        const vulkan_blend_state_key key = make_blend_state_key(draw_state);
        if (key == default_blend_key_) {
            return nullptr;
        }

        const auto pipeline_match = std::find_if(blend_pipeline_cache_.begin(), blend_pipeline_cache_.end(),
            [&key](const vulkan_blend_pipeline_cache_entry &entry) {
                return entry.key == key;
            });
        if (pipeline_match != blend_pipeline_cache_.end()) {
            return &(*pipeline_match);
        }

        vulkan_blend_pipeline_cache_entry entry;
        entry.key = key;
        entry.rectangle_pipeline = create_2d_pipeline(render_pass_.get(), rectangle_pipeline_layout_.get(),
            rectangle_vertex_shader_.get(), rectangle_fragment_shader_.get(), key);
        entry.bitmap_pipeline = create_2d_pipeline(render_pass_.get(), bitmap_pipeline_layout_.get(),
            bitmap_vertex_shader_.get(), bitmap_fragment_shader_.get(), key);
        entry.offscreen_rectangle_pipeline = create_2d_pipeline(offscreen_render_pass_.get(), rectangle_pipeline_layout_.get(),
            rectangle_vertex_shader_.get(), rectangle_fragment_shader_.get(), key);
        entry.offscreen_bitmap_pipeline = create_2d_pipeline(offscreen_render_pass_.get(), bitmap_pipeline_layout_.get(),
            bitmap_vertex_shader_.get(), bitmap_fragment_shader_.get(), key);
        entry.offscreen_no_depth_rectangle_pipeline = create_2d_pipeline(offscreen_no_depth_render_pass_.get(), rectangle_pipeline_layout_.get(),
            rectangle_vertex_shader_.get(), rectangle_fragment_shader_.get(), key);
        entry.offscreen_no_depth_bitmap_pipeline = create_2d_pipeline(offscreen_no_depth_render_pass_.get(), bitmap_pipeline_layout_.get(),
            bitmap_vertex_shader_.get(), bitmap_fragment_shader_.get(), key);

        if (!entry.rectangle_pipeline || !entry.bitmap_pipeline || !entry.offscreen_rectangle_pipeline || !entry.offscreen_bitmap_pipeline || !entry.offscreen_no_depth_rectangle_pipeline || !entry.offscreen_no_depth_bitmap_pipeline) {
            LOG_ERROR(DRIVER_GRAPHICS, "Failed to create Vulkan blend pipeline variant");
            return nullptr;
        }

        blend_pipeline_cache_.push_back(std::move(entry));
        return &blend_pipeline_cache_.back();
    }

    vk::Pipeline vulkan_graphics_driver::get_or_create_upscale_pipeline(const std::string &shader_name,
        const vulkan_draw_state &draw_state, const bool offscreen) {
        if (shader_name.empty() || (shader_name == "Default") || (shader_name == "default")) {
            return nullptr;
        }

        const vulkan_blend_state_key key = make_blend_state_key(draw_state);
        const auto pipeline_match = std::find_if(upscale_pipeline_cache_.begin(), upscale_pipeline_cache_.end(),
            [&](const vulkan_upscale_pipeline_cache_entry &entry) {
                return (entry.shader_name == shader_name) && (entry.blend_key == key) && (entry.offscreen == offscreen);
            });
        if (pipeline_match != upscale_pipeline_cache_.end()) {
            return pipeline_match->pipeline.get();
        }

        vulkan_upscale_pipeline_cache_entry entry;
        entry.shader_name = shader_name;
        entry.blend_key = key;
        entry.offscreen = offscreen;

        if (!compile_upscale_fragment_shader(dvc_.get(), shader_name, entry.fragment_shader)) {
            return nullptr;
        }

        entry.pipeline = create_2d_pipeline(
            offscreen ? offscreen_render_pass_.get() : render_pass_.get(),
            bitmap_pipeline_layout_.get(),
            bitmap_vertex_shader_.get(),
            entry.fragment_shader.get(),
            key);
        if (!entry.pipeline) {
            LOG_WARN(DRIVER_GRAPHICS, "Failed to create Vulkan upscale pipeline for shader {}", shader_name);
            return nullptr;
        }

        upscale_pipeline_cache_.push_back(std::move(entry));
        return upscale_pipeline_cache_.back().pipeline.get();
    }

    vulkan_advanced_pipeline_cache_entry *vulkan_graphics_driver::get_or_create_advanced_pipeline(
        const vulkan_advanced_draw &draw, const bool offscreen) {
        if (!draw.program || !draw.program->vertex_module() || !draw.program->fragment_module()) {
            return nullptr;
        }

        const vk::ShaderModule vertex_module = draw.program->executable_vertex_module();
        const vk::ShaderModule fragment_module = draw.program->executable_fragment_module();
        if (!vertex_module || !fragment_module) {
            static bool warned = false;
            if (!warned) {
                LOG_WARN(DRIVER_GRAPHICS, "Vulkan advanced draw skipped because shader modules are not executable");
                warned = true;
            }
            return nullptr;
        }

        vulkan_advanced_pipeline_key key;
        key.program = draw.program;
        const std::vector<input_descriptor> &input_descriptors = advanced_draw_inputs(draw);
        key.input_descriptors = input_descriptors;
        key.primitive_mode = draw.primitive_mode;
        key.blend_state = make_blend_state_key(draw.state);
        key.cull_enabled = draw.state.cull_enabled;
        key.cull_face = draw.state.cull_face;
        key.front_face_rule = draw.state.front_face_rule;
        key.depth_test_enabled = draw.state.depth_test_enabled;
        key.depth_write_enabled = draw.state.depth_write_enabled;
        key.depth_compare = draw.state.depth_compare;
        key.depth_bias_enabled = draw.state.depth_bias_enabled;
        key.stencil_test_enabled = draw.state.stencil_test_enabled;
        key.front_stencil = draw.state.front_stencil;
        key.back_stencil = draw.state.back_stencil;
        key.offscreen = offscreen;

        const auto pipeline_match = std::find_if(advanced_pipeline_cache_.begin(), advanced_pipeline_cache_.end(),
            [&key](const vulkan_advanced_pipeline_cache_entry &entry) {
                return entry.key == key;
            });
        if (pipeline_match != advanced_pipeline_cache_.end()) {
            return &(*pipeline_match);
        }

        std::vector<vk::VertexInputBindingDescription> vertex_bindings;
        std::vector<vk::VertexInputAttributeDescription> vertex_attributes;
        if (!input_descriptors.empty()) {
            vertex_bindings = vulkan_input_descriptors::binding_descriptions(input_descriptors);
            vertex_attributes = vulkan_input_descriptors::attribute_descriptions(input_descriptors);
        }

        try {
            const vk::DescriptorSetLayout descriptor_set_layout = draw.program->descriptor_set_layout();
            vk::PipelineLayoutCreateInfo pipeline_layout_create_info;
            if (descriptor_set_layout) {
                pipeline_layout_create_info.setLayoutCount = 1;
                pipeline_layout_create_info.pSetLayouts = &descriptor_set_layout;
            }

            vulkan_advanced_pipeline_cache_entry entry;
            entry.key = key;
            entry.pipeline_layout = dvc_->createPipelineLayoutUnique(pipeline_layout_create_info);

            vk::PipelineShaderStageCreateInfo shader_stages[2];
            shader_stages[0].stage = vk::ShaderStageFlagBits::eVertex;
            shader_stages[0].module = vertex_module;
            shader_stages[0].pName = "main";
            shader_stages[1].stage = vk::ShaderStageFlagBits::eFragment;
            shader_stages[1].module = fragment_module;
            shader_stages[1].pName = "main";

            vk::PipelineVertexInputStateCreateInfo vertex_input_state;
            vertex_input_state.vertexBindingDescriptionCount = static_cast<std::uint32_t>(vertex_bindings.size());
            vertex_input_state.pVertexBindingDescriptions = vertex_bindings.empty() ? nullptr : vertex_bindings.data();
            vertex_input_state.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vertex_attributes.size());
            vertex_input_state.pVertexAttributeDescriptions = vertex_attributes.empty() ? nullptr : vertex_attributes.data();

            vk::PipelineInputAssemblyStateCreateInfo input_assembly_state(
                vk::PipelineInputAssemblyStateCreateFlags{},
                to_vulkan_primitive_topology(draw.primitive_mode),
                false);

            vk::PipelineViewportStateCreateInfo viewport_state;
            viewport_state.viewportCount = 1;
            viewport_state.scissorCount = 1;

            vk::PipelineRasterizationStateCreateInfo rasterization_state;
            rasterization_state.depthClampEnable = false;
            rasterization_state.rasterizerDiscardEnable = false;
            rasterization_state.polygonMode = vk::PolygonMode::eFill;
            rasterization_state.cullMode = to_vulkan_cull_mode(draw.state);
            rasterization_state.frontFace = to_vulkan_front_face(draw.state.front_face_rule);
            rasterization_state.depthBiasEnable = draw.state.depth_bias_enabled;
            rasterization_state.lineWidth = 1.0f;

            vk::PipelineDepthStencilStateCreateInfo depth_stencil_state;
            depth_stencil_state.depthTestEnable = draw.state.depth_test_enabled;
            depth_stencil_state.depthWriteEnable = draw.state.depth_write_enabled;
            depth_stencil_state.depthCompareOp = to_vulkan_compare_op(draw.state.depth_compare);
            depth_stencil_state.depthBoundsTestEnable = false;
            depth_stencil_state.stencilTestEnable = draw.state.stencil_test_enabled;
            depth_stencil_state.front = to_vulkan_stencil_state(draw.state.front_stencil);
            depth_stencil_state.back = to_vulkan_stencil_state(draw.state.back_stencil);
            depth_stencil_state.minDepthBounds = 0.0f;
            depth_stencil_state.maxDepthBounds = 1.0f;

            vk::PipelineMultisampleStateCreateInfo multisample_state;
            multisample_state.rasterizationSamples = vk::SampleCountFlagBits::e1;
            multisample_state.sampleShadingEnable = false;
            multisample_state.alphaToCoverageEnable = draw.state.sample_alpha_to_coverage_enabled;
            multisample_state.alphaToOneEnable = false;

            vk::PipelineColorBlendAttachmentState color_blend_attachment = make_blend_attachment(key.blend_state);
            vk::PipelineColorBlendStateCreateInfo color_blend_state;
            color_blend_state.logicOpEnable = false;
            color_blend_state.attachmentCount = 1;
            color_blend_state.pAttachments = &color_blend_attachment;

            const vk::DynamicState dynamic_states[] = {
                vk::DynamicState::eViewport,
                vk::DynamicState::eScissor,
                vk::DynamicState::eBlendConstants,
                vk::DynamicState::eDepthBias,
                vk::DynamicState::eLineWidth
            };
            vk::PipelineDynamicStateCreateInfo dynamic_state;
            dynamic_state.dynamicStateCount = static_cast<std::uint32_t>(sizeof(dynamic_states) / sizeof(dynamic_states[0]));
            dynamic_state.pDynamicStates = dynamic_states;

            vk::GraphicsPipelineCreateInfo pipeline_create_info;
            pipeline_create_info.stageCount = 2;
            pipeline_create_info.pStages = shader_stages;
            pipeline_create_info.pVertexInputState = &vertex_input_state;
            pipeline_create_info.pInputAssemblyState = &input_assembly_state;
            pipeline_create_info.pViewportState = &viewport_state;
            pipeline_create_info.pRasterizationState = &rasterization_state;
            pipeline_create_info.pMultisampleState = &multisample_state;
            pipeline_create_info.pDepthStencilState = &depth_stencil_state;
            pipeline_create_info.pColorBlendState = &color_blend_state;
            pipeline_create_info.pDynamicState = &dynamic_state;
            pipeline_create_info.layout = entry.pipeline_layout.get();
            pipeline_create_info.renderPass = offscreen ? offscreen_render_pass_.get() : render_pass_.get();
            pipeline_create_info.subpass = 0;

            auto pipeline_result = dvc_->createGraphicsPipelineUnique(nullptr, pipeline_create_info);
            if (pipeline_result.result != vk::Result::eSuccess) {
                LOG_ERROR(DRIVER_GRAPHICS, "Vulkan advanced pipeline creation failed with result {}", vk::to_string(pipeline_result.result));
                return nullptr;
            }

            entry.pipeline = std::move(pipeline_result.value);
            advanced_pipeline_cache_.push_back(std::move(entry));
            return &advanced_pipeline_cache_.back();
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan advanced pipeline creation failed: {}", e.what());
            return nullptr;
        }
    }

    bool vulkan_graphics_driver::create_render_resources() {
        if (!dvc_ || swapchain_image_views_.empty() || (swapchain_format_ == vk::Format::eUndefined)) {
            return false;
        }

        destroy_render_resources();

        try {
            depth_stencil_format_ = choose_depth_stencil_format(phys_dvc_);

            vk::AttachmentDescription color_attachment;
            color_attachment.format = swapchain_format_;
            color_attachment.samples = vk::SampleCountFlagBits::e1;
            color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
            color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            color_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            color_attachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
            color_attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

            vk::AttachmentDescription depth_stencil_attachment;
            depth_stencil_attachment.format = depth_stencil_format_;
            depth_stencil_attachment.samples = vk::SampleCountFlagBits::e1;
            depth_stencil_attachment.loadOp = vk::AttachmentLoadOp::eClear;
            depth_stencil_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
            depth_stencil_attachment.stencilLoadOp = vk::AttachmentLoadOp::eClear;
            depth_stencil_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            depth_stencil_attachment.initialLayout = vk::ImageLayout::eUndefined;
            depth_stencil_attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

            const vk::AttachmentDescription swapchain_attachments[] = {
                color_attachment,
                depth_stencil_attachment
            };
            vk::AttachmentReference color_reference(0, vk::ImageLayout::eColorAttachmentOptimal);
            vk::AttachmentReference depth_stencil_reference(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);
            vk::SubpassDescription subpass;
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &color_reference;
            subpass.pDepthStencilAttachment = &depth_stencil_reference;

            vk::RenderPassCreateInfo render_pass_create_info;
            render_pass_create_info.attachmentCount = static_cast<std::uint32_t>(sizeof(swapchain_attachments) / sizeof(swapchain_attachments[0]));
            render_pass_create_info.pAttachments = swapchain_attachments;
            render_pass_create_info.subpassCount = 1;
            render_pass_create_info.pSubpasses = &subpass;
            render_pass_ = dvc_->createRenderPassUnique(render_pass_create_info);

            vk::ShaderModuleCreateInfo vertex_shader_create_info(
                vk::ShaderModuleCreateFlags{},
                sizeof(rectangle_vertex_spirv),
                rectangle_vertex_spirv);
            vk::ShaderModuleCreateInfo fragment_shader_create_info(
                vk::ShaderModuleCreateFlags{},
                sizeof(rectangle_fragment_spirv),
                rectangle_fragment_spirv);
            rectangle_vertex_shader_ = dvc_->createShaderModuleUnique(vertex_shader_create_info);
            rectangle_fragment_shader_ = dvc_->createShaderModuleUnique(fragment_shader_create_info);

            vk::PushConstantRange push_constant_range(
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0,
                sizeof(vulkan_rectangle_push_constants));
            vk::PipelineLayoutCreateInfo pipeline_layout_create_info(
                vk::PipelineLayoutCreateFlags{},
                0,
                nullptr,
                1,
                &push_constant_range);
            rectangle_pipeline_layout_ = dvc_->createPipelineLayoutUnique(pipeline_layout_create_info);

            vk::PipelineShaderStageCreateInfo shader_stages[2];
            shader_stages[0].stage = vk::ShaderStageFlagBits::eVertex;
            shader_stages[0].module = rectangle_vertex_shader_.get();
            shader_stages[0].pName = "main";
            shader_stages[1].stage = vk::ShaderStageFlagBits::eFragment;
            shader_stages[1].module = rectangle_fragment_shader_.get();
            shader_stages[1].pName = "main";

            vk::PipelineVertexInputStateCreateInfo vertex_input_state;
            vk::PipelineInputAssemblyStateCreateInfo input_assembly_state(
                vk::PipelineInputAssemblyStateCreateFlags{},
                vk::PrimitiveTopology::eTriangleList,
                false);

            vk::PipelineViewportStateCreateInfo viewport_state;
            viewport_state.viewportCount = 1;
            viewport_state.scissorCount = 1;

            vk::PipelineRasterizationStateCreateInfo rasterization_state;
            rasterization_state.depthClampEnable = false;
            rasterization_state.rasterizerDiscardEnable = false;
            rasterization_state.polygonMode = vk::PolygonMode::eFill;
            rasterization_state.cullMode = vk::CullModeFlagBits::eNone;
            rasterization_state.frontFace = vk::FrontFace::eCounterClockwise;
            rasterization_state.depthBiasEnable = false;
            rasterization_state.lineWidth = 1.0f;

            vk::PipelineMultisampleStateCreateInfo multisample_state;
            multisample_state.rasterizationSamples = vk::SampleCountFlagBits::e1;

            vk::PipelineDepthStencilStateCreateInfo depth_stencil_state;
            depth_stencil_state.depthTestEnable = false;
            depth_stencil_state.depthWriteEnable = false;
            depth_stencil_state.depthCompareOp = vk::CompareOp::eAlways;
            depth_stencil_state.depthBoundsTestEnable = false;
            depth_stencil_state.stencilTestEnable = false;

            default_blend_key_ = make_blend_state_key(vulkan_draw_state{});
            vk::PipelineColorBlendAttachmentState color_blend_attachment = make_blend_attachment(default_blend_key_);

            vk::PipelineColorBlendStateCreateInfo color_blend_state;
            color_blend_state.logicOpEnable = false;
            color_blend_state.attachmentCount = 1;
            color_blend_state.pAttachments = &color_blend_attachment;

            const vk::DynamicState dynamic_states[] = {
                vk::DynamicState::eViewport,
                vk::DynamicState::eScissor,
                vk::DynamicState::eBlendConstants
            };
            vk::PipelineDynamicStateCreateInfo dynamic_state;
            dynamic_state.dynamicStateCount = static_cast<std::uint32_t>(sizeof(dynamic_states) / sizeof(dynamic_states[0]));
            dynamic_state.pDynamicStates = dynamic_states;

            vk::GraphicsPipelineCreateInfo pipeline_create_info;
            pipeline_create_info.stageCount = 2;
            pipeline_create_info.pStages = shader_stages;
            pipeline_create_info.pVertexInputState = &vertex_input_state;
            pipeline_create_info.pInputAssemblyState = &input_assembly_state;
            pipeline_create_info.pViewportState = &viewport_state;
            pipeline_create_info.pRasterizationState = &rasterization_state;
            pipeline_create_info.pMultisampleState = &multisample_state;
            pipeline_create_info.pDepthStencilState = &depth_stencil_state;
            pipeline_create_info.pColorBlendState = &color_blend_state;
            pipeline_create_info.pDynamicState = &dynamic_state;
            pipeline_create_info.layout = rectangle_pipeline_layout_.get();
            pipeline_create_info.renderPass = render_pass_.get();
            pipeline_create_info.subpass = 0;

            auto pipeline_result = dvc_->createGraphicsPipelineUnique(nullptr, pipeline_create_info);
            if (pipeline_result.result != vk::Result::eSuccess) {
                LOG_ERROR(DRIVER_GRAPHICS, "Vulkan rectangle pipeline creation failed with result {}", vk::to_string(pipeline_result.result));
                destroy_render_resources();
                return false;
            }

            rectangle_pipeline_ = std::move(pipeline_result.value);

            if (!create_descriptor_resources()) {
                destroy_render_resources();
                return false;
            }

            vk::ShaderModuleCreateInfo bitmap_vertex_shader_create_info(
                vk::ShaderModuleCreateFlags{},
                sizeof(bitmap_vertex_spirv),
                bitmap_vertex_spirv);
            vk::ShaderModuleCreateInfo bitmap_fragment_shader_create_info(
                vk::ShaderModuleCreateFlags{},
                sizeof(bitmap_fragment_spirv),
                bitmap_fragment_spirv);
            bitmap_vertex_shader_ = dvc_->createShaderModuleUnique(bitmap_vertex_shader_create_info);
            bitmap_fragment_shader_ = dvc_->createShaderModuleUnique(bitmap_fragment_shader_create_info);

            vk::PushConstantRange bitmap_push_constant_range(
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0,
                sizeof(vulkan_bitmap_push_constants));
            const vk::DescriptorSetLayout bitmap_descriptor_set_layouts[] = {
                bitmap_descriptor_set_layout_.get(),
                bitmap_descriptor_set_layout_.get()
            };
            vk::PipelineLayoutCreateInfo bitmap_pipeline_layout_create_info(
                vk::PipelineLayoutCreateFlags{},
                static_cast<std::uint32_t>(sizeof(bitmap_descriptor_set_layouts) / sizeof(bitmap_descriptor_set_layouts[0])),
                bitmap_descriptor_set_layouts,
                1,
                &bitmap_push_constant_range);
            bitmap_pipeline_layout_ = dvc_->createPipelineLayoutUnique(bitmap_pipeline_layout_create_info);

            vk::PipelineShaderStageCreateInfo bitmap_shader_stages[2];
            bitmap_shader_stages[0].stage = vk::ShaderStageFlagBits::eVertex;
            bitmap_shader_stages[0].module = bitmap_vertex_shader_.get();
            bitmap_shader_stages[0].pName = "main";
            bitmap_shader_stages[1].stage = vk::ShaderStageFlagBits::eFragment;
            bitmap_shader_stages[1].module = bitmap_fragment_shader_.get();
            bitmap_shader_stages[1].pName = "main";

            pipeline_create_info.pStages = bitmap_shader_stages;
            pipeline_create_info.layout = bitmap_pipeline_layout_.get();

            auto bitmap_pipeline_result = dvc_->createGraphicsPipelineUnique(nullptr, pipeline_create_info);
            if (bitmap_pipeline_result.result != vk::Result::eSuccess) {
                LOG_ERROR(DRIVER_GRAPHICS, "Vulkan bitmap pipeline creation failed with result {}", vk::to_string(bitmap_pipeline_result.result));
                destroy_render_resources();
                return false;
            }

            bitmap_pipeline_ = std::move(bitmap_pipeline_result.value);

            vk::AttachmentDescription offscreen_color_attachment;
            offscreen_color_attachment.format = vk::Format::eR8G8B8A8Unorm;
            offscreen_color_attachment.samples = vk::SampleCountFlagBits::e1;
            offscreen_color_attachment.loadOp = vk::AttachmentLoadOp::eLoad;
            offscreen_color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            offscreen_color_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            offscreen_color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            offscreen_color_attachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
            offscreen_color_attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

            vk::AttachmentDescription offscreen_depth_stencil_attachment;
            offscreen_depth_stencil_attachment.format = depth_stencil_format_;
            offscreen_depth_stencil_attachment.samples = vk::SampleCountFlagBits::e1;
            offscreen_depth_stencil_attachment.loadOp = vk::AttachmentLoadOp::eLoad;
            offscreen_depth_stencil_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            offscreen_depth_stencil_attachment.stencilLoadOp = vk::AttachmentLoadOp::eLoad;
            offscreen_depth_stencil_attachment.stencilStoreOp = vk::AttachmentStoreOp::eStore;
            offscreen_depth_stencil_attachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            offscreen_depth_stencil_attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

            const vk::AttachmentDescription offscreen_attachments[] = {
                offscreen_color_attachment,
                offscreen_depth_stencil_attachment
            };
            const vk::AttachmentReference offscreen_color_reference(0, vk::ImageLayout::eColorAttachmentOptimal);
            const vk::AttachmentReference offscreen_depth_stencil_reference(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

            vk::SubpassDescription offscreen_subpass;
            offscreen_subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            offscreen_subpass.colorAttachmentCount = 1;
            offscreen_subpass.pColorAttachments = &offscreen_color_reference;
            offscreen_subpass.pDepthStencilAttachment = &offscreen_depth_stencil_reference;

            vk::RenderPassCreateInfo offscreen_render_pass_create_info;
            offscreen_render_pass_create_info.attachmentCount = static_cast<std::uint32_t>(sizeof(offscreen_attachments) / sizeof(offscreen_attachments[0]));
            offscreen_render_pass_create_info.pAttachments = offscreen_attachments;
            offscreen_render_pass_create_info.subpassCount = 1;
            offscreen_render_pass_create_info.pSubpasses = &offscreen_subpass;
            offscreen_render_pass_ = dvc_->createRenderPassUnique(offscreen_render_pass_create_info);

            vk::SubpassDescription offscreen_no_depth_subpass;
            offscreen_no_depth_subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            offscreen_no_depth_subpass.colorAttachmentCount = 1;
            offscreen_no_depth_subpass.pColorAttachments = &offscreen_color_reference;

            vk::RenderPassCreateInfo offscreen_no_depth_render_pass_create_info;
            offscreen_no_depth_render_pass_create_info.attachmentCount = 1;
            offscreen_no_depth_render_pass_create_info.pAttachments = &offscreen_color_attachment;
            offscreen_no_depth_render_pass_create_info.subpassCount = 1;
            offscreen_no_depth_render_pass_create_info.pSubpasses = &offscreen_no_depth_subpass;
            offscreen_no_depth_render_pass_ = dvc_->createRenderPassUnique(offscreen_no_depth_render_pass_create_info);

            pipeline_create_info.renderPass = offscreen_render_pass_.get();
            pipeline_create_info.pStages = shader_stages;
            pipeline_create_info.layout = rectangle_pipeline_layout_.get();
            auto offscreen_rectangle_pipeline_result = dvc_->createGraphicsPipelineUnique(nullptr, pipeline_create_info);
            if (offscreen_rectangle_pipeline_result.result != vk::Result::eSuccess) {
                LOG_ERROR(DRIVER_GRAPHICS, "Vulkan offscreen rectangle pipeline creation failed with result {}",
                    vk::to_string(offscreen_rectangle_pipeline_result.result));
                destroy_render_resources();
                return false;
            }

            offscreen_rectangle_pipeline_ = std::move(offscreen_rectangle_pipeline_result.value);

            pipeline_create_info.pStages = bitmap_shader_stages;
            pipeline_create_info.layout = bitmap_pipeline_layout_.get();
            auto offscreen_bitmap_pipeline_result = dvc_->createGraphicsPipelineUnique(nullptr, pipeline_create_info);
            if (offscreen_bitmap_pipeline_result.result != vk::Result::eSuccess) {
                LOG_ERROR(DRIVER_GRAPHICS, "Vulkan offscreen bitmap pipeline creation failed with result {}",
                    vk::to_string(offscreen_bitmap_pipeline_result.result));
                destroy_render_resources();
                return false;
            }

            offscreen_bitmap_pipeline_ = std::move(offscreen_bitmap_pipeline_result.value);

            pipeline_create_info.renderPass = offscreen_no_depth_render_pass_.get();
            pipeline_create_info.pStages = shader_stages;
            pipeline_create_info.layout = rectangle_pipeline_layout_.get();
            auto offscreen_no_depth_rectangle_pipeline_result = dvc_->createGraphicsPipelineUnique(nullptr, pipeline_create_info);
            if (offscreen_no_depth_rectangle_pipeline_result.result != vk::Result::eSuccess) {
                LOG_ERROR(DRIVER_GRAPHICS, "Vulkan no-depth offscreen rectangle pipeline creation failed with result {}",
                    vk::to_string(offscreen_no_depth_rectangle_pipeline_result.result));
                destroy_render_resources();
                return false;
            }

            offscreen_no_depth_rectangle_pipeline_ = std::move(offscreen_no_depth_rectangle_pipeline_result.value);

            pipeline_create_info.pStages = bitmap_shader_stages;
            pipeline_create_info.layout = bitmap_pipeline_layout_.get();
            auto offscreen_no_depth_bitmap_pipeline_result = dvc_->createGraphicsPipelineUnique(nullptr, pipeline_create_info);
            if (offscreen_no_depth_bitmap_pipeline_result.result != vk::Result::eSuccess) {
                LOG_ERROR(DRIVER_GRAPHICS, "Vulkan no-depth offscreen bitmap pipeline creation failed with result {}",
                    vk::to_string(offscreen_no_depth_bitmap_pipeline_result.result));
                destroy_render_resources();
                return false;
            }

            offscreen_no_depth_bitmap_pipeline_ = std::move(offscreen_no_depth_bitmap_pipeline_result.value);

            swapchain_framebuffers_.reserve(swapchain_image_views_.size());
            vk::ImageCreateInfo depth_image_create_info;
            depth_image_create_info.imageType = vk::ImageType::e2D;
            depth_image_create_info.format = depth_stencil_format_;
            depth_image_create_info.extent = vk::Extent3D(swapchain_extent_.width, swapchain_extent_.height, 1);
            depth_image_create_info.mipLevels = 1;
            depth_image_create_info.arrayLayers = 1;
            depth_image_create_info.samples = vk::SampleCountFlagBits::e1;
            depth_image_create_info.tiling = vk::ImageTiling::eOptimal;
            depth_image_create_info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
            depth_image_create_info.sharingMode = vk::SharingMode::eExclusive;
            depth_image_create_info.initialLayout = vk::ImageLayout::eUndefined;
            swapchain_depth_stencil_image_ = dvc_->createImageUnique(depth_image_create_info);

            const vk::MemoryRequirements memory_requirements = dvc_->getImageMemoryRequirements(swapchain_depth_stencil_image_.get());
            vk::MemoryAllocateInfo memory_allocate_info(
                memory_requirements.size,
                find_memory_type(memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal));
            swapchain_depth_stencil_memory_ = dvc_->allocateMemoryUnique(memory_allocate_info);
            dvc_->bindImageMemory(swapchain_depth_stencil_image_.get(), swapchain_depth_stencil_memory_.get(), 0);

            vk::ImageViewCreateInfo depth_image_view_create_info;
            depth_image_view_create_info.image = swapchain_depth_stencil_image_.get();
            depth_image_view_create_info.viewType = vk::ImageViewType::e2D;
            depth_image_view_create_info.format = depth_stencil_format_;
            depth_image_view_create_info.subresourceRange = vk::ImageSubresourceRange(aspect_for_format(depth_stencil_format_), 0, 1, 0, 1);
            swapchain_depth_stencil_image_view_ = dvc_->createImageViewUnique(depth_image_view_create_info);

            for (const auto &image_view : swapchain_image_views_) {
                const vk::ImageView attachments[] = {
                    image_view.get(),
                    swapchain_depth_stencil_image_view_.get()
                };
                vk::FramebufferCreateInfo framebuffer_create_info;
                framebuffer_create_info.renderPass = render_pass_.get();
                framebuffer_create_info.attachmentCount = static_cast<std::uint32_t>(sizeof(attachments) / sizeof(attachments[0]));
                framebuffer_create_info.pAttachments = attachments;
                framebuffer_create_info.width = swapchain_extent_.width;
                framebuffer_create_info.height = swapchain_extent_.height;
                framebuffer_create_info.layers = 1;
                swapchain_framebuffers_.push_back(dvc_->createFramebufferUnique(framebuffer_create_info));
            }
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan render resource creation failed with error: {}", e.what());
            destroy_render_resources();
            return false;
        }

        return render_pass_ && rectangle_pipeline_layout_ && rectangle_pipeline_ && bitmap_descriptor_set_layout_ && bitmap_descriptor_pool_ && bitmap_pipeline_layout_ && bitmap_pipeline_ && offscreen_render_pass_ && offscreen_rectangle_pipeline_ && offscreen_bitmap_pipeline_ && offscreen_no_depth_render_pass_ && offscreen_no_depth_rectangle_pipeline_ && offscreen_no_depth_bitmap_pipeline_ && (swapchain_framebuffers_.size() == swapchain_image_views_.size());
    }

    void vulkan_graphics_driver::destroy_render_resources() {
        advanced_pipeline_cache_.clear();
        upscale_pipeline_cache_.clear();
        blend_pipeline_cache_.clear();
        swapchain_framebuffers_.clear();
        swapchain_depth_stencil_image_view_.reset();
        swapchain_depth_stencil_image_.reset();
        swapchain_depth_stencil_memory_.reset();
        offscreen_no_depth_bitmap_pipeline_.reset();
        offscreen_no_depth_rectangle_pipeline_.reset();
        offscreen_no_depth_render_pass_.reset();
        offscreen_bitmap_pipeline_.reset();
        offscreen_rectangle_pipeline_.reset();
        offscreen_render_pass_.reset();
        bitmap_pipeline_.reset();
        bitmap_pipeline_layout_.reset();
        bitmap_fragment_shader_.reset();
        bitmap_vertex_shader_.reset();
        rectangle_pipeline_.reset();
        rectangle_pipeline_layout_.reset();
        rectangle_fragment_shader_.reset();
        rectangle_vertex_shader_.reset();
        render_pass_.reset();
    }

    void vulkan_graphics_driver::destroy_swapchain_resources() {
        destroy_render_resources();
        swapchain_image_initialized_.clear();
        swapchain_image_views_.clear();
        swapchain_images_.clear();
        swapchain_.reset();
        swapchain_format_ = vk::Format::eUndefined;
        swapchain_extent_ = vk::Extent2D(0, 0);
    }

    bool vulkan_graphics_driver::has_pending_framebuffer_draws(vulkan_framebuffer *target) const {
        if (!target) {
            return false;
        }

        return std::find_if(pending_draws_.begin(), pending_draws_.end(),
                   [target](const vulkan_pending_draw &draw) {
                       return pending_draw_target(draw) == target;
                   })
            != pending_draws_.end();
    }

    bool vulkan_graphics_driver::can_use_no_depth_offscreen(vulkan_framebuffer *target) const {
        if (!target || !offscreen_no_depth_render_pass_ || !offscreen_no_depth_rectangle_pipeline_ || !offscreen_no_depth_bitmap_pipeline_) {
            return false;
        }

        auto uses_depth_or_stencil = [](const vulkan_draw_state &state) {
            return state.depth_test_enabled || state.stencil_test_enabled || state.depth_bias_enabled;
        };

        bool found_target_draw = false;
        for (const vulkan_pending_draw &draw : pending_draws_) {
            if (pending_draw_target(draw) != target) {
                continue;
            }

            found_target_draw = true;
            const bool requires_depth_path = std::visit([&](const auto &entry) {
                using draw_type = std::decay_t<decltype(entry)>;
                if constexpr (std::is_same_v<draw_type, vulkan_advanced_draw>) {
                    return true;
                } else if constexpr (std::is_same_v<draw_type, vulkan_bitmap_draw>) {
                    const bool uses_custom_upscale_shader = (entry.flags & bitmap_draw_flag_use_upscale_shader) && !active_upscale_shader_.empty() && (active_upscale_shader_ != "Default") && (active_upscale_shader_ != "default");
                    return uses_depth_or_stencil(entry.state) || uses_custom_upscale_shader;
                } else {
                    return uses_depth_or_stencil(entry.state);
                }
            },
                draw);

            if (requires_depth_path) {
                return false;
            }
        }

        return found_target_draw;
    }

    void vulkan_graphics_driver::record_pending_draws(vk::CommandBuffer command_buffer, const vk::Extent2D &extent,
        vulkan_framebuffer *target, vk::Pipeline rectangle_pipeline, vk::Pipeline bitmap_pipeline,
        const bool offscreen, const bool offscreen_has_depth,
        std::vector<vulkan_shader_descriptor_snapshot> *descriptor_snapshots) {
        const vk::Viewport viewport(
            0.0f,
            0.0f,
            static_cast<float>(extent.width),
            static_cast<float>(extent.height),
            0.0f,
            1.0f);
        const vk::Rect2D scissor(vk::Offset2D(0, 0), extent);
        auto reset_2d_dynamic_state = [&]() {
            command_buffer.setViewport(0, viewport);
            command_buffer.setScissor(0, scissor);
            command_buffer.setLineWidth(1.0f);
        };

        reset_2d_dynamic_state();

        vk::Pipeline bound_pipeline = nullptr;

        auto record_advanced_draw = [&](const vulkan_advanced_draw &advanced_draw) {
            if (advanced_draw.target != target) {
                return;
            }

            vulkan_advanced_pipeline_cache_entry *pipeline_entry = get_or_create_advanced_pipeline(advanced_draw, offscreen);
            if (!pipeline_entry || !pipeline_entry->pipeline) {
                return;
            }

            vk::DescriptorSet descriptor_set = nullptr;
            if (descriptor_snapshots) {
                vulkan_shader_descriptor_snapshot descriptor_snapshot;
                if (!advanced_draw.program->prepare_descriptor_snapshot(this, advanced_draw.texture_slots, descriptor_snapshot, &advanced_draw.descriptor_state)) {
                    return;
                }

                descriptor_set = descriptor_snapshot.descriptor_set;
                descriptor_snapshots->push_back(std::move(descriptor_snapshot));
            } else {
                if (!advanced_draw.program->prepare_descriptors(this, advanced_draw.texture_slots, &advanced_draw.descriptor_state)) {
                    return;
                }

                descriptor_set = advanced_draw.program->descriptor_set();
            }

            if (pipeline_entry->pipeline.get() != bound_pipeline) {
                command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_entry->pipeline.get());
                bound_pipeline = pipeline_entry->pipeline.get();
            }

            const vk::Viewport draw_viewport = make_viewport_from_rect(
                advanced_draw.state.viewport_set
                    ? advanced_draw.state.viewport
                    : eka2l1::rect(eka2l1::vec2(0, 0), eka2l1::vec2(static_cast<int>(extent.width), static_cast<int>(extent.height))),
                extent,
                advanced_draw.state.depth_range_near,
                advanced_draw.state.depth_range_far);
            command_buffer.setViewport(0, draw_viewport);
            command_buffer.setDepthBias(
                advanced_draw.state.depth_bias_constant,
                advanced_draw.state.depth_bias_clamp,
                advanced_draw.state.depth_bias_slope);
            command_buffer.setLineWidth(physical_device_features_.wideLines
                    ? std::max(1.0f, advanced_draw.state.line_width)
                    : 1.0f);

            if (descriptor_set) {
                command_buffer.bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics,
                    pipeline_entry->pipeline_layout.get(),
                    0,
                    1,
                    &descriptor_set,
                    0,
                    nullptr);
            }

            command_buffer.setBlendConstants(advanced_draw.state.blend_color.data());

            bool has_missing_vertex_buffer = false;
            const std::vector<input_descriptor> &input_descriptors = advanced_draw_inputs(advanced_draw);
            if (!input_descriptors.empty()) {
                const std::vector<vk::VertexInputBindingDescription> bindings = vulkan_input_descriptors::binding_descriptions(input_descriptors);
                for (const vk::VertexInputBindingDescription &binding : bindings) {
                    if (binding.binding >= advanced_draw.vertex_buffers.size()) {
                        has_missing_vertex_buffer = true;
                        break;
                    }

                    vulkan_buffer *vertex_buffer = advanced_draw.vertex_buffers[binding.binding];
                    if (!vertex_buffer || !vertex_buffer->buffer_handle()) {
                        has_missing_vertex_buffer = true;
                        break;
                    }

                    const vk::Buffer buffer = vertex_buffer->buffer_handle();
                    const vk::DeviceSize offset = advanced_draw.vertex_buffer_offsets[binding.binding];
                    command_buffer.bindVertexBuffers(binding.binding, 1, &buffer, &offset);
                }
            }

            if (has_missing_vertex_buffer) {
                LOG_WARN(DRIVER_GRAPHICS, "Skipping Vulkan advanced draw with missing vertex buffer");
                return;
            }

            for_each_scissor(advanced_draw.state, extent, [&](const vk::Rect2D &draw_scissor) {
                command_buffer.setScissor(0, draw_scissor);

                if (advanced_draw.indexed) {
                    vk::IndexType index_type = vk::IndexType::eUint16;
                    if (!to_vulkan_index_type(advanced_draw.index_type, index_type)) {
                        LOG_WARN(DRIVER_GRAPHICS, "Skipping Vulkan indexed draw with unsupported index type {}", static_cast<int>(advanced_draw.index_type));
                        return;
                    }

                    if (!advanced_draw.index_buffer || !advanced_draw.index_buffer->buffer_handle()) {
                        LOG_WARN(DRIVER_GRAPHICS, "Skipping Vulkan indexed draw with no index buffer");
                        return;
                    }

                    command_buffer.bindIndexBuffer(
                        advanced_draw.index_buffer->buffer_handle(),
                        static_cast<vk::DeviceSize>(std::max(0, advanced_draw.index_offset)),
                        index_type);
                    command_buffer.drawIndexed(
                        static_cast<std::uint32_t>(std::max(0, advanced_draw.index_count)),
                        static_cast<std::uint32_t>(std::max(1, advanced_draw.instance_count)),
                        0,
                        advanced_draw.vertex_base,
                        0);
                } else {
                    command_buffer.draw(
                        static_cast<std::uint32_t>(std::max(0, advanced_draw.vertex_count)),
                        static_cast<std::uint32_t>(std::max(1, advanced_draw.instance_count)),
                        static_cast<std::uint32_t>(std::max(0, advanced_draw.first_vertex)),
                        0);
                }
            });
        };

        auto record_rectangle_draw = [&](vulkan_rectangle_draw rectangle_draw) {
            if (rectangle_draw.target != target) {
                return;
            }

            reset_2d_dynamic_state();

            vk::Pipeline selected_pipeline = rectangle_pipeline;
            if (vulkan_blend_pipeline_cache_entry *pipeline_entry = get_or_create_blend_pipeline(rectangle_draw.state)) {
                if (offscreen) {
                    selected_pipeline = offscreen_has_depth
                        ? pipeline_entry->offscreen_rectangle_pipeline.get()
                        : pipeline_entry->offscreen_no_depth_rectangle_pipeline.get();
                } else {
                    selected_pipeline = pipeline_entry->rectangle_pipeline.get();
                }
            }

            if (!selected_pipeline) {
                return;
            }

            if (selected_pipeline && (selected_pipeline != bound_pipeline)) {
                command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, selected_pipeline);
                bound_pipeline = selected_pipeline;
            }

            if (rectangle_draw.rectangle.size.x == 0) {
                rectangle_draw.rectangle.size.x = static_cast<int>(extent.width);
            }

            if (rectangle_draw.rectangle.size.y == 0) {
                rectangle_draw.rectangle.size.y = static_cast<int>(extent.height);
            }

            vulkan_rectangle_push_constants push_constants = {};
            push_constants.rectangle[0] = static_cast<float>(rectangle_draw.rectangle.top.x);
            // The baked rectangle shader maps its input Y through the legacy
            // bottom-origin projection path. Feed it a bottom-origin Y so the final Vulkan
            // framebuffer position matches bitmap draws and Symbian's top-left
            // window coordinates.
            push_constants.rectangle[1] = static_cast<float>(extent.height) - static_cast<float>(rectangle_draw.rectangle.top.y + rectangle_draw.rectangle.size.y);
            push_constants.rectangle[2] = static_cast<float>(rectangle_draw.rectangle.size.x);
            push_constants.rectangle[3] = static_cast<float>(rectangle_draw.rectangle.size.y);
            push_constants.color[0] = rectangle_draw.color[0] / 255.0f;
            push_constants.color[1] = rectangle_draw.color[1] / 255.0f;
            push_constants.color[2] = rectangle_draw.color[2] / 255.0f;
            push_constants.color[3] = rectangle_draw.color[3] / 255.0f;
            push_constants.viewport[0] = static_cast<float>(extent.width);
            push_constants.viewport[1] = static_cast<float>(extent.height);
            push_constants.viewport[2] = 0.0f;
            push_constants.viewport[3] = 0.0f;

            command_buffer.pushConstants(
                rectangle_pipeline_layout_.get(),
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0,
                sizeof(push_constants),
                &push_constants);
            command_buffer.setBlendConstants(rectangle_draw.state.blend_color.data());

            for_each_scissor(rectangle_draw.state, extent, [&](const vk::Rect2D &draw_scissor) {
                command_buffer.setScissor(0, draw_scissor);
                command_buffer.draw(6, 1, 0, 0);
            });
        };

        auto record_bitmap_draw = [&](vulkan_bitmap_draw bitmap_draw) {
            if (bitmap_draw.target != target) {
                return;
            }

            if (!bitmap_draw.texture || !bitmap_draw.texture->descriptor_set()) {
                return;
            }

            reset_2d_dynamic_state();

            const bool use_custom_upscale_shader = (bitmap_draw.flags & bitmap_draw_flag_use_upscale_shader) && !active_upscale_shader_.empty() && (active_upscale_shader_ != "Default") && (active_upscale_shader_ != "default");
            vk::Pipeline selected_pipeline = bitmap_pipeline;
            bool using_custom_upscale_pipeline = false;
            if (use_custom_upscale_shader) {
                selected_pipeline = get_or_create_upscale_pipeline(active_upscale_shader_, bitmap_draw.state, offscreen);
                using_custom_upscale_pipeline = static_cast<bool>(selected_pipeline);
            }

            if (!using_custom_upscale_pipeline) {
                selected_pipeline = bitmap_pipeline;
                if (vulkan_blend_pipeline_cache_entry *pipeline_entry = get_or_create_blend_pipeline(bitmap_draw.state)) {
                    if (offscreen) {
                        selected_pipeline = offscreen_has_depth
                            ? pipeline_entry->offscreen_bitmap_pipeline.get()
                            : pipeline_entry->offscreen_no_depth_bitmap_pipeline.get();
                    } else {
                        selected_pipeline = pipeline_entry->bitmap_pipeline.get();
                    }
                }
            }

            if (!selected_pipeline) {
                return;
            }

            if (selected_pipeline && (selected_pipeline != bound_pipeline)) {
                command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, selected_pipeline);
                bound_pipeline = selected_pipeline;
            }

            eka2l1::rect source_rect = bitmap_draw.source;
            const eka2l1::vec2 texture_size = bitmap_draw.texture->get_size();
            if (source_rect.size.x == 0) {
                source_rect.size.x = texture_size.x;
            }

            if (source_rect.size.y == 0) {
                source_rect.size.y = texture_size.y;
            }

            eka2l1::rect destination_rect = bitmap_draw.destination;
            if (destination_rect.size.x == 0) {
                destination_rect.size.x = source_rect.size.x;
            }

            if (destination_rect.size.y == 0) {
                destination_rect.size.y = source_rect.size.y;
            }

            const float radians = bitmap_draw.rotation * 0.017453292519943295769f;
            const float sine = std::sin(radians);
            const float cosine = std::cos(radians);
            const float local_corners[4][2] = {
                { 0.0f, 0.0f },
                { static_cast<float>(destination_rect.size.x), 0.0f },
                { static_cast<float>(destination_rect.size.x), static_cast<float>(destination_rect.size.y) },
                { 0.0f, static_cast<float>(destination_rect.size.y) }
            };

            vulkan_bitmap_push_constants push_constants = {};
            for (std::size_t i = 0; i < 4; i++) {
                const float relative_x = local_corners[i][0] - static_cast<float>(bitmap_draw.origin.x);
                const float relative_y = local_corners[i][1] - static_cast<float>(bitmap_draw.origin.y);
                const float rotated_x = static_cast<float>(bitmap_draw.origin.x) + (cosine * relative_x) - (sine * relative_y);
                const float rotated_y = static_cast<float>(bitmap_draw.origin.y) + (sine * relative_x) + (cosine * relative_y);
                const float x = static_cast<float>(destination_rect.top.x) + rotated_x;
                const float y = static_cast<float>(destination_rect.top.y) + rotated_y;

                push_constants.positions[i * 4 + 0] = (x / static_cast<float>(extent.width)) * 2.0f - 1.0f;
                push_constants.positions[i * 4 + 1] = (y / static_cast<float>(extent.height)) * 2.0f - 1.0f;
                push_constants.positions[i * 4 + 2] = 0.0f;
                push_constants.positions[i * 4 + 3] = 1.0f;
            }

            const float tex_width = static_cast<float>(std::max(1, texture_size.x));
            const float tex_height = static_cast<float>(std::max(1, texture_size.y));
            push_constants.uv_rect[0] = static_cast<float>(source_rect.top.x) / tex_width;
            push_constants.uv_rect[1] = static_cast<float>(source_rect.top.y) / tex_height;
            push_constants.uv_rect[2] = static_cast<float>(source_rect.top.x + source_rect.size.x) / tex_width;
            push_constants.uv_rect[3] = static_cast<float>(source_rect.top.y + source_rect.size.y) / tex_height;

            const bool flip_source_y = ((bitmap_draw.flags & bitmap_draw_flag_flip) != 0) ^ bitmap_draw.texture->framebuffer_target();
            if (flip_source_y) {
                std::swap(push_constants.uv_rect[1], push_constants.uv_rect[3]);
            }

            push_constants.color[0] = bitmap_draw.color[0];
            push_constants.color[1] = bitmap_draw.color[1];
            push_constants.color[2] = bitmap_draw.color[2];
            push_constants.color[3] = bitmap_draw.color[3];

            push_constants.options[0] = bitmap_draw.mask_texture ? 1.0f : 0.0f;
            push_constants.options[1] = (bitmap_draw.flags & bitmap_draw_flag_invert_mask) ? 1.0f : 0.0f;
            push_constants.options[2] = (bitmap_draw.flags & bitmap_draw_flag_flat_blending) ? 1.0f : 0.0f;
            if (using_custom_upscale_pipeline) {
                push_constants.options[0] = 1.0f / static_cast<float>(std::max(1, source_rect.size.x));
                push_constants.options[1] = 1.0f / static_cast<float>(std::max(1, source_rect.size.y));
                push_constants.options[2] = 1.0f / static_cast<float>(std::max(1, destination_rect.size.x));
                push_constants.options[3] = 1.0f / static_cast<float>(std::max(1, destination_rect.size.y));
            }

            const vk::DescriptorSet source_descriptor_set = bitmap_draw.texture->descriptor_set();
            const vk::DescriptorSet mask_descriptor_set = bitmap_draw.mask_texture
                ? bitmap_draw.mask_texture->descriptor_set()
                : source_descriptor_set;
            if (!source_descriptor_set || !mask_descriptor_set) {
                return;
            }

            const vk::DescriptorSet descriptor_sets[] = {
                source_descriptor_set,
                mask_descriptor_set
            };
            command_buffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                bitmap_pipeline_layout_.get(),
                0,
                static_cast<std::uint32_t>(sizeof(descriptor_sets) / sizeof(descriptor_sets[0])),
                descriptor_sets,
                0,
                nullptr);
            command_buffer.pushConstants(
                bitmap_pipeline_layout_.get(),
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0,
                sizeof(push_constants),
                &push_constants);
            command_buffer.setBlendConstants(bitmap_draw.state.blend_color.data());

            for_each_scissor(bitmap_draw.state, extent, [&](const vk::Rect2D &draw_scissor) {
                command_buffer.setScissor(0, draw_scissor);
                command_buffer.draw(6, 1, 0, 0);
            });
        };

        for (const vulkan_pending_draw &draw : pending_draws_) {
            std::visit([&](const auto &entry) {
                using draw_type = std::decay_t<decltype(entry)>;
                if constexpr (std::is_same_v<draw_type, vulkan_rectangle_draw>) {
                    record_rectangle_draw(entry);
                } else if constexpr (std::is_same_v<draw_type, vulkan_bitmap_draw>) {
                    record_bitmap_draw(entry);
                } else if constexpr (std::is_same_v<draw_type, vulkan_advanced_draw>) {
                    record_advanced_draw(entry);
                }
            },
                draw);
        }
    }

    bool vulkan_graphics_driver::render_pending_to_framebuffer(vulkan_framebuffer *target) {
        if (!target || !has_pending_framebuffer_draws(target)) {
            return true;
        }

        vulkan_texture *target_texture = target->draw_texture();
        if (!target_texture || !target_texture->image() || !target_texture->image_view()) {
            return false;
        }

        if (target_texture->vk_format() != vk::Format::eR8G8B8A8Unorm) {
            LOG_ERROR(DRIVER_GRAPHICS, "Unsupported Vulkan framebuffer color format");
            return false;
        }

        const eka2l1::vec2 target_size = target_texture->get_size();
        const vk::Extent2D extent(
            static_cast<std::uint32_t>(std::max(1, target_size.x)),
            static_cast<std::uint32_t>(std::max(1, target_size.y)));
        debug_log_pending_draws(target, "offscreen");

        const bool should_capture_offscreen = debug_options_.capture_offscreen && (debug_offscreen_target_seen_ >= debug_options_.offscreen_capture_skip) && (((debug_offscreen_target_seen_ - debug_options_.offscreen_capture_skip) % debug_options_.offscreen_capture_interval) == 0) && (debug_offscreen_capture_index_ < debug_options_.offscreen_capture_limit);

        if (should_capture_offscreen && debug_options_.trace_draws) {
            std::vector<vulkan_texture *> captured_sources;
            for (const vulkan_pending_draw &draw : pending_draws_) {
                std::visit([&](const auto &entry) {
                    using draw_type = std::decay_t<decltype(entry)>;
                    if constexpr (std::is_same_v<draw_type, vulkan_bitmap_draw>) {
                        if ((entry.target == target) && entry.texture && (entry.texture != target_texture) && (std::find(captured_sources.begin(), captured_sources.end(), entry.texture) == captured_sources.end())) {
                            const std::uint64_t source_index = (debug_offscreen_capture_index_ * 100) + captured_sources.size();
                            captured_sources.push_back(entry.texture);
                            debug_capture_texture(entry.texture, "offscreen-source", source_index);
                        }
                    }
                },
                    draw);
            }
        }

        vk::UniqueImage temporary_depth_stencil_image;
        vk::UniqueDeviceMemory temporary_depth_stencil_memory;
        vk::UniqueImageView temporary_depth_stencil_image_view;
        vk::UniqueFramebuffer framebuffer;
        vulkan_texture *depth_stencil_texture = target->depth_stencil_texture();
        bool used_attached_depth_stencil = false;
        const bool use_depth_stencil = !can_use_no_depth_offscreen(target);
        const vk::RenderPass offscreen_render_pass = use_depth_stencil
            ? offscreen_render_pass_.get()
            : offscreen_no_depth_render_pass_.get();
        const vk::Pipeline offscreen_rectangle_pipeline = use_depth_stencil
            ? offscreen_rectangle_pipeline_.get()
            : offscreen_no_depth_rectangle_pipeline_.get();
        const vk::Pipeline offscreen_bitmap_pipeline = use_depth_stencil
            ? offscreen_bitmap_pipeline_.get()
            : offscreen_no_depth_bitmap_pipeline_.get();
        try {
            if (use_depth_stencil && (depth_stencil_format_ == vk::Format::eUndefined)) {
                depth_stencil_format_ = choose_depth_stencil_format(phys_dvc_);
            }

            vk::ImageView depth_stencil_attachment;
            if (use_depth_stencil) {
                if (depth_stencil_texture && depth_stencil_texture->image_view() && (depth_stencil_texture->vk_format() == depth_stencil_format_)) {
                    depth_stencil_attachment = depth_stencil_texture->image_view();
                    used_attached_depth_stencil = true;
                } else {
                    vk::ImageCreateInfo depth_image_create_info;
                    depth_image_create_info.imageType = vk::ImageType::e2D;
                    depth_image_create_info.format = depth_stencil_format_;
                    depth_image_create_info.extent = vk::Extent3D(extent.width, extent.height, 1);
                    depth_image_create_info.mipLevels = 1;
                    depth_image_create_info.arrayLayers = 1;
                    depth_image_create_info.samples = vk::SampleCountFlagBits::e1;
                    depth_image_create_info.tiling = vk::ImageTiling::eOptimal;
                    depth_image_create_info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferDst;
                    depth_image_create_info.sharingMode = vk::SharingMode::eExclusive;
                    depth_image_create_info.initialLayout = vk::ImageLayout::eUndefined;
                    temporary_depth_stencil_image = dvc_->createImageUnique(depth_image_create_info);

                    const vk::MemoryRequirements memory_requirements = dvc_->getImageMemoryRequirements(temporary_depth_stencil_image.get());
                    vk::MemoryAllocateInfo memory_allocate_info(
                        memory_requirements.size,
                        find_memory_type(memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal));
                    temporary_depth_stencil_memory = dvc_->allocateMemoryUnique(memory_allocate_info);
                    dvc_->bindImageMemory(temporary_depth_stencil_image.get(), temporary_depth_stencil_memory.get(), 0);

                    vk::ImageViewCreateInfo depth_image_view_create_info;
                    depth_image_view_create_info.image = temporary_depth_stencil_image.get();
                    depth_image_view_create_info.viewType = vk::ImageViewType::e2D;
                    depth_image_view_create_info.format = depth_stencil_format_;
                    depth_image_view_create_info.subresourceRange = vk::ImageSubresourceRange(
                        vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
                        0,
                        1,
                        0,
                        1);
                    temporary_depth_stencil_image_view = dvc_->createImageViewUnique(depth_image_view_create_info);
                    depth_stencil_attachment = temporary_depth_stencil_image_view.get();
                }
            }

            vk::ImageView attachments[] = {
                target_texture->image_view(),
                depth_stencil_attachment
            };
            vk::FramebufferCreateInfo framebuffer_create_info;
            framebuffer_create_info.renderPass = offscreen_render_pass;
            framebuffer_create_info.attachmentCount = use_depth_stencil ? 2 : 1;
            framebuffer_create_info.pAttachments = attachments;
            framebuffer_create_info.width = extent.width;
            framebuffer_create_info.height = extent.height;
            framebuffer_create_info.layers = 1;
            framebuffer = dvc_->createFramebufferUnique(framebuffer_create_info);
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan offscreen framebuffer creation failed: {}", e.what());
            return false;
        }

        std::vector<vulkan_shader_descriptor_snapshot> descriptor_snapshots;
        const bool submitted = submit_immediate([&](vk::CommandBuffer command_buffer) {
            record_pending_texture_uploads(command_buffer);

            const vk::ImageSubresourceRange color_range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            const vk::ImageSubresourceRange depth_stencil_range(
                vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
                0,
                1,
                0,
                1);

            if (target->needs_clear()) {
                transition_texture(command_buffer, target_texture, vk::ImageLayout::eTransferDstOptimal,
                    vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTransfer);
                const vk::ClearColorValue clear_value(std::array<float, 4>{ 1.0f, 1.0f, 1.0f, 1.0f });
                command_buffer.clearColorImage(target_texture->image(), vk::ImageLayout::eTransferDstOptimal, clear_value, color_range);
            }

            transition_texture(command_buffer, target_texture, vk::ImageLayout::eColorAttachmentOptimal,
                vk::AccessFlagBits::eColorAttachmentWrite, vk::PipelineStageFlagBits::eColorAttachmentOutput);

            if (use_depth_stencil) {
                const vk::ClearDepthStencilValue default_depth_stencil_clear(1.0f, 0);
                if (used_attached_depth_stencil) {
                    if (target->needs_depth_stencil_clear()) {
                        transition_texture(command_buffer, depth_stencil_texture, vk::ImageLayout::eTransferDstOptimal,
                            vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTransfer);
                        command_buffer.clearDepthStencilImage(depth_stencil_texture->image(), vk::ImageLayout::eTransferDstOptimal,
                            default_depth_stencil_clear, depth_stencil_range);
                    }

                    transition_texture(command_buffer, depth_stencil_texture, vk::ImageLayout::eDepthStencilAttachmentOptimal,
                        vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                        vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests);
                } else {
                    vk::ImageMemoryBarrier to_transfer;
                    to_transfer.oldLayout = vk::ImageLayout::eUndefined;
                    to_transfer.newLayout = vk::ImageLayout::eTransferDstOptimal;
                    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    to_transfer.image = temporary_depth_stencil_image.get();
                    to_transfer.subresourceRange = depth_stencil_range;
                    to_transfer.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
                    command_buffer.pipelineBarrier(
                        vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eTransfer,
                        vk::DependencyFlags{},
                        nullptr,
                        nullptr,
                        to_transfer);

                    command_buffer.clearDepthStencilImage(temporary_depth_stencil_image.get(), vk::ImageLayout::eTransferDstOptimal,
                        default_depth_stencil_clear, depth_stencil_range);

                    vk::ImageMemoryBarrier to_attachment;
                    to_attachment.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
                    to_attachment.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
                    to_attachment.oldLayout = vk::ImageLayout::eTransferDstOptimal;
                    to_attachment.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                    to_attachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    to_attachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    to_attachment.image = temporary_depth_stencil_image.get();
                    to_attachment.subresourceRange = depth_stencil_range;
                    command_buffer.pipelineBarrier(
                        vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
                        vk::DependencyFlags{},
                        nullptr,
                        nullptr,
                        to_attachment);
                }
            }

            vk::RenderPassBeginInfo render_pass_begin_info;
            render_pass_begin_info.renderPass = offscreen_render_pass;
            render_pass_begin_info.framebuffer = framebuffer.get();
            render_pass_begin_info.renderArea.offset = vk::Offset2D(0, 0);
            render_pass_begin_info.renderArea.extent = extent;
            render_pass_begin_info.clearValueCount = 0;
            render_pass_begin_info.pClearValues = nullptr;

            command_buffer.beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);
            record_pending_draws(command_buffer, extent, target, offscreen_rectangle_pipeline, offscreen_bitmap_pipeline,
                true, use_depth_stencil, &descriptor_snapshots);
            command_buffer.endRenderPass();

            transition_texture(command_buffer, target_texture, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eFragmentShader);
        });

        if (!submitted) {
            complete_pending_texture_uploads();
            return false;
        }

        complete_pending_texture_uploads();
        target->mark_cleared();
        if (use_depth_stencil && used_attached_depth_stencil) {
            depth_stencil_texture->set_layout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
            target->mark_depth_stencil_cleared();
        }
        debug_offscreen_target_seen_++;
        if (should_capture_offscreen) {
            debug_capture_texture(target_texture, "offscreen", debug_offscreen_capture_index_++);
        }
        erase_pending_framebuffer_draws(target);
        return true;
    }

    bool vulkan_graphics_driver::flush_pending_framebuffer_draws() {
        std::vector<vulkan_framebuffer *> targets;

        auto append_target = [&targets](vulkan_framebuffer *target) {
            if (target && (std::find(targets.begin(), targets.end(), target) == targets.end())) {
                targets.push_back(target);
            }
        };

        for (const vulkan_pending_draw &draw : pending_draws_) {
            append_target(pending_draw_target(draw));
        }

        for (vulkan_framebuffer *target : targets) {
            if (!render_pending_to_framebuffer(target)) {
                return false;
            }
        }

        return true;
    }

    void vulkan_graphics_driver::erase_pending_framebuffer_draws(vulkan_framebuffer *target) {
        pending_draws_.erase(std::remove_if(pending_draws_.begin(), pending_draws_.end(),
                                 [target](const vulkan_pending_draw &draw) {
                                     return pending_draw_target(draw) == target;
                                 }),
            pending_draws_.end());
    }

    bool vulkan_graphics_driver::present_clear_frame() {
        if (!swapchain_ || swapchain_images_.empty() || !command_buffer_ || !render_pass_ || !rectangle_pipeline_ || !bitmap_pipeline_) {
            return false;
        }

        if (!flush_pending_framebuffer_draws()) {
            return false;
        }
        if (!flush_pending_texture_uploads()) {
            return false;
        }

        try {
            const vk::Result wait_result = dvc_->waitForFences(frame_fence_.get(), true, std::numeric_limits<std::uint64_t>::max());
            if (wait_result != vk::Result::eSuccess) {
                LOG_ERROR(DRIVER_GRAPHICS, "Waiting for Vulkan frame fence failed with result {}", vk::to_string(wait_result));
                return false;
            }

            present_descriptor_snapshots_.clear();
            dvc_->resetFences(frame_fence_.get());

            const vk::ResultValue<std::uint32_t> acquire_result = dvc_->acquireNextImageKHR(
                swapchain_.get(),
                std::numeric_limits<std::uint64_t>::max(),
                image_available_semaphore_.get(),
                nullptr);

            const std::uint32_t image_index = acquire_result.value;
            if (image_index >= swapchain_images_.size()) {
                return false;
            }

            const bool present_capture_frame = (debug_present_frame_seen_ >= debug_options_.present_frame_skip) && (((debug_present_frame_seen_ - debug_options_.present_frame_skip) % debug_options_.present_frame_interval) == 0);
            bool should_capture_present = debug_options_.capture_present && debug_swapchain_capture_supported_ && present_capture_frame && (debug_frame_index_ < debug_options_.present_frame_limit);
            vk::UniqueBuffer debug_readback_buffer;
            vk::UniqueDeviceMemory debug_readback_memory;
            if (should_capture_present) {
                bool bgra_source = false;
                should_capture_present = is_supported_debug_capture_format(swapchain_format_, bgra_source) && create_debug_readback_buffer(static_cast<vk::DeviceSize>(swapchain_extent_.width) * swapchain_extent_.height * 4, debug_readback_buffer, debug_readback_memory);
            }
            bool recorded_present_capture = false;

            command_buffer_->reset();

            vk::CommandBufferBeginInfo begin_info(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
            command_buffer_->begin(begin_info);

            const vk::ImageSubresourceRange color_range(
                vk::ImageAspectFlagBits::eColor,
                0,
                1,
                0,
                1);

            vk::ImageMemoryBarrier to_transfer;
            to_transfer.oldLayout = swapchain_image_initialized_[image_index]
                ? vk::ImageLayout::ePresentSrcKHR
                : vk::ImageLayout::eUndefined;
            to_transfer.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
            to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_transfer.image = swapchain_images_[image_index];
            to_transfer.subresourceRange = color_range;
            to_transfer.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

            command_buffer_->pipelineBarrier(
                swapchain_image_initialized_[image_index] ? vk::PipelineStageFlagBits::eBottomOfPipe : vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::DependencyFlags{},
                nullptr,
                nullptr,
                to_transfer);

            vk::ClearValue clear_values[2];
            clear_values[0].color = clear_color_;
            clear_values[1].depthStencil = vk::ClearDepthStencilValue(clear_depth_, clear_stencil_);

            vk::RenderPassBeginInfo render_pass_begin_info;
            render_pass_begin_info.renderPass = render_pass_.get();
            render_pass_begin_info.framebuffer = swapchain_framebuffers_[image_index].get();
            render_pass_begin_info.renderArea.offset = vk::Offset2D(0, 0);
            render_pass_begin_info.renderArea.extent = swapchain_extent_;
            render_pass_begin_info.clearValueCount = static_cast<std::uint32_t>(sizeof(clear_values) / sizeof(clear_values[0]));
            render_pass_begin_info.pClearValues = clear_values;

            std::vector<vulkan_shader_descriptor_snapshot> descriptor_snapshots;
            command_buffer_->beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);
            debug_log_pending_draws(nullptr, "present");
            record_pending_draws(command_buffer_.get(), swapchain_extent_, nullptr, rectangle_pipeline_.get(), bitmap_pipeline_.get(), false,
                true, &descriptor_snapshots);

            command_buffer_->endRenderPass();

            if (should_capture_present) {
                recorded_present_capture = debug_record_swapchain_capture(command_buffer_.get(), swapchain_images_[image_index],
                    debug_readback_buffer.get(), swapchain_extent_);
            }

            if (!recorded_present_capture) {
                vk::ImageMemoryBarrier to_present;
                to_present.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
                to_present.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
                to_present.newLayout = vk::ImageLayout::ePresentSrcKHR;
                to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                to_present.image = swapchain_images_[image_index];
                to_present.subresourceRange = color_range;

                command_buffer_->pipelineBarrier(
                    vk::PipelineStageFlagBits::eColorAttachmentOutput,
                    vk::PipelineStageFlagBits::eBottomOfPipe,
                    vk::DependencyFlags{},
                    nullptr,
                    nullptr,
                    to_present);
            }

            command_buffer_->end();

            const vk::Semaphore wait_semaphore = image_available_semaphore_.get();
            const vk::Semaphore signal_semaphore = render_finished_semaphore_.get();
            const vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            const vk::CommandBuffer command_buffer = command_buffer_.get();

            vk::SubmitInfo submit_info;
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &wait_semaphore;
            submit_info.pWaitDstStageMask = &wait_stage;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &command_buffer;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &signal_semaphore;

            graphics_queue_.submit(submit_info, frame_fence_.get());
            present_descriptor_snapshots_ = std::move(descriptor_snapshots);

            const vk::SwapchainKHR swapchain = swapchain_.get();
            vk::PresentInfoKHR present_info;
            present_info.waitSemaphoreCount = 1;
            present_info.pWaitSemaphores = &signal_semaphore;
            present_info.swapchainCount = 1;
            present_info.pSwapchains = &swapchain;
            present_info.pImageIndices = &image_index;

            const vk::Result present_result = graphics_queue_.presentKHR(present_info);
            swapchain_image_initialized_[image_index] = true;

            if (should_capture_present) {
                if (recorded_present_capture) {
                    const vk::Result capture_wait_result = dvc_->waitForFences(frame_fence_.get(), true, std::numeric_limits<std::uint64_t>::max());
                    if (capture_wait_result == vk::Result::eSuccess) {
                        debug_write_swapchain_capture(debug_readback_memory.get(), swapchain_extent_, "present", debug_frame_index_);
                    } else {
                        LOG_WARN(DRIVER_GRAPHICS, "Waiting for Vulkan debug present capture failed with result {}",
                            vk::to_string(capture_wait_result));
                    }
                }

                debug_frame_index_++;
            }
            debug_present_frame_seen_++;

            if (present_result == vk::Result::eSuboptimalKHR) {
                dvc_->waitIdle();
                return create_swapchain() && create_render_resources();
            }
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan present failed with error: {}", e.what());
            return false;
        }

        pending_draws_.clear();
        return true;
    }

    vulkan_graphics_driver::vulkan_graphics_driver(const window_system_info &info)
        : shared_graphics_driver(graphic_api::vulkan)
        , swapchain_format_(vk::Format::eUndefined)
        , depth_stencil_format_(vk::Format::eUndefined)
        , swapchain_extent_(0, 0)
        , graphics_queue_family_index_(std::numeric_limits<std::uint32_t>::max())
        , clear_color_(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f })
        , clear_depth_(1.0f)
        , clear_stencil_(0)
        , wsi_(info)
        , should_stop_(false)
        , active_upscale_shader_("Default")
        , active_framebuffer_(nullptr)
        , bound_read_framebuffer_(nullptr)
        , bound_draw_framebuffer_(nullptr)
        , index_buffer_(nullptr)
        , active_input_descriptors_(nullptr)
        , active_shader_program_(nullptr)
        , point_size_(1.0f)
        , line_style_(pen_style_solid)
        , debug_frame_index_(0)
        , debug_present_frame_seen_(0)
        , debug_offscreen_capture_index_(0)
        , debug_offscreen_target_seen_(0)
        , debug_swapchain_capture_supported_(false)
        , initialized_(false)
        , renderer_supported_(false) {
        vertex_buffers_.fill(nullptr);
        vertex_buffer_offsets_.fill(0);
        texture_slots_.fill(nullptr);
        list_queue_.max_pending_count_ = 128;
        configure_debug_options();

        if (!create_instance()) {
            return;
        }

        create_debug_callback();

        if (!create_surface()) {
            return;
        }

        if (!create_device()) {
            return;
        }

        if (!create_swapchain()) {
            return;
        }

        if (!create_command_resources()) {
            return;
        }

        if (!create_render_resources()) {
            return;
        }

        if (!create_sync_objects()) {
            return;
        }

        renderer_supported_ = true;

        initialized_ = true;
    }

    vulkan_graphics_driver::~vulkan_graphics_driver() {
        if (dvc_) {
            try {
                dvc_->waitIdle();
            } catch (std::exception &e) {
                LOG_WARN(DRIVER_GRAPHICS, "Waiting for Vulkan device idle during shutdown failed: {}", e.what());
            }
        }

        complete_pending_texture_uploads();
        pending_draws_.clear();
        active_framebuffer_ = nullptr;
        bound_read_framebuffer_ = nullptr;
        bound_draw_framebuffer_ = nullptr;
        bmp_textures.clear();
        graphic_objects.clear();

#if EKA2L1_PLATFORM(MACOS) || EKA2L1_PLATFORM(IOS)
        destroy_swapchain_resources();
        surface_.reset();
        destroy_vulkan_metal_layer(wsi_.render_surface);
#endif
    }

    void vulkan_graphics_driver::submit_command_list(command_list &cmd_list) {
        if ((cmd_list.size_ == 0) || !cmd_list.base_ || should_stop_) {
            delete[] cmd_list.base_;
            return;
        }

        list_queue_.push(cmd_list);
    }

    void vulkan_graphics_driver::run() {
        while (!should_stop_) {
            std::optional<command_list> list = list_queue_.pop();
            if (!list) {
                break;
            }

            for (std::size_t i = 0; i < list->size_; i++) {
                dispatch(list->base_[i]);
            }

            flush_pending_framebuffer_draws();
            flush_pending_texture_uploads();

            delete[] list->base_;
        }
    }

    void vulkan_graphics_driver::display(command &cmd) {
        const bool presented = present_clear_frame();
        if (disp_hook_) {
            disp_hook_();
        }

        finish(cmd.status_, presented ? 0 : -1);
    }

    void vulkan_graphics_driver::bind_bitmap(command &cmd) {
        if (active_framebuffer_ && !render_pending_to_framebuffer(active_framebuffer_)) {
            LOG_ERROR(DRIVER_GRAPHICS, "Failed to flush Vulkan bitmap framebuffer before binding a new target");
        }

        shared_graphics_driver::dispatch(cmd);
        active_framebuffer_ = nullptr;

        if (binding && binding->fb) {
            active_framebuffer_ = reinterpret_cast<vulkan_framebuffer *>(binding->fb.get());
        }
    }

    void vulkan_graphics_driver::draw_rectangle(command &cmd) {
        vulkan_rectangle_draw rectangle_draw;
        rectangle_draw.target = active_framebuffer_;
        unpack_u64_to_2u32(cmd.data_[0], rectangle_draw.rectangle.top.x, rectangle_draw.rectangle.top.y);
        unpack_u64_to_2u32(cmd.data_[1], rectangle_draw.rectangle.size.x, rectangle_draw.rectangle.size.y);
        rectangle_draw.color = brush_color;
        rectangle_draw.state = draw_state_;
        pending_draws_.push_back(rectangle_draw);
    }

    void vulkan_graphics_driver::draw_bitmap(command &cmd) {
        const drivers::handle draw_handle = static_cast<drivers::handle>(cmd.data_[0]);
        const drivers::handle mask_handle = static_cast<drivers::handle>(cmd.data_[1]);
        const std::uint32_t flags = static_cast<std::uint32_t>(cmd.data_[7] >> 32);

        vulkan_texture *draw_texture = nullptr;
        if (bitmap *bmp = get_bitmap(draw_handle)) {
            draw_texture = reinterpret_cast<vulkan_texture *>(bmp->tex.get());
        } else {
            draw_texture = reinterpret_cast<vulkan_texture *>(get_graphics_object(draw_handle));
        }

        if (!draw_texture) {
            LOG_ERROR(DRIVER_GRAPHICS, "Invalid Vulkan bitmap handle to draw");
            return;
        }

        vulkan_texture *mask_texture = nullptr;
        if (mask_handle) {
            if (bitmap *mask_bitmap = get_bitmap(mask_handle)) {
                mask_texture = reinterpret_cast<vulkan_texture *>(mask_bitmap->tex.get());
            } else {
                mask_texture = reinterpret_cast<vulkan_texture *>(get_graphics_object(mask_handle));
            }
        }

        if (mask_handle && !mask_texture) {
            LOG_WARN(DRIVER_GRAPHICS, "Vulkan bitmap mask handle was provided but invalid");
        }

        vulkan_bitmap_draw bitmap_draw = {};
        bitmap_draw.target = active_framebuffer_;
        bitmap_draw.texture = draw_texture;
        bitmap_draw.mask_texture = mask_texture;
        unpack_u64_to_2u32(cmd.data_[2], bitmap_draw.destination.top.x, bitmap_draw.destination.top.y);
        unpack_u64_to_2u32(cmd.data_[3], bitmap_draw.destination.size.x, bitmap_draw.destination.size.y);
        unpack_u64_to_2u32(cmd.data_[4], bitmap_draw.source.top.x, bitmap_draw.source.top.y);
        unpack_u64_to_2u32(cmd.data_[5], bitmap_draw.source.size.x, bitmap_draw.source.size.y);
        unpack_u64_to_2u32(cmd.data_[6], bitmap_draw.origin.x, bitmap_draw.origin.y);

        const std::uint32_t rotation_bits = static_cast<std::uint32_t>(cmd.data_[7]);
        std::memcpy(&bitmap_draw.rotation, &rotation_bits, sizeof(bitmap_draw.rotation));
        bitmap_draw.flags = flags;
        bitmap_draw.color = brush_color;
        if ((flags & bitmap_draw_flag_use_brush) == 0) {
            bitmap_draw.color = { 255.0f, 255.0f, 255.0f, 255.0f };
        }
        bitmap_draw.state = draw_state_;

        pending_draws_.push_back(bitmap_draw);
    }

    void vulkan_graphics_driver::draw_line_segment(const eka2l1::point &start, const eka2l1::point &end) {
        if (line_style_ == pen_style_none) {
            return;
        }

        const std::uint32_t bit_pattern = pen_style_to_bit_pattern(line_style_);
        if (bit_pattern == 0) {
            LOG_WARN(DRIVER_GRAPHICS, "Unrecognised Vulkan pen style {}", static_cast<int>(line_style_));
            return;
        }

        const int delta_x = end.x - start.x;
        const int delta_y = end.y - start.y;
        const int steps = std::max(std::abs(delta_x), std::abs(delta_y));
        const float stroke_size = std::max(1.0f, point_size_);
        const int rect_size = std::max(1, static_cast<int>(std::ceil(stroke_size)));
        const int half_size = rect_size / 2;

        auto queue_point = [&](const float x, const float y, const int step) {
            if (((bit_pattern >> (step & 0xF)) & 1U) == 0) {
                return;
            }

            vulkan_rectangle_draw rectangle_draw;
            rectangle_draw.target = active_framebuffer_;
            rectangle_draw.rectangle.top.x = static_cast<int>(std::round(x)) - half_size;
            rectangle_draw.rectangle.top.y = static_cast<int>(std::round(y)) - half_size;
            rectangle_draw.rectangle.size.x = rect_size;
            rectangle_draw.rectangle.size.y = rect_size;
            rectangle_draw.color = brush_color;
            rectangle_draw.state = draw_state_;
            pending_draws_.push_back(rectangle_draw);
        };

        if (steps == 0) {
            queue_point(static_cast<float>(start.x), static_cast<float>(start.y), 0);
            return;
        }

        const float step_x = static_cast<float>(delta_x) / static_cast<float>(steps);
        const float step_y = static_cast<float>(delta_y) / static_cast<float>(steps);
        for (int i = 0; i <= steps; i++) {
            queue_point(static_cast<float>(start.x) + step_x * i, static_cast<float>(start.y) + step_y * i, i);
        }
    }

    void vulkan_graphics_driver::draw_line(command &cmd) {
        eka2l1::point start;
        eka2l1::point end;
        unpack_u64_to_2u32(cmd.data_[0], start.x, start.y);
        unpack_u64_to_2u32(cmd.data_[1], end.x, end.y);
        draw_line_segment(start, end);
    }

    void vulkan_graphics_driver::draw_polygon(command &cmd) {
        const std::size_t point_count = static_cast<std::size_t>(cmd.data_[0]);
        eka2l1::point *point_list = reinterpret_cast<eka2l1::point *>(cmd.data_[1]);
        if (!point_list || (point_count < 2)) {
            delete[] point_list;
            return;
        }

        for (std::size_t i = 0; i + 1 < point_count; i++) {
            draw_line_segment(point_list[i], point_list[i + 1]);
        }

        delete[] point_list;
    }

    void vulkan_graphics_driver::set_point_size(command &cmd) {
        point_size_ = static_cast<float>(static_cast<std::uint8_t>(cmd.data_[0]));
    }

    void vulkan_graphics_driver::set_pen_style(command &cmd) {
        line_style_ = static_cast<pen_style>(cmd.data_[0]);
    }

    void vulkan_graphics_driver::clip_rect(command &cmd) {
        eka2l1::rect clip_rect;
        unpack_u64_to_2u32(cmd.data_[0], clip_rect.top.x, clip_rect.top.y);
        unpack_u64_to_2u32(cmd.data_[1], clip_rect.size.x, clip_rect.size.y);

        draw_state_.clip_rects.clear();
        draw_state_.clip_rects.push_back(clip_rect);
    }

    void vulkan_graphics_driver::clip_region(command &cmd) {
        const std::uint64_t rect_count = cmd.data_[0];
        eka2l1::rect *rects = reinterpret_cast<eka2l1::rect *>(cmd.data_[1]);

        float scale = 0.0f;
        float unused = 0.0f;
        unpack_to_two_floats(cmd.data_[2], scale, unused);

        draw_state_.clip_rects.clear();
        if (rects && rect_count) {
            draw_state_.clip_rects.reserve(static_cast<std::size_t>(rect_count));
            for (std::uint64_t i = 0; i < rect_count; i++) {
                eka2l1::rect next_rect = rects[i];
                next_rect.scale(scale);
                if (next_rect.valid()) {
                    draw_state_.clip_rects.push_back(next_rect);
                }
            }
        }

        delete[] rects;
        draw_state_.clipping_enabled = !draw_state_.clip_rects.empty();
    }

    void vulkan_graphics_driver::set_feature(command &cmd) {
        drivers::graphics_feature feature = drivers::graphics_feature::blend;
        bool enable = true;
        unpack_u64_to_2u32(cmd.data_[0], feature, enable);

        switch (feature) {
        case drivers::graphics_feature::clipping:
            draw_state_.clipping_enabled = enable;
            break;

        case drivers::graphics_feature::blend:
            draw_state_.blend_enabled = enable;
            break;

        case drivers::graphics_feature::cull:
            draw_state_.cull_enabled = enable;
            break;

        case drivers::graphics_feature::depth_test:
            draw_state_.depth_test_enabled = enable;
            break;

        case drivers::graphics_feature::stencil_test:
            draw_state_.stencil_test_enabled = enable;
            break;

        case drivers::graphics_feature::polygon_offset_fill:
            draw_state_.depth_bias_enabled = enable;
            break;

        case drivers::graphics_feature::line_smooth:
            draw_state_.line_smooth_enabled = enable;
            break;

        case drivers::graphics_feature::multisample:
            draw_state_.multisample_enabled = enable;
            break;

        case drivers::graphics_feature::sample_alpha_to_coverage:
            draw_state_.sample_alpha_to_coverage_enabled = enable;
            break;

        case drivers::graphics_feature::sample_alpha_to_one:
            draw_state_.sample_alpha_to_one_enabled = enable;
            break;

        case drivers::graphics_feature::sample_coverage:
            draw_state_.sample_coverage_enabled = enable;
            break;

        case drivers::graphics_feature::dither:
            draw_state_.dither_enabled = enable;
            break;

        default:
            break;
        }
    }

    void vulkan_graphics_driver::set_viewport(command &cmd) {
        eka2l1::rect viewport;
        unpack_u64_to_2u32(cmd.data_[0], viewport.top.x, viewport.top.y);
        unpack_u64_to_2u32(cmd.data_[1], viewport.size.x, viewport.size.y);
        draw_state_.viewport = viewport;
        draw_state_.viewport_set = true;
        shared_graphics_driver::set_viewport(viewport);
    }

    void vulkan_graphics_driver::blend_formula(command &cmd) {
        unpack_u64_to_2u32(cmd.data_[0], draw_state_.rgb_blend_equation, draw_state_.alpha_blend_equation);
        unpack_u64_to_2u32(cmd.data_[1], draw_state_.rgb_source_factor, draw_state_.rgb_dest_factor);
        unpack_u64_to_2u32(cmd.data_[2], draw_state_.alpha_source_factor, draw_state_.alpha_dest_factor);
    }

    void vulkan_graphics_driver::set_color_mask(command &cmd) {
        draw_state_.color_write_mask = static_cast<std::uint8_t>(cmd.data_[0] & 0xF);
    }

    void vulkan_graphics_driver::set_depth_func(command &cmd) {
        draw_state_.depth_compare = static_cast<condition_func>(cmd.data_[0]);
    }

    void vulkan_graphics_driver::set_depth_mask(command &cmd) {
        draw_state_.depth_write_enabled = (static_cast<std::uint32_t>(cmd.data_[0]) != 0);
    }

    void vulkan_graphics_driver::set_depth_bias(command &cmd) {
        float unused = 0.0f;
        unpack_to_two_floats(cmd.data_[0], draw_state_.depth_bias_constant, draw_state_.depth_bias_slope);
        unpack_to_two_floats(cmd.data_[1], draw_state_.depth_bias_clamp, unused);
    }

    void vulkan_graphics_driver::set_depth_range(command &cmd) {
        unpack_to_two_floats(cmd.data_[0], draw_state_.depth_range_near, draw_state_.depth_range_far);
    }

    void vulkan_graphics_driver::set_line_width(command &cmd) {
        const std::uint32_t width_bits = static_cast<std::uint32_t>(cmd.data_[0]);
        float width = 1.0f;
        std::memcpy(&width, &width_bits, sizeof(width));
        draw_state_.line_width = std::max(1.0f, width);
    }

    void vulkan_graphics_driver::set_blend_colour(command &cmd) {
        unpack_to_two_floats(cmd.data_[0], draw_state_.blend_color[0], draw_state_.blend_color[1]);
        unpack_to_two_floats(cmd.data_[1], draw_state_.blend_color[2], draw_state_.blend_color[3]);
    }

    void vulkan_graphics_driver::set_stencil_pass_condition(command &cmd) {
        rendering_face face_to_operate = rendering_face::back_and_front;
        condition_func pass_func = condition_func::always;
        std::int32_t reference = 0;
        std::uint32_t mask = 0xFFFFFFFF;

        unpack_u64_to_2u32(cmd.data_[0], face_to_operate, pass_func);
        unpack_u64_to_2u32(cmd.data_[1], reference, mask);
        update_stencil_faces(draw_state_, face_to_operate, [&](vulkan_stencil_face_state &face_state) {
            face_state.compare = pass_func;
            face_state.reference = reference;
            face_state.compare_mask = mask;
        });
    }

    void vulkan_graphics_driver::set_stencil_action(command &cmd) {
        rendering_face face_to_operate = rendering_face::back_and_front;
        stencil_action on_stencil_fail = stencil_action::keep;
        stencil_action on_stencil_pass_depth_fail = stencil_action::keep;
        stencil_action on_stencil_depth_pass = stencil_action::keep;

        unpack_u64_to_2u32(cmd.data_[0], face_to_operate, on_stencil_fail);
        unpack_u64_to_2u32(cmd.data_[1], on_stencil_pass_depth_fail, on_stencil_depth_pass);
        update_stencil_faces(draw_state_, face_to_operate, [&](vulkan_stencil_face_state &face_state) {
            face_state.stencil_fail = on_stencil_fail;
            face_state.depth_fail = on_stencil_pass_depth_fail;
            face_state.depth_pass = on_stencil_depth_pass;
        });
    }

    void vulkan_graphics_driver::set_stencil_mask(command &cmd) {
        rendering_face face_to_operate = rendering_face::back_and_front;
        std::uint32_t mask = 0xFFFFFFFF;

        unpack_u64_to_2u32(cmd.data_[0], face_to_operate, mask);
        update_stencil_faces(draw_state_, face_to_operate, [&](vulkan_stencil_face_state &face_state) {
            face_state.write_mask = mask;
        });
    }

    void vulkan_graphics_driver::set_front_face_rule(command &cmd) {
        draw_state_.front_face_rule = static_cast<rendering_face_determine_rule>(cmd.data_[0]);
    }

    void vulkan_graphics_driver::set_cull_face(command &cmd) {
        draw_state_.cull_face = static_cast<rendering_face>(cmd.data_[0]);
    }

    void vulkan_graphics_driver::bind_framebuffer(command &cmd) {
        if (!flush_pending_framebuffer_draws()) {
            LOG_ERROR(DRIVER_GRAPHICS, "Failed to flush Vulkan framebuffer draws before binding framebuffer");
        }

        const drivers::handle handle = static_cast<drivers::handle>(cmd.data_[0]);
        const drivers::framebuffer_bind_type bind_type = static_cast<drivers::framebuffer_bind_type>(cmd.data_[1]);
        if (handle == 0) {
            active_framebuffer_ = nullptr;
            bound_read_framebuffer_ = nullptr;
            bound_draw_framebuffer_ = nullptr;
            return;
        }

        vulkan_framebuffer *framebuffer = reinterpret_cast<vulkan_framebuffer *>(get_graphics_object(handle));
        if (!framebuffer) {
            LOG_ERROR(DRIVER_GRAPHICS, "Invalid Vulkan framebuffer handle to bind");
            return;
        }

        framebuffer->bind(this, bind_type);
        if ((bind_type == framebuffer_bind_draw) || (bind_type == framebuffer_bind_read_draw)) {
            active_framebuffer_ = framebuffer;
        }
    }

    void vulkan_graphics_driver::set_texture_anisotrophy(command &cmd) {
        if (!support_extension(graphics_driver_extension_anisotrophy_filtering)) {
            return;
        }

        const drivers::handle handle = static_cast<drivers::handle>(cmd.data_[0]);
        float requested_anisotropy = 1.0f;
        float unused = 0.0f;
        unpack_to_two_floats(cmd.data_[1], requested_anisotropy, unused);

        float max_anisotropy = 1.0f;
        query_extension_value(graphics_driver_extension_query_max_texture_max_anisotrophy, &max_anisotropy);
        requested_anisotropy = std::clamp(requested_anisotropy, 1.0f, max_anisotropy);

        vulkan_texture *texture = nullptr;
        if (handle & HANDLE_BITMAP) {
            if (bitmap *bmp = get_bitmap(handle)) {
                texture = reinterpret_cast<vulkan_texture *>(bmp->tex.get());
            }
        } else {
            texture = reinterpret_cast<vulkan_texture *>(get_graphics_object(handle));
        }

        if (texture) {
            texture->set_anisotropy(this, requested_anisotropy);
        }
    }

    void vulkan_graphics_driver::bind_texture(command &cmd) {
        const drivers::handle handle = static_cast<drivers::handle>(cmd.data_[0]);
        const int binding = static_cast<int>(cmd.data_[1]);
        if ((binding < 0) || (binding >= static_cast<int>(texture_slots_.size()))) {
            LOG_WARN(DRIVER_GRAPHICS, "Ignoring Vulkan texture bind outside slot range {}", binding);
            return;
        }

        vulkan_texture *texture = nullptr;
        if (handle & HANDLE_BITMAP) {
            if (bitmap *bmp = get_bitmap(handle)) {
                texture = reinterpret_cast<vulkan_texture *>(bmp->tex.get());
            }
        } else {
            texture = reinterpret_cast<vulkan_texture *>(get_graphics_object(handle));
        }

        texture_slots_[binding] = texture;
    }

    void vulkan_graphics_driver::bind_vertex_buffers(command &cmd) {
        drivers::handle *handles = reinterpret_cast<drivers::handle *>(cmd.data_[0]);
        std::size_t *offsets = reinterpret_cast<std::size_t *>(cmd.data_[2]);
        std::uint32_t starting_slot = 0;
        std::uint32_t count = 0;
        unpack_u64_to_2u32(cmd.data_[1], starting_slot, count);

        if (!handles) {
            return;
        }

        if ((starting_slot + count) > vertex_buffers_.size()) {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan vertex buffer bind range exceeds maximum (startSlot={}, count={})",
                starting_slot, count);
            delete[] handles;
            delete[] offsets;
            return;
        }

        for (std::uint32_t i = 0; i < count; i++) {
            vertex_buffers_[starting_slot + i] = reinterpret_cast<vulkan_buffer *>(get_graphics_object(handles[i]));
            vertex_buffer_offsets_[starting_slot + i] = offsets ? static_cast<vk::DeviceSize>(offsets[i]) : 0;
        }

        delete[] handles;
        delete[] offsets;
    }

    void vulkan_graphics_driver::bind_index_buffer(command &cmd) {
        const drivers::handle handle = static_cast<drivers::handle>(cmd.data_[0]);
        index_buffer_ = reinterpret_cast<vulkan_buffer *>(get_graphics_object(handle));
    }

    void vulkan_graphics_driver::bind_input_descriptor(command &cmd) {
        const drivers::handle handle = static_cast<drivers::handle>(cmd.data_[0]);
        active_input_descriptors_ = reinterpret_cast<vulkan_input_descriptors *>(get_graphics_object(handle));
    }

    void vulkan_graphics_driver::draw_array(command &cmd) {
        graphics_primitive_mode primitive_mode = graphics_primitive_mode::triangles;
        std::int32_t first = 0;
        std::int32_t count = 0;
        std::int32_t instance_count = 0;

        unpack_u64_to_2u32(cmd.data_[0], primitive_mode, first);
        unpack_u64_to_2u32(cmd.data_[1], count, instance_count);

        if (!active_shader_program_) {
            static bool warned = false;
            if (!warned) {
                LOG_WARN(DRIVER_GRAPHICS, "Skipping Vulkan draw arrays with no active shader program");
                warned = true;
            }
            return;
        }

        vulkan_advanced_draw draw;
        draw.target = active_framebuffer_;
        draw.program = active_shader_program_;
        draw.input_descriptors = active_input_descriptors_;
        if (active_input_descriptors_) {
            draw.input_descriptor_snapshot = active_input_descriptors_->inputs();
        }
        active_shader_program_->capture_descriptor_state(draw.descriptor_state);
        draw.vertex_buffers = vertex_buffers_;
        draw.vertex_buffer_offsets = vertex_buffer_offsets_;
        draw.texture_slots = texture_slots_;
        draw.index_buffer = index_buffer_;
        draw.state = draw_state_;
        draw.primitive_mode = primitive_mode;
        draw.first_vertex = first;
        draw.vertex_count = count;
        draw.instance_count = (instance_count == 0) ? 1 : instance_count;
        draw.indexed = false;
        pending_draws_.push_back(draw);
    }

    void vulkan_graphics_driver::draw_indexed(command &cmd) {
        graphics_primitive_mode primitive_mode = graphics_primitive_mode::triangles;
        int count = 0;
        data_format index_type = data_format::word;
        int index_offset = 0;
        const int vertex_base = static_cast<int>(cmd.data_[2]);

        unpack_u64_to_2u32(cmd.data_[0], primitive_mode, count);
        unpack_u64_to_2u32(cmd.data_[1], index_type, index_offset);

        if (!active_shader_program_) {
            static bool warned = false;
            if (!warned) {
                LOG_WARN(DRIVER_GRAPHICS, "Skipping Vulkan indexed draw with no active shader program");
                warned = true;
            }
            return;
        }

        vulkan_advanced_draw draw;
        draw.target = active_framebuffer_;
        draw.program = active_shader_program_;
        draw.input_descriptors = active_input_descriptors_;
        if (active_input_descriptors_) {
            draw.input_descriptor_snapshot = active_input_descriptors_->inputs();
        }
        active_shader_program_->capture_descriptor_state(draw.descriptor_state);
        draw.vertex_buffers = vertex_buffers_;
        draw.vertex_buffer_offsets = vertex_buffer_offsets_;
        draw.texture_slots = texture_slots_;
        draw.index_buffer = index_buffer_;
        draw.state = draw_state_;
        draw.primitive_mode = primitive_mode;
        draw.index_type = index_type;
        draw.index_count = count;
        draw.index_offset = index_offset;
        draw.vertex_base = vertex_base;
        draw.instance_count = 1;
        draw.indexed = true;
        pending_draws_.push_back(draw);
    }

    void vulkan_graphics_driver::set_uniform(command &cmd) {
        std::uint8_t *data = reinterpret_cast<std::uint8_t *>(cmd.data_[1]);
        if (active_shader_program_ && data) {
            int binding = 0;
            drivers::shader_var_type var_type = drivers::shader_var_type::none;
            unpack_u64_to_2u32(cmd.data_[0], binding, var_type);
            active_shader_program_->set_uniform_value(binding, data, static_cast<std::size_t>(cmd.data_[2]));
        }

        delete[] data;
    }

    void vulkan_graphics_driver::set_texture_for_shader(command &cmd) {
        if (!active_shader_program_) {
            return;
        }

        std::int32_t texture_slot = 0;
        std::int32_t shader_binding = 0;
        unpack_u64_to_2u32(cmd.data_[0], texture_slot, shader_binding);
        active_shader_program_->set_texture_binding(shader_binding, texture_slot);
    }

    void vulkan_graphics_driver::read_bitmap(command &cmd) {
        const drivers::handle handle = static_cast<drivers::handle>(cmd.data_[0]);
        bitmap *bmp = get_bitmap(handle);
        if (!bmp || !bmp->tex) {
            finish(cmd.status_, 0);
            return;
        }

        if (!flush_pending_framebuffer_draws()) {
            finish(cmd.status_, 0);
            return;
        }

        eka2l1::point pos(0, 0);
        eka2l1::object_size size(0, 0);
        unpack_u64_to_2u32(cmd.data_[1], pos.x, pos.y);
        unpack_u64_to_2u32(cmd.data_[2], size.x, size.y);

        texture_format target_format = texture_format::rgba;
        texture_data_type target_data_type = texture_data_type::ubyte;
        const std::uint32_t bpp = static_cast<std::uint32_t>(cmd.data_[3]);

        switch (bpp) {
        case 8:
        case 24:
        case 32:
            break;

        case 12:
            target_format = texture_format::rgba4;
            target_data_type = texture_data_type::ushort_4_4_4_4;
            break;

        case 16:
            target_format = texture_format::rgb;
            target_data_type = texture_data_type::ushort_5_6_5;
            break;

        default:
            LOG_ERROR(DRIVER_GRAPHICS, "Unsupported BPP type to read Vulkan bitmap from (value={})", bpp);
            finish(cmd.status_, 0);
            return;
        }

        std::uint8_t *ptr = reinterpret_cast<std::uint8_t *>(cmd.data_[4]);
        if (!ptr) {
            finish(cmd.status_, 0);
            return;
        }

        vulkan_texture *texture = reinterpret_cast<vulkan_texture *>(bmp->tex.get());
        finish(cmd.status_, texture->read_data(this, target_format, target_data_type, pos, size, ptr));
    }

    void vulkan_graphics_driver::read_framebuffer(command &cmd) {
        const drivers::handle handle = static_cast<drivers::handle>(cmd.data_[0]);
        if (handle == 0) {
            LOG_WARN(DRIVER_GRAPHICS, "Vulkan swapchain framebuffer readback is not implemented yet");
            finish(cmd.status_, 0);
            return;
        }

        vulkan_framebuffer *framebuffer = reinterpret_cast<vulkan_framebuffer *>(get_graphics_object(handle));
        if (!framebuffer) {
            finish(cmd.status_, 0);
            return;
        }

        if (!flush_pending_framebuffer_draws()) {
            finish(cmd.status_, 0);
            return;
        }

        texture_format target_format = static_cast<drivers::texture_format>(static_cast<std::uint32_t>(cmd.data_[1]));
        texture_data_type target_data_type = static_cast<drivers::texture_data_type>(static_cast<std::uint32_t>(cmd.data_[1] >> 32));

        eka2l1::point pos;
        eka2l1::object_size size;
        unpack_u64_to_2u32(cmd.data_[2], pos.x, pos.y);
        unpack_u64_to_2u32(cmd.data_[3], size.x, size.y);

        std::uint8_t *ptr = reinterpret_cast<std::uint8_t *>(cmd.data_[4]);
        if (!ptr) {
            finish(cmd.status_, 0);
            return;
        }

        framebuffer->bind(this, framebuffer_bind_read);
        const bool result = framebuffer->read(target_format, target_data_type, pos, size, ptr);
        framebuffer->unbind(this);
        finish(cmd.status_, result);
    }

    void vulkan_graphics_driver::dispatch(command &cmd) {
        switch (cmd.opcode_) {
        case graphics_driver_display: {
            display(cmd);
            break;
        }

        case graphics_driver_clear: {
            float clear_values[6];
            const std::uint8_t clear_bits = static_cast<std::uint8_t>(cmd.data_[3]);
            const bool clear_color = clear_bits & draw_buffer_bit_color_buffer;
            const bool clear_depth = clear_bits & draw_buffer_bit_depth_buffer;
            const bool clear_stencil = clear_bits & draw_buffer_bit_stencil_buffer;

            unpack_to_two_floats(cmd.data_[0], clear_values[0], clear_values[1]);
            unpack_to_two_floats(cmd.data_[1], clear_values[2], clear_values[3]);
            unpack_to_two_floats(cmd.data_[2], clear_values[4], clear_values[5]);

            if (clear_color) {
                clear_color_ = vk::ClearColorValue(std::array<float, 4>{
                    clear_values[0],
                    clear_values[1],
                    clear_values[2],
                    clear_values[3] });
            }

            if (clear_depth) {
                clear_depth_ = clear_values[4];
            }

            if (clear_stencil) {
                clear_stencil_ = static_cast<std::uint32_t>(std::clamp(clear_values[5], 0.0f, 1.0f) * 255.0f);
            }

            if (active_framebuffer_ && (clear_color || clear_depth || clear_stencil)) {
                if (!flush_pending_framebuffer_draws()) {
                    LOG_ERROR(DRIVER_GRAPHICS, "Failed to flush Vulkan framebuffer draws before clear");
                    break;
                }

                vulkan_texture *target_texture = active_framebuffer_->draw_texture();
                vulkan_texture *depth_stencil_texture = active_framebuffer_->depth_stencil_texture();
                const bool submitted = submit_immediate([&](vk::CommandBuffer command_buffer) {
                    if (clear_color && target_texture && target_texture->image()) {
                        const vk::ImageSubresourceRange color_range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
                        transition_texture(command_buffer, target_texture, vk::ImageLayout::eTransferDstOptimal,
                            vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTransfer);
                        command_buffer.clearColorImage(target_texture->image(), vk::ImageLayout::eTransferDstOptimal,
                            clear_color_, color_range);
                        transition_texture(command_buffer, target_texture, vk::ImageLayout::eShaderReadOnlyOptimal,
                            vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eFragmentShader);
                    }

                    if ((clear_depth || clear_stencil) && depth_stencil_texture && depth_stencil_texture->image()) {
                        vk::ImageAspectFlags clear_aspect = {};
                        if (clear_depth) {
                            clear_aspect |= vk::ImageAspectFlagBits::eDepth;
                        }

                        if (clear_stencil) {
                            clear_aspect |= vk::ImageAspectFlagBits::eStencil;
                        }

                        const vk::ImageSubresourceRange depth_stencil_range(clear_aspect, 0, 1, 0, 1);
                        const vk::ClearDepthStencilValue depth_stencil_clear(clear_depth_, clear_stencil_);
                        transition_texture(command_buffer, depth_stencil_texture, vk::ImageLayout::eTransferDstOptimal,
                            vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTransfer);
                        command_buffer.clearDepthStencilImage(depth_stencil_texture->image(), vk::ImageLayout::eTransferDstOptimal,
                            depth_stencil_clear, depth_stencil_range);
                        transition_texture(command_buffer, depth_stencil_texture, vk::ImageLayout::eDepthStencilAttachmentOptimal,
                            vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                            vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests);
                    }
                });

                if (submitted) {
                    if (clear_color) {
                        active_framebuffer_->mark_cleared();
                    }

                    if (clear_depth || clear_stencil) {
                        active_framebuffer_->mark_depth_stencil_cleared();
                    }
                }
            }

            break;
        }

        case graphics_driver_clip_rect:
        case graphics_driver_clip_bitmap_rect: {
            clip_rect(cmd);
            break;
        }

        case graphics_driver_clip_region: {
            clip_region(cmd);
            break;
        }

        case graphics_driver_set_feature: {
            set_feature(cmd);
            break;
        }

        case graphics_driver_set_viewport:
        case graphics_driver_set_bitmap_viewport: {
            set_viewport(cmd);
            break;
        }

        case graphics_driver_blend_formula: {
            blend_formula(cmd);
            break;
        }

        case graphics_driver_draw_bitmap: {
            draw_bitmap(cmd);
            break;
        }

        case graphics_driver_draw_rectangle: {
            draw_rectangle(cmd);
            break;
        }

        case graphics_driver_draw_line: {
            draw_line(cmd);
            break;
        }

        case graphics_driver_draw_polygon: {
            draw_polygon(cmd);
            break;
        }

        case graphics_driver_set_point_size: {
            set_point_size(cmd);
            break;
        }

        case graphics_driver_set_pen_style: {
            set_pen_style(cmd);
            break;
        }

        case graphics_driver_bind_bitmap: {
            bind_bitmap(cmd);
            break;
        }

        case graphics_driver_read_bitmap: {
            read_bitmap(cmd);
            break;
        }

        case graphics_driver_read_framebuffer: {
            read_framebuffer(cmd);
            break;
        }

        case graphics_driver_bind_framebuffer: {
            bind_framebuffer(cmd);
            break;
        }

        case graphics_driver_set_texture_anisotrophy: {
            set_texture_anisotrophy(cmd);
            break;
        }

        case graphics_driver_bind_vertex_buffers: {
            bind_vertex_buffers(cmd);
            break;
        }

        case graphics_driver_bind_index_buffer: {
            bind_index_buffer(cmd);
            break;
        }

        case graphics_driver_bind_input_descriptor: {
            bind_input_descriptor(cmd);
            break;
        }

        case graphics_driver_draw_array: {
            draw_array(cmd);
            break;
        }

        case graphics_driver_draw_indexed: {
            draw_indexed(cmd);
            break;
        }

        case graphics_driver_set_uniform: {
            set_uniform(cmd);
            break;
        }

        case graphics_driver_set_texture_for_shader: {
            set_texture_for_shader(cmd);
            break;
        }

        case graphics_driver_bind_texture: {
            bind_texture(cmd);
            break;
        }

        case graphics_driver_set_color_mask: {
            set_color_mask(cmd);
            break;
        }

        case graphics_driver_set_depth_func:
        case graphics_driver_depth_pass_condition: {
            set_depth_func(cmd);
            break;
        }

        case graphics_driver_depth_set_mask: {
            set_depth_mask(cmd);
            break;
        }

        case graphics_driver_set_depth_bias: {
            set_depth_bias(cmd);
            break;
        }

        case graphics_driver_set_depth_range: {
            set_depth_range(cmd);
            break;
        }

        case graphics_driver_set_line_width: {
            set_line_width(cmd);
            break;
        }

        case graphics_driver_set_blend_colour: {
            set_blend_colour(cmd);
            break;
        }

        case graphics_driver_stencil_pass_condition: {
            set_stencil_pass_condition(cmd);
            break;
        }

        case graphics_driver_stencil_set_action: {
            set_stencil_action(cmd);
            break;
        }

        case graphics_driver_stencil_set_mask: {
            set_stencil_mask(cmd);
            break;
        }

        case graphics_driver_set_front_face_rule: {
            set_front_face_rule(cmd);
            break;
        }

        case graphics_driver_cull_face: {
            set_cull_face(cmd);
            break;
        }

        case graphics_driver_backup_state: {
            backup_draw_state_ = draw_state_;
            break;
        }

        case graphics_driver_restore_state: {
            draw_state_ = backup_draw_state_;
            break;
        }

        case graphics_driver_update_bitmap:
        case graphics_driver_update_texture: {
            flush_pending_framebuffer_draws();
            shared_graphics_driver::dispatch(cmd);
            active_framebuffer_ = binding && binding->fb
                ? reinterpret_cast<vulkan_framebuffer *>(binding->fb.get())
                : nullptr;
            break;
        }

        case graphics_driver_set_swapchain_size:
        case graphics_driver_set_ortho_size:
        case graphics_driver_set_brush_color:
        case graphics_driver_create_bitmap:
        case graphics_driver_destroy_bitmap:
        case graphics_driver_resize_bitmap: {
            flush_pending_framebuffer_draws();
            flush_pending_texture_uploads();
            shared_graphics_driver::dispatch(cmd);
            active_framebuffer_ = binding && binding->fb
                ? reinterpret_cast<vulkan_framebuffer *>(binding->fb.get())
                : nullptr;
            break;
        }

        case graphics_driver_create_texture:
        case graphics_driver_destroy_object:
        case graphics_driver_create_shader_module:
        case graphics_driver_create_shader_program:
        case graphics_driver_create_buffer:
        case graphics_driver_update_buffer:
        case graphics_driver_create_renderbuffer:
        case graphcis_driver_create_framebuffer:
        case graphics_driver_create_input_descriptor:
        case graphics_driver_set_framebuffer_color_buffer:
        case graphics_driver_set_framebuffer_depth_stencil_buffer:
        case graphics_driver_use_program:
        case graphics_driver_set_texture_filter:
        case graphics_driver_set_texture_wrap:
        case graphics_driver_set_swizzle:
        case graphics_driver_generate_mips:
        case graphics_driver_set_max_mip_level: {
            if ((cmd.opcode_ == graphics_driver_destroy_object) || (cmd.opcode_ == graphics_driver_create_renderbuffer) || (cmd.opcode_ == graphcis_driver_create_framebuffer) || (cmd.opcode_ == graphics_driver_set_framebuffer_color_buffer) || (cmd.opcode_ == graphics_driver_set_framebuffer_depth_stencil_buffer)) {
                flush_pending_framebuffer_draws();
                flush_pending_texture_uploads();
            }
            shared_graphics_driver::dispatch(cmd);
            break;
        }

        default: {
            LOG_ERROR(DRIVER_GRAPHICS, "Vulkan command {} is not implemented yet", static_cast<std::uint32_t>(cmd.opcode_));
            finish(cmd.status_, -1);
            break;
        }
        }
    }

    void vulkan_graphics_driver::abort() {
        list_queue_.abort();
        should_stop_ = true;
        cond_.notify_all();
    }

    bool vulkan_graphics_driver::aborted() const {
        return should_stop_.load();
    }

    void vulkan_graphics_driver::bind_swapchain_framebuf() {
        active_framebuffer_ = nullptr;
        bound_draw_framebuffer_ = nullptr;
        bound_read_framebuffer_ = nullptr;
    }

    void vulkan_graphics_driver::update_surface(void *surface) {
        if (wsi_.render_surface == surface) {
            return;
        }

        wsi_.render_surface = surface;

        if (!initialized_) {
            return;
        }

        try {
            dvc_->waitIdle();
        } catch (std::exception &e) {
            LOG_WARN(DRIVER_GRAPHICS, "Waiting for Vulkan device idle before surface update failed: {}", e.what());
        }

        destroy_swapchain_resources();
        surface_.reset();

        if (!wsi_.render_surface) {
            return;
        }

        if (!create_surface()) {
            LOG_ERROR(DRIVER_GRAPHICS, "Failed to recreate Vulkan surface");
            return;
        }

        if (!create_swapchain()) {
            LOG_ERROR(DRIVER_GRAPHICS, "Failed to recreate Vulkan swapchain after surface update");
            return;
        }

        if (!create_render_resources()) {
            LOG_ERROR(DRIVER_GRAPHICS, "Failed to recreate Vulkan render resources after surface update");
        }
    }

    void vulkan_graphics_driver::update_surface_size(const eka2l1::vec2 &size) {
        const std::uint32_t width = static_cast<std::uint32_t>(std::max(0, size.x));
        const std::uint32_t height = static_cast<std::uint32_t>(std::max(0, size.y));

        if ((wsi_.surface_width == width) && (wsi_.surface_height == height)) {
            return;
        }

        wsi_.surface_width = width;
        wsi_.surface_height = height;

#if EKA2L1_PLATFORM(MACOS) || EKA2L1_PLATFORM(IOS)
        if (wsi_.render_surface) {
            get_or_create_vulkan_metal_layer(wsi_.render_surface, wsi_.render_surface_scale, wsi_.surface_width, wsi_.surface_height);
        }
#endif

        if (!initialized_ || !surface_ || (width == 0) || (height == 0)) {
            return;
        }

        try {
            dvc_->waitIdle();
        } catch (std::exception &e) {
            LOG_WARN(DRIVER_GRAPHICS, "Waiting for Vulkan device idle before swapchain resize failed: {}", e.what());
        }

        if (!create_swapchain()) {
#if EKA2L1_PLATFORM(MACOS) || EKA2L1_PLATFORM(IOS)
            LOG_INFO(DRIVER_GRAPHICS, "Retrying Vulkan swapchain resize with a recreated surface");
            destroy_swapchain_resources();
            surface_.reset();

            if (!create_surface()) {
                LOG_ERROR(DRIVER_GRAPHICS, "Failed to recreate Vulkan surface after swapchain resize failure");
                return;
            }

            if (create_swapchain() && create_render_resources()) {
                return;
            }
#endif
            LOG_ERROR(DRIVER_GRAPHICS, "Failed to resize Vulkan swapchain to {}x{}", width, height);
            return;
        }

        if (!create_render_resources()) {
            LOG_ERROR(DRIVER_GRAPHICS, "Failed to recreate Vulkan render resources after swapchain resize");
        }
    }

    void vulkan_graphics_driver::wait_for(int *status) {
        if (should_stop_) {
            return;
        }

        driver::wait_for(status);
    }

    void vulkan_graphics_driver::set_upscale_shader(const std::string &name) {
        active_upscale_shader_ = name.empty() ? "Default" : name;
    }

    std::string vulkan_graphics_driver::get_active_upscale_shader() const {
        return active_upscale_shader_;
    }

    bool vulkan_graphics_driver::support_extension(const graphics_driver_extension ext) {
        switch (ext) {
        case graphics_driver_extension_anisotrophy_filtering:
            return physical_device_features_.samplerAnisotropy == VK_TRUE;

        case graphics_driver_extension_float_precision_qualifier:
            return false;

        default:
            break;
        }

        return false;
    }

    bool vulkan_graphics_driver::query_extension_value(const graphics_driver_extension_query query, void *data_ptr) {
        if (!data_ptr) {
            return false;
        }

        switch (query) {
        case graphics_driver_extension_query_max_texture_max_anisotrophy:
            if (!support_extension(graphics_driver_extension_anisotrophy_filtering)) {
                return false;
            }

            *reinterpret_cast<float *>(data_ptr) = physical_device_properties_.limits.maxSamplerAnisotropy;
            return true;

        default:
            break;
        }

        return false;
    }
}

#endif

#undef EKA2L1_USE_VULKAN_BACKEND
