/*
 * Copyright (c) 2018 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project
 * (see bentokun.github.com/EKA2L1).
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

#include <common/algorithm.h>
#include <common/buffer.h>
#include <common/log.h>

#include <loader/romimage.h>
#include <mem/ptr.h>

namespace eka2l1::loader {
    std::optional<romimg> parse_romimg(common::ro_stream *stream, memory_system *mem, const epocver os_ver, const bool is_driver) {
        romimg img = {};

        std::uint32_t size_to_initially_read = offsetof(rom_image_header, export_dir_count);
        if (stream->read(&(img.header), size_to_initially_read) != size_to_initially_read) {
            return std::nullopt;
        }

        if ((stream->left() == 0) && is_driver) {
            return img;
        }

        if (stream->read(&img.header.export_dir_count, 4) != 4) {
            return std::nullopt;
        }

        if (stream->read(&img.header.export_dir_address, 4) != 4) {
            return std::nullopt;
        }

        // Read platform difference
        if (os_ver <= epocver::eka2) {
            if (stream->read(&img.header.checksum_code, 4) != 4) {
                return std::nullopt;
            }

            if (stream->read(&img.header.checksum_data, 4) != 4) {
                return std::nullopt;
            }
        } else {
            if (stream->read(&img.header.sec_info, sizeof(rom_vsec_info)) != sizeof(rom_vsec_info)) {
                return std::nullopt;
            }
        }

        // Read the rest
        size_to_initially_read = offsetof(rom_image_header, exception_des) - offsetof(rom_image_header, major) + sizeof(rom_image_header::exception_des);
        if (stream->read(&img.header.major, size_to_initially_read) != size_to_initially_read) {
            return std::nullopt;
        }

        if (img.header.export_dir_count < 0 || img.header.export_dir_count > 65536) {
            LOG_ERROR(LOADER, "Invalid ROM image export count: {}", img.header.export_dir_count);
            return std::nullopt;
        }

        if (img.header.export_dir_count > 0 && img.header.export_dir_address == 0) {
            LOG_ERROR(LOADER, "ROM image has exports but no export directory address");
            return std::nullopt;
        }

        ptr<uint32_t> export_off(img.header.export_dir_address);

        for (int32_t i = 0; i < img.header.export_dir_count; i++) {
            const auto export_ptr = export_off.get(mem);
            if (!export_ptr) {
                LOG_ERROR(LOADER, "ROM image export directory points to unmapped memory at 0x{:X}", export_off.ptr_address());
                return std::nullopt;
            }

            auto export_addr = *export_ptr;
            img.exports.push_back(export_addr);

            export_off += 4;
        }

        return img;
    }
}
