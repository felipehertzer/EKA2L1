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

#include <cstdint>

namespace eka2l1::drivers {
    void *get_or_create_vulkan_metal_layer(void *render_surface, float scale, std::uint32_t width, std::uint32_t height);
    void destroy_vulkan_metal_layer(void *render_surface);
}
