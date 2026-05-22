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

#include <drivers/graphics/graphics.h>
#if EKA2L1_BUILD_NATIVE_BACKEND
#include <drivers/graphics/backend/native/graphics_native.h>
#endif
#if EKA2L1_BUILD_VULKAN_BACKEND
#include <drivers/graphics/backend/vulkan/graphics_vulkan.h>
#endif

#include <common/configure.h>
#include <common/log.h>
#include <common/platform.h>

namespace eka2l1::drivers {
    graphics_driver_ptr create_graphics_driver(const graphic_api api, const window_system_info &info) {
#if EKA2L1_BUILD_VULKAN_BACKEND
        if (api == graphic_api::vulkan) {
            auto driver = std::make_unique<vulkan_graphics_driver>(info);
            if (!driver->is_initialized()) {
                LOG_ERROR(DRIVER_GRAPHICS, "Vulkan initialization failed");
                return nullptr;
            }

            return driver;
        }
#endif

#if EKA2L1_BUILD_NATIVE_BACKEND
        if (api == graphic_api::native) {
            auto driver = std::make_unique<native_graphics_driver>(info);
            if (!driver->is_initialized()) {
                LOG_ERROR(DRIVER_GRAPHICS, "Native graphics backend initialization failed");
                return nullptr;
            }

            return driver;
        }
#endif

        LOG_ERROR(DRIVER_GRAPHICS, "Requested graphics backend is not available in this build");
        return nullptr;
    }
}
