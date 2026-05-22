/*
 * Copyright (c) 2022 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
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

#if EKA2L1_BUILD_NATIVE_BACKEND
#include <drivers/graphics/backend/native/graphics_native.h>
#endif
#if EKA2L1_BUILD_VULKAN_BACKEND
#include <drivers/graphics/backend/vulkan/input_desc_vulkan.h>
#endif
#include <drivers/graphics/graphics.h>
#include <drivers/graphics/input_desc.h>

namespace eka2l1::drivers {
    std::unique_ptr<input_descriptors> make_input_descriptors(graphics_driver *driver) {
#if EKA2L1_BUILD_VULKAN_BACKEND
        if (driver && (driver->get_current_api() == graphic_api::vulkan)) {
            return std::make_unique<vulkan_input_descriptors>();
        }
#endif

#if EKA2L1_BUILD_NATIVE_BACKEND
        if (driver && (driver->get_current_api() == graphic_api::native)) {
            return std::make_unique<native_input_descriptors>();
        }
#endif

        return nullptr;
    }
}
