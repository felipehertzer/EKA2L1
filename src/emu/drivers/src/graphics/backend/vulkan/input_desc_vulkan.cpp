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
#include <drivers/graphics/backend/vulkan/input_desc_vulkan.h>

#include <algorithm>
#include <set>

namespace eka2l1::drivers {
    static vk::Format to_vulkan_vertex_format(const int component_count, const data_format format, const bool normalized) {
        switch (format) {
        case data_format::sfloat:
            switch (component_count) {
            case 1:
                return vk::Format::eR32Sfloat;
            case 2:
                return vk::Format::eR32G32Sfloat;
            case 3:
                return vk::Format::eR32G32B32Sfloat;
            case 4:
                return vk::Format::eR32G32B32A32Sfloat;
            default:
                break;
            }
            break;

        case data_format::byte:
            if (normalized) {
                switch (component_count) {
                case 1:
                    return vk::Format::eR8Unorm;
                case 2:
                    return vk::Format::eR8G8Unorm;
                case 3:
                    return vk::Format::eR8G8B8Unorm;
                case 4:
                    return vk::Format::eR8G8B8A8Unorm;
                default:
                    break;
                }
            } else {
                switch (component_count) {
                case 1:
                    return vk::Format::eR8Uint;
                case 2:
                    return vk::Format::eR8G8Uint;
                case 3:
                    return vk::Format::eR8G8B8Uint;
                case 4:
                    return vk::Format::eR8G8B8A8Uint;
                default:
                    break;
                }
            }
            break;

        case data_format::sbyte:
            if (normalized) {
                switch (component_count) {
                case 1:
                    return vk::Format::eR8Snorm;
                case 2:
                    return vk::Format::eR8G8Snorm;
                case 3:
                    return vk::Format::eR8G8B8Snorm;
                case 4:
                    return vk::Format::eR8G8B8A8Snorm;
                default:
                    break;
                }
            } else {
                switch (component_count) {
                case 1:
                    return vk::Format::eR8Sint;
                case 2:
                    return vk::Format::eR8G8Sint;
                case 3:
                    return vk::Format::eR8G8B8Sint;
                case 4:
                    return vk::Format::eR8G8B8A8Sint;
                default:
                    break;
                }
            }
            break;

        case data_format::word:
            if (normalized) {
                switch (component_count) {
                case 1:
                    return vk::Format::eR16Unorm;
                case 2:
                    return vk::Format::eR16G16Unorm;
                case 3:
                    return vk::Format::eR16G16B16Unorm;
                case 4:
                    return vk::Format::eR16G16B16A16Unorm;
                default:
                    break;
                }
            } else {
                switch (component_count) {
                case 1:
                    return vk::Format::eR16Uint;
                case 2:
                    return vk::Format::eR16G16Uint;
                case 3:
                    return vk::Format::eR16G16B16Uint;
                case 4:
                    return vk::Format::eR16G16B16A16Uint;
                default:
                    break;
                }
            }
            break;

        case data_format::sword:
            if (normalized) {
                switch (component_count) {
                case 1:
                    return vk::Format::eR16Snorm;
                case 2:
                    return vk::Format::eR16G16Snorm;
                case 3:
                    return vk::Format::eR16G16B16Snorm;
                case 4:
                    return vk::Format::eR16G16B16A16Snorm;
                default:
                    break;
                }
            } else {
                switch (component_count) {
                case 1:
                    return vk::Format::eR16Sint;
                case 2:
                    return vk::Format::eR16G16Sint;
                case 3:
                    return vk::Format::eR16G16B16Sint;
                case 4:
                    return vk::Format::eR16G16B16A16Sint;
                default:
                    break;
                }
            }
            break;

        case data_format::uint:
            switch (component_count) {
            case 1:
                return vk::Format::eR32Uint;
            case 2:
                return vk::Format::eR32G32Uint;
            case 3:
                return vk::Format::eR32G32B32Uint;
            case 4:
                return vk::Format::eR32G32B32A32Uint;
            default:
                break;
            }
            break;

        case data_format::sint:
        case data_format::fixed:
            switch (component_count) {
            case 1:
                return vk::Format::eR32Sint;
            case 2:
                return vk::Format::eR32G32Sint;
            case 3:
                return vk::Format::eR32G32B32Sint;
            case 4:
                return vk::Format::eR32G32B32A32Sint;
            default:
                break;
            }
            break;

        default:
            break;
        }

        return vk::Format::eUndefined;
    }

    bool vulkan_input_descriptors::modify(drivers::graphics_driver *drv, input_descriptor *descs, const int count) {
        (void)drv;
        inputs_.clear();

        if (!descs || (count <= 0)) {
            return true;
        }

        inputs_.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; i++) {
            inputs_.push_back(descs[i]);
        }

        return true;
    }

    std::vector<vk::VertexInputBindingDescription> vulkan_input_descriptors::binding_descriptions(const std::vector<input_descriptor> &inputs) {
        std::set<std::uint32_t> slots;
        for (const input_descriptor &input : inputs) {
            slots.insert(input.buffer_slot);
        }

        std::vector<vk::VertexInputBindingDescription> bindings;
        bindings.reserve(slots.size());

        for (const std::uint32_t slot : slots) {
            const auto descriptor = std::find_if(inputs.begin(), inputs.end(),
                [slot](const input_descriptor &input) {
                    return input.buffer_slot == slot;
                });
            if (descriptor == inputs.end()) {
                continue;
            }

            vk::VertexInputBindingDescription binding;
            binding.binding = slot;
            binding.stride = static_cast<std::uint32_t>(std::max(0, descriptor->stride));
            binding.inputRate = descriptor->is_per_instance()
                ? vk::VertexInputRate::eInstance
                : vk::VertexInputRate::eVertex;
            bindings.push_back(binding);
        }

        return bindings;
    }

    std::vector<vk::VertexInputAttributeDescription> vulkan_input_descriptors::attribute_descriptions(const std::vector<input_descriptor> &inputs) {
        std::vector<vk::VertexInputAttributeDescription> attributes;
        attributes.reserve(inputs.size());

        for (const input_descriptor &input : inputs) {
            const int component_count = input.format & 0b1111;
            const data_format format = static_cast<data_format>((input.format >> 4) & 0b1111);
            const vk::Format vk_format = to_vulkan_vertex_format(component_count, format, input.is_normalized());
            if (vk_format == vk::Format::eUndefined) {
                LOG_WARN(DRIVER_GRAPHICS, "Unsupported Vulkan vertex input format {}", input.format);
                continue;
            }

            vk::VertexInputAttributeDescription attribute;
            attribute.location = static_cast<std::uint32_t>(input.location);
            attribute.binding = input.buffer_slot;
            attribute.format = vk_format;
            attribute.offset = static_cast<std::uint32_t>(std::max(0, input.offset));
            attributes.push_back(attribute);
        }

        return attributes;
    }

    std::vector<vk::VertexInputBindingDescription> vulkan_input_descriptors::binding_descriptions() const {
        return binding_descriptions(inputs_);
    }

    std::vector<vk::VertexInputAttributeDescription> vulkan_input_descriptors::attribute_descriptions() const {
        return attribute_descriptions(inputs_);
    }
}

#endif

#undef EKA2L1_USE_VULKAN_BACKEND
