/*
 * Copyright (c) 2019 EKA2L1 Team
 *
 * This file is part of EKA2L1 project
 * (see bentokun.github.com/EKA2L1).
 *
 * Initial contributor: pent0
 * Contributors:
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

#include <services/fbs/fbs.h>
#include <services/fbs/palette.h>
#include <services/window/bitmap_cache.h>
#include <services/window/classes/gstore.h>

#include <kernel/chunk.h>
#include <kernel/kernel.h>
#include <system/epoc.h>

#include <drivers/graphics/graphics.h>
#include <drivers/itc.h>

#include <loader/nvg.h>
#include <utils/guest/akn.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <common/buffer.h>
#include <common/log.h>
#include <common/runlen.h>
#include <common/time.h>

#define XXH_INLINE_ALL
#include <xxhash.h>

// LunaSVG
#include <lunasvg.h>

namespace eka2l1::epoc {
    static bool bitmap_trace_enabled() {
        return std::getenv("EKA2L1_BITMAP_TRACE") != nullptr;
    }

    static int bitmap_trace_env_int(const char *name, const int fallback) {
        const char *value = std::getenv(name);
        if (!value || !*value) {
            return fallback;
        }

        return std::atoi(value);
    }

    static bool bitmap_trace_matches(epoc::bitwise_bitmap *bmp) {
        if (!bitmap_trace_enabled()) {
            return false;
        }

        if (std::getenv("EKA2L1_BITMAP_TRACE_ALL")) {
            return true;
        }

        const int width = bitmap_trace_env_int("EKA2L1_BITMAP_TRACE_WIDTH", 0);
        const int height = bitmap_trace_env_int("EKA2L1_BITMAP_TRACE_HEIGHT", 0);
        if ((width > 0) && (bmp->header_.size_pixels.x != width)) {
            return false;
        }

        if ((height > 0) && (bmp->header_.size_pixels.y != height)) {
            return false;
        }

        return (width > 0) || (height > 0);
    }

    static std::string bitmap_trace_sample(const char *data, const std::size_t size) {
        constexpr std::size_t SAMPLE_SIZE = 32;
        const std::size_t count = std::min(SAMPLE_SIZE, size);
        std::string result;
        result.reserve(count * 3);

        for (std::size_t i = 0; i < count; i++) {
            char byte_string[4];
            std::snprintf(byte_string, sizeof(byte_string), "%02X", static_cast<unsigned>(static_cast<std::uint8_t>(data[i])));
            if (i != 0) {
                result.push_back(' ');
            }
            result += byte_string;
        }

        return result;
    }

    static void trace_bitmap_upload(const char *stage, epoc::bitwise_bitmap *bmp, const char *data,
        const std::size_t size, const std::uint32_t upload_bpp, const std::size_t pixels_per_line,
        const std::uint64_t hash, const bool should_upload, const bool should_recreate) {
        if (!bitmap_trace_matches(bmp) || !data) {
            return;
        }

        std::uint8_t min_value = 0xFF;
        std::uint8_t max_value = 0x00;
        std::size_t non_zero = 0;
        std::size_t non_ff = 0;
        const std::size_t inspected_size = std::min<std::size_t>(size, 1024 * 1024);

        for (std::size_t i = 0; i < inspected_size; i++) {
            const std::uint8_t value = static_cast<std::uint8_t>(data[i]);
            min_value = std::min(min_value, value);
            max_value = std::max(max_value, value);
            if (value != 0) {
                non_zero++;
            }
            if (value != 0xFF) {
                non_ff++;
            }
        }

        epoc::display_mode current_dsp = bmp->settings_.current_display_mode();
        if (current_dsp == epoc::display_mode::none) {
            current_dsp = bmp->settings_.initial_display_mode();
        }

        LOG_INFO(SERVICE_WINDOW,
            "Bitmap trace {} ptr={} uid=0x{:X} size={}x{} header_bpp={} upload_bpp={} mode={} init_mode={} byte_width={} header_len={} bitmap_size={} data_size={} comp={} upload={} recreate={} pixels_per_line={} hash=0x{:X} inspected={} non_zero={} non_ff={} min={} max={} sample={}",
            stage,
            reinterpret_cast<void *>(bmp),
            bmp->uid_,
            bmp->header_.size_pixels.x,
            bmp->header_.size_pixels.y,
            bmp->header_.bit_per_pixels,
            upload_bpp,
            static_cast<int>(current_dsp),
            static_cast<int>(bmp->settings_.initial_display_mode()),
            bmp->byte_width_,
            bmp->header_.header_len,
            bmp->header_.bitmap_size,
            bmp->data_size(),
            static_cast<int>(bmp->compression_type()),
            should_upload,
            should_recreate,
            pixels_per_line,
            hash,
            inspected_size,
            non_zero,
            non_ff,
            min_value,
            max_value,
            bitmap_trace_sample(data, size));
    }

    static std::uint8_t bitmap_dump_expand_bits(const std::uint32_t value, const std::uint32_t bits) {
        const std::uint32_t max_value = (1U << bits) - 1U;
        return static_cast<std::uint8_t>((value * 255U + (max_value / 2U)) / max_value);
    }

    static void bitmap_dump_write_ppm(epoc::bitwise_bitmap *bmp, const char *data,
        const std::size_t size, const std::uint32_t upload_bpp, const std::size_t pixels_per_line,
        const std::uint64_t hash) {
        const char *dump_dir = std::getenv("EKA2L1_BITMAP_DUMP_DIR");
        if (!dump_dir || !*dump_dir || !bitmap_trace_matches(bmp) || !data) {
            return;
        }

        static int dump_count = 0;
        const int dump_limit = bitmap_trace_env_int("EKA2L1_BITMAP_DUMP_LIMIT", 200);
        if ((dump_limit > 0) && (dump_count >= dump_limit)) {
            return;
        }

        const std::uint32_t width = static_cast<std::uint32_t>(bmp->header_.size_pixels.x);
        const std::uint32_t height = static_cast<std::uint32_t>(bmp->header_.size_pixels.y);
        std::size_t bytes_per_pixel = 0;
        switch (upload_bpp) {
        case 12:
        case 16:
            bytes_per_pixel = 2;
            break;

        case 24:
            bytes_per_pixel = 3;
            break;

        case 32:
            bytes_per_pixel = 4;
            break;

        default:
            return;
        }

        const std::size_t row_stride = pixels_per_line
            ? (pixels_per_line * bytes_per_pixel)
            : (height ? (size / height) : 0);
        if ((width == 0) || (height == 0) || (row_stride < width * bytes_per_pixel)) {
            return;
        }

        char filename[1024];
        std::snprintf(filename, sizeof(filename), "%s/bitmap-%04d-%ux%u-bpp%u-%016llx.ppm",
            dump_dir, dump_count, width, height, upload_bpp,
            static_cast<unsigned long long>(hash));

        FILE *out = std::fopen(filename, "wb");
        if (!out) {
            return;
        }

        std::fprintf(out, "P6\n%u %u\n255\n", width, height);
        const std::uint8_t *source = reinterpret_cast<const std::uint8_t *>(data);
        for (std::uint32_t y = 0; y < height; y++) {
            const std::uint8_t *row = source + (row_stride * y);
            for (std::uint32_t x = 0; x < width; x++) {
                const std::uint8_t *pixel = row + (x * bytes_per_pixel);
                std::uint8_t rgb[3] = { 0, 0, 0 };

                switch (upload_bpp) {
                case 12: {
                    const std::uint16_t packed = static_cast<std::uint16_t>(pixel[0] | (pixel[1] << 8));
                    rgb[0] = bitmap_dump_expand_bits((packed >> 8) & 0xF, 4);
                    rgb[1] = bitmap_dump_expand_bits((packed >> 4) & 0xF, 4);
                    rgb[2] = bitmap_dump_expand_bits(packed & 0xF, 4);
                    break;
                }

                case 16: {
                    const std::uint16_t packed = static_cast<std::uint16_t>(pixel[0] | (pixel[1] << 8));
                    rgb[0] = bitmap_dump_expand_bits((packed >> 11) & 0x1F, 5);
                    rgb[1] = bitmap_dump_expand_bits((packed >> 5) & 0x3F, 6);
                    rgb[2] = bitmap_dump_expand_bits(packed & 0x1F, 5);
                    break;
                }

                case 24:
                    rgb[0] = pixel[2];
                    rgb[1] = pixel[1];
                    rgb[2] = pixel[0];
                    break;

                case 32:
                    rgb[0] = pixel[2];
                    rgb[1] = pixel[1];
                    rgb[2] = pixel[0];
                    break;

                default:
                    break;
                }

                std::fwrite(rgb, sizeof(rgb), 1, out);
            }
        }

        std::fclose(out);
        dump_count++;
    }

    bitmap_cache::bitmap_cache(kernel_system *kern_)
        : fbss_(nullptr)
        , kern(kern_) {
        std::fill(driver_textures.begin(), driver_textures.end(), 0);
        std::fill(hashes.begin(), hashes.end(), 0);
    }

    void bitmap_cache::clean(drivers::graphics_driver *drv) {
        if (!drv) {
            return;
        }

        drivers::graphics_command_builder builder;

        for (const auto tex_handle : driver_textures) {
            if (tex_handle) {
                builder.destroy_bitmap(tex_handle);
            }
        }

        eka2l1::drivers::command_list retrieved = builder.retrieve_command_list();
        drv->submit_command_list(retrieved);
    }

    bool is_palette_bitmap(epoc::bitwise_bitmap *bw_bmp) {
        epoc::display_mode dsp = bw_bmp->settings_.current_display_mode();
        if (dsp == epoc::display_mode::none) {
            dsp = bw_bmp->settings_.initial_display_mode();
        }

        return (dsp == epoc::display_mode::color16) || (dsp == epoc::display_mode::color256);
    }

    static char *converted_one_bpp_to_twenty_four_bpp_bitmap(epoc::bitwise_bitmap *bw_bmp,
        const std::uint32_t *original_ptr, std::size_t &raw_size) {
        std::uint32_t byte_width_converted = common::align(bw_bmp->header_.size_pixels.x * 3, 4);
        raw_size = byte_width_converted * bw_bmp->header_.size_pixels.y;

        char *return_ptr = new char[raw_size];

        for (std::size_t y = 0; y < bw_bmp->header_.size_pixels.y; y++) {
            for (std::size_t x = 0; x < bw_bmp->header_.size_pixels.x; x++) {
                std::uint32_t word_per_line = (bw_bmp->header_.size_pixels.x + 31) / 32;
                std::uint32_t color = original_ptr[y * word_per_line + x / 32];
                std::uint32_t converted_color = 0;
                if (color & (1 << (x & 0x1F))) {
                    converted_color = 0xFFFFFF;
                }

                std::memcpy(return_ptr + byte_width_converted * y + x * 3, reinterpret_cast<const char *>(&converted_color), 3);
            }
        }
        return return_ptr;
    }

    static char *converted_gray_four_bpp_to_twenty_four_bpp_bitmap(epoc::bitwise_bitmap *bw_bmp,
        const std::uint8_t *original_ptr, std::size_t &raw_size) {
        std::uint32_t byte_width_converted = common::align(bw_bmp->header_.size_pixels.x * 3, 4);
        raw_size = byte_width_converted * bw_bmp->header_.size_pixels.y;

        char *return_ptr = new char[raw_size];
        std::uint32_t scan_line_size = ((bw_bmp->header_.size_pixels.x + 7) >> 3) << 2;

        for (std::size_t y = 0; y < bw_bmp->header_.size_pixels.y; y++) {
            for (std::size_t x = 0; x < bw_bmp->header_.size_pixels.x / 2; x++) {
                std::uint8_t gray_pack = original_ptr[y * scan_line_size + x];
                std::uint8_t converted_color_comp_first = (gray_pack & 0xF) | ((gray_pack & 0xF) << 4);
                std::uint8_t converted_color_comp_second = ((gray_pack & 0xF0) >> 4) | (gray_pack & 0xF0);

                std::memset(return_ptr + y * byte_width_converted + x * 3 * 2, converted_color_comp_first, 3);
                std::memset(return_ptr + y * byte_width_converted + x * 3 * 2 + 3, converted_color_comp_second, 3);
            }
        }

        return return_ptr;
    }

    static char *converted_palette_bitmap_to_twenty_four_bitmap(epoc::bitwise_bitmap *bw_bmp,
        const std::uint8_t *original_ptr, epoc::palette_256 &the_palette, epoc::palette_16 &the_palette_16,
        std::size_t &raw_size) {
        std::uint32_t byte_width_converted = common::align(bw_bmp->header_.size_pixels.x * 3, 4);
        raw_size = byte_width_converted * bw_bmp->header_.size_pixels.y;
        char *return_ptr = new char[raw_size];

        epoc::display_mode dsp = bw_bmp->settings_.current_display_mode();
        if (dsp == epoc::display_mode::none) {
            dsp = bw_bmp->settings_.initial_display_mode();
        }

        std::uint32_t skip_width = 1;
        if (dsp == epoc::display_mode::color16) {
            skip_width = 2;
        }

        for (std::size_t y = 0; y < bw_bmp->header_.size_pixels.y; y++) {
            for (std::size_t x = 0; x < bw_bmp->header_.size_pixels.x / skip_width; x++) {
                switch (dsp) {
                case epoc::display_mode::color256: {
                    const std::uint8_t palette_index = original_ptr[y * bw_bmp->byte_width_ + x];
                    const std::uint32_t palette_color = the_palette[palette_index];
                    const std::size_t location = byte_width_converted * y + x * 3;

                    return_ptr[location + 2] = palette_color & 0xFF;
                    return_ptr[location + 1] = (palette_color >> 8) & 0xFF;
                    return_ptr[location] = (palette_color >> 16) & 0xFF;

                    break;
                }

                case epoc::display_mode::color16: {
                    const std::uint8_t palette_index_for_two = original_ptr[y * bw_bmp->byte_width_ + x];
                    const std::uint32_t palette_color_first = the_palette_16[palette_index_for_two & 0x0F];
                    const std::uint32_t palette_color_second = the_palette_16[(palette_index_for_two >> 4) & 0x0F];
                    const std::size_t location = byte_width_converted * y + x * 3 * 2;

                    return_ptr[location + 2] = palette_color_first & 0xFF;
                    return_ptr[location + 1] = (palette_color_first >> 8) & 0xFF;
                    return_ptr[location] = (palette_color_first >> 16) & 0xFF;

                    return_ptr[location + 5] = palette_color_second & 0xFF;
                    return_ptr[location + 4] = (palette_color_second >> 8) & 0xFF;
                    return_ptr[location + 3] = (palette_color_second >> 16) & 0xFF;

                    break;
                }

                default:
                    LOG_ERROR(SERVICE_WINDOW, "Unhandled display mode to convert {}", static_cast<int>(dsp));
                    break;
                }
            }
        }

        return return_ptr;
    }

    static std::uint32_t get_suitable_bpp_for_bitmap(epoc::bitwise_bitmap *bmp) {
        if (bmp->uid_ != epoc::bitwise_bitmap_uid) {
            // Extended bitmap, will be converted to RGBA8888
            return 32;
        }

        if (is_palette_bitmap(bmp) || (bmp->header_.bit_per_pixels == 1) || (bmp->header_.bit_per_pixels == 4)) {
            return 24;
        }

        return bmp->header_.bit_per_pixels;
    }

    std::uint64_t bitmap_cache::hash_bitwise_bitmap(epoc::bitwise_bitmap *bw_bmp) {
        std::uint64_t hash = 0xB1711A3F;

        // Hash using XXHASH
        XXH64_state_t *const state = XXH64_createState();
        XXH64_reset(state, hash);

        // First, hash the single bitmap header
        XXH64_update(state, reinterpret_cast<const void *>(&bw_bmp->header_), sizeof(loader::sbm_header));

        // Now, hash the byte width and UID, if it changes, we need to update
        XXH64_update(state, reinterpret_cast<const void *>(&bw_bmp->byte_width_), sizeof(bw_bmp->byte_width_));
        XXH64_update(state, reinterpret_cast<const void *>(&bw_bmp->uid_), sizeof(bw_bmp->uid_));

        // Lastly, we needs to hash the data, to see if anything changed
        const std::size_t bitmap_data_size = bw_bmp->header_.bitmap_size > sizeof(bw_bmp->header_)
            ? static_cast<std::size_t>(bw_bmp->header_.bitmap_size - sizeof(bw_bmp->header_))
            : 0;
        std::uint8_t *bitmap_data = bw_bmp->data_pointer(fbss_);
        if ((bitmap_data_size > 0) && (!bitmap_data || !fbss_ || !fbss_->ensure_host_range_accessible(bitmap_data, bitmap_data_size))) {
            LOG_WARN(SERVICE_WINDOW, "Unable to hash bitmap with inaccessible data pointer");
        } else if (bitmap_data_size > 0) {
            XXH64_update(state, bitmap_data, bitmap_data_size);
        }

        hash = XXH64_digest(state);
        XXH64_freeState(state);

        return hash;
    }

    std::int64_t bitmap_cache::get_suitable_bitmap_index() {
        // First time, will scans through the bitmap array to find empty box
        // Sometimes, app might purges a lot of bitmaps at same time
        for (std::int64_t i = MAX_CACHE_SIZE - 1; i >= 0; i--) {
            if (!bitmaps[i]) {
                return i;
            }
        }

        // Next, we will compare the timestamp
        std::uint64_t oldest_timestamp_idx = 0;
        for (std::int64_t i = 1; i < MAX_CACHE_SIZE; i++) {
            if (timestamps[i] < timestamps[oldest_timestamp_idx]) {
                oldest_timestamp_idx = i;
            }
        }

        return oldest_timestamp_idx;
    }

    drivers::handle bitmap_cache::add_or_get(drivers::graphics_driver *driver, epoc::bitwise_bitmap *bmp,
        drivers::graphics_command_builder *builder, gdi_store_command *update_cmd) {
        if (!fbss_) {
            server_ptr ss = kern->get_by_name<service::server>(epoc::get_fbs_server_name_by_epocver(
                kern->get_epoc_version()));

            fbss_ = reinterpret_cast<fbs_server *>(ss);
        }

        std::int64_t idx = 0;
        std::uint64_t crr_timestamp = common::get_current_utc_time_in_microseconds_since_0ad();

        std::uint64_t hash = 0;

        bool should_upload = true;
        bool should_recreate = true;

        const std::uint32_t suit_bpp = get_suitable_bpp_for_bitmap(bmp);
        auto bitmap_ite = std::find(bitmaps.begin(), bitmaps.end(), bmp);

        if (bitmap_ite == bitmaps.end()) {
            // If the bitmap is not in the bitmap array
            if (last_free < MAX_CACHE_SIZE) {
                // Use last free
                idx = last_free++;
            } else {
                idx = get_suitable_bitmap_index();
            }

            bitmaps[idx] = bmp;
            driver_textures[idx] = 0;
            hash = (hashes[idx] == 0) ? hash_bitwise_bitmap(bmp) : hashes[idx];
        } else {
            // Else, get the index
            idx = std::distance(bitmaps.begin(), bitmap_ite);

            // Check if we should upload or not, by calculating the hash
            hash = hash_bitwise_bitmap(bmp);
            should_upload = hash != (hashes[idx]);

            const std::uint32_t bitmap_bpp = static_cast<std::uint32_t>(bitmap_sizes[idx].first >> 32);
            eka2l1::object_size bitmap_stored_size(static_cast<int>(bitmap_sizes[idx].first), static_cast<int>(bitmap_sizes[idx].second));

            should_recreate = (bmp->header_.size_pixels != bitmap_stored_size) || (bitmap_bpp != suit_bpp);
        }

        if (update_cmd) {
            gdi_store_command_update_texture_data &data = update_cmd->get_data_struct<gdi_store_command_update_texture_data>();
            data.destroy_handle_ = 0;
            data.do_swizz_ = false;
        }

        if (should_recreate) {
            if (driver_textures[idx]) {
                if (builder) {
                    builder->destroy_bitmap(driver_textures[idx]);
                }

                if (update_cmd) {
                    gdi_store_command_update_texture_data &data = update_cmd->get_data_struct<gdi_store_command_update_texture_data>();

                    update_cmd->opcode_ = gdi_store_command_update_texture;
                    data.destroy_handle_ = driver_textures[idx];
                }
            }

            driver_textures[idx] = drivers::create_bitmap(driver, bmp->header_.size_pixels, suit_bpp);
        }

        if (should_upload) {
            char *data_pointer = reinterpret_cast<char *>(bmp->data_pointer(fbss_));
            std::uint32_t raw_size = 0;
            std::size_t pixels_per_line = 0;

            const std::uint32_t compressed_size = bmp->header_.bitmap_size > bmp->header_.header_len
                ? bmp->header_.bitmap_size - bmp->header_.header_len
                : 0;
            if ((compressed_size > 0) && (!data_pointer || !fbss_->ensure_host_range_accessible(data_pointer, compressed_size))) {
                LOG_WARN(SERVICE_WINDOW, "Skipping bitmap upload with inaccessible source data");
                hashes[idx] = hash;
                timestamps[idx] = crr_timestamp;
                bitmap_sizes[idx].first = static_cast<std::uint64_t>(bmp->header_.size_pixels.x) | (static_cast<std::uint64_t>(suit_bpp) << 32);
                bitmap_sizes[idx].second = static_cast<std::uint32_t>(bmp->header_.size_pixels.y);
                return driver_textures[idx];
            }

            if (bmp->uid_ == epoc::NVG_BITMAP_UID_REV2) {
                // Skip the header!!
                utils::akn_icon_header *header_icon = reinterpret_cast<utils::akn_icon_header *>(data_pointer);
                data_pointer += header_icon->header_size_;

                std::size_t pixmap_size = bmp->header_.size_pixels.x * bmp->header_.size_pixels.y * 4;
                pixels_per_line = bmp->header_.size_pixels.x;

                const bool is_mask = header_icon->is_mask_;

                if (!is_mask && (header_icon->icon_color_ & 0xFFFFFF)) {
                    // TODO: Seems like actually force RGB fill and keep alpha!
                    // https://github.com/SymbianSource/oss.FCL.sf.mw.uiaccelerator/blob/773fc5e215e04584c3e00a1b1fa05c5a8c50440a/uiacceltk/hitchcock/coretoolkit/rendervg10/src/HuiVg10Texture.cpp#L1704
                    data_pointer = new char[pixmap_size];
                    std::fill(reinterpret_cast<std::uint32_t *>(data_pointer), reinterpret_cast<std::uint32_t *>(data_pointer + pixmap_size),
                        ((header_icon->icon_color_ & 0xFFFFFF) << 8) | 0xFF);
                } else {
                    common::ro_buf_stream nvg_in_stream(reinterpret_cast<std::uint8_t *>(data_pointer), compressed_size);
                    common::wo_growable_buf_stream svg_out_stream;

                    std::vector<loader::nvg_convert_error_description> errors;

                    loader::nvg_options cvt_options;
                    cvt_options.width = bmp->header_.size_pixels.x;
                    cvt_options.height = bmp->header_.size_pixels.y;
                    cvt_options.aspect_ratio_mode_ = static_cast<loader::nvg_aspect_ratio_mode>(header_icon->aspect_ratio_);

                    if (!loader::convert_nvg_to_svg(nvg_in_stream, svg_out_stream, errors, &cvt_options)) {
                        data_pointer = new char[pixmap_size];
                        std::memset(data_pointer, 0, pixmap_size);

                        LOG_ERROR(SERVICE_WINDOW, "Failed to convert NVG bitmap to SVG for rendering!");
                    } else {
                        data_pointer = new char[pixmap_size];

                        // Render in-memory using lunasvg
                        // Hope the performance is good! The icon/bitmap seems very small
                        std::memset(data_pointer, 0, pixmap_size);

                        const std::string svg_out_content = svg_out_stream.content();
                        // LOG_DEBUG(SERVICE_WINDOW, "{}", svg_out_content);

                        auto nvg_doc = lunasvg::Document::loadFromData(svg_out_content);
                        if (!nvg_doc) {
                            LOG_ERROR(SERVICE_WINDOW, "Error loading SVG in-memory, SVG content: {}", svg_out_content);
                        } else {
                            auto bitmap = lunasvg::Bitmap(reinterpret_cast<std::uint8_t *>(data_pointer), bmp->header_.size_pixels.x,
                                bmp->header_.size_pixels.y, bmp->header_.size_pixels.x * 4);

                            lunasvg::Matrix matrix{ 1, 0, 0, 1, 0, 0 };
                            nvg_doc->render(bitmap, matrix);
                        }
                    }
                }
            } else {
                const bitmap_file_compression comp = bmp->compression_type();

                if (comp != bitmap_file_no_compression) {
                    raw_size = bmp->byte_width_ * bmp->header_.size_pixels.y;
                    char *new_data_allocated = new char[raw_size];
                    std::size_t final_size = raw_size;

                    switch (comp) {
                    case bitmap_file_byte_rle_compression:
                        eka2l1::decompress_rle_fast_route<8>(reinterpret_cast<std::uint8_t *>(data_pointer), compressed_size, reinterpret_cast<std::uint8_t *>(new_data_allocated), final_size);
                        break;

                    case bitmap_file_twelve_bit_rle_compression:
                        eka2l1::decompress_rle_fast_route<12>(reinterpret_cast<std::uint8_t *>(data_pointer), compressed_size, reinterpret_cast<std::uint8_t *>(new_data_allocated), final_size);
                        break;

                    case bitmap_file_sixteen_bit_rle_compression:
                        eka2l1::decompress_rle_fast_route<16>(reinterpret_cast<std::uint8_t *>(data_pointer), compressed_size, reinterpret_cast<std::uint8_t *>(new_data_allocated), final_size);
                        break;

                    case bitmap_file_twenty_four_bit_rle_compression:
                        eka2l1::decompress_rle_fast_route<24>(reinterpret_cast<std::uint8_t *>(data_pointer), compressed_size, reinterpret_cast<std::uint8_t *>(new_data_allocated), final_size);
                        break;

                    default:
                        LOG_ERROR(SERVICE_WINDOW, "Unsupported bitmap format to decode {}", static_cast<std::uint32_t>(comp));
                        break;
                    }

                    data_pointer = new_data_allocated;
                } else {
                    raw_size = bmp->header_.bitmap_size - bmp->header_.header_len;

                    char *new_data_pointer = new char[raw_size];
                    std::memcpy(new_data_pointer, data_pointer, raw_size);

                    data_pointer = new_data_pointer;
                }

                trace_bitmap_upload("source", bmp, data_pointer, raw_size, bmp->header_.bit_per_pixels,
                    pixels_per_line, hash, should_upload, should_recreate);

                std::uint32_t bpp = bmp->header_.bit_per_pixels;

                if ((bmp->header_.bit_per_pixels % 8) == 0) {
                    pixels_per_line = bmp->byte_width_ / (bmp->header_.bit_per_pixels >> 3);
                }

                std::size_t raw_size_big = 0;

                epoc::display_mode dsp = bmp->settings_.current_display_mode();
                if (dsp == epoc::display_mode::none) {
                    dsp = bmp->settings_.initial_display_mode();
                }

                // GPU don't support them. Convert them on CPU
                if (is_palette_bitmap(bmp) || (dsp == epoc::display_mode::gray16)) {
                    char *new_pointer = nullptr;
                    if (dsp == epoc::display_mode::gray16) {
                        new_pointer = converted_gray_four_bpp_to_twenty_four_bpp_bitmap(bmp, reinterpret_cast<const std::uint8_t *>(data_pointer), raw_size_big);
                    } else {
                        new_pointer = converted_palette_bitmap_to_twenty_four_bitmap(bmp, reinterpret_cast<const std::uint8_t *>(data_pointer),
                            epoc::get_suitable_palette_256(kern->get_epoc_version(), kern->get_system()->is_s80_device_active()),
                            epoc::color_16_palette, raw_size_big);
                    }

                    bpp = 24;
                    raw_size = static_cast<std::uint32_t>(raw_size_big);

                    delete[] data_pointer;
                    data_pointer = new_pointer;

                    // Use default
                    pixels_per_line = 0;
                }

                char *newly_pointer = nullptr;

                switch (bpp) {
                case 1:
                    newly_pointer = converted_one_bpp_to_twenty_four_bpp_bitmap(bmp, reinterpret_cast<const std::uint32_t *>(data_pointer),
                        raw_size_big);
                    bpp = 24;
                    raw_size = static_cast<std::uint32_t>(raw_size_big);
                    pixels_per_line = 0;

                    break;

                default:
                    break;
                }

                if (newly_pointer) {
                    delete[] data_pointer;
                    data_pointer = newly_pointer;
                }
            }

            std::uint32_t trace_bpp = suit_bpp;
            if (bmp->uid_ == epoc::NVG_BITMAP_UID_REV2) {
                trace_bpp = 32;
            }
            trace_bitmap_upload("upload", bmp, data_pointer, raw_size, trace_bpp, pixels_per_line,
                hash, should_upload, should_recreate);
            bitmap_dump_write_ppm(bmp, data_pointer, raw_size, trace_bpp, pixels_per_line, hash);

            if (builder) {
                builder->update_bitmap(driver_textures[idx], data_pointer, raw_size, { 0, 0 }, bmp->header_.size_pixels, pixels_per_line, false);
            }

            if (update_cmd) {
                update_cmd->opcode_ = gdi_store_command_update_texture;

                gdi_store_command_update_texture_data &data = update_cmd->get_data_struct<gdi_store_command_update_texture_data>();
                data.handle_ = driver_textures[idx];
                data.texture_data_ = data_pointer;
                data.pixel_per_line_ = pixels_per_line;
                data.dim_ = bmp->header_.size_pixels;
                data.texture_size_ = raw_size;
            }

            hashes[idx] = hash;

            if (bmp->uid_ == epoc::NVG_BITMAP_UID_REV2) {
                // Used for blending mostly, where only alpha is relevant
                const auto display_mode = bmp->settings_.current_display_mode();
                if (epoc::is_display_mode_mono(display_mode)) {
                    if (builder) {
                        builder->set_swizzle(driver_textures[idx], drivers::channel_swizzle::alpha, drivers::channel_swizzle::alpha,
                            drivers::channel_swizzle::alpha, drivers::channel_swizzle::alpha);
                    }

                    if (update_cmd) {
                        gdi_store_command_update_texture_data &data = update_cmd->get_data_struct<gdi_store_command_update_texture_data>();
                        data.do_swizz_ = true;
                        data.swizz_ = { drivers::channel_swizzle::alpha, drivers::channel_swizzle::alpha,
                            drivers::channel_swizzle::alpha, drivers::channel_swizzle::alpha };
                    }
                } else {
                    if (builder) {
                        builder->set_swizzle(driver_textures[idx], drivers::channel_swizzle::red,
                            drivers::channel_swizzle::green,
                            drivers::channel_swizzle::blue,
                            epoc::is_display_mode_alpha(display_mode) ? drivers::channel_swizzle::alpha : drivers::channel_swizzle::one);
                    }

                    if (update_cmd) {
                        gdi_store_command_update_texture_data &data = update_cmd->get_data_struct<gdi_store_command_update_texture_data>();
                        data.do_swizz_ = true;
                        data.swizz_ = { drivers::channel_swizzle::red,
                            drivers::channel_swizzle::green,
                            drivers::channel_swizzle::blue,
                            epoc::is_display_mode_alpha(display_mode) ? drivers::channel_swizzle::alpha : drivers::channel_swizzle::one };
                    }
                }
            } else {
                if (bmp->settings_.current_display_mode() == epoc::display_mode::color16mu) {
                    if (builder) {
                        builder->set_swizzle(driver_textures[idx], drivers::channel_swizzle::red, drivers::channel_swizzle::green,
                            drivers::channel_swizzle::blue, drivers::channel_swizzle::one);
                    }

                    if (update_cmd) {
                        gdi_store_command_update_texture_data &data = update_cmd->get_data_struct<gdi_store_command_update_texture_data>();
                        data.do_swizz_ = true;
                        data.swizz_ = { drivers::channel_swizzle::red, drivers::channel_swizzle::green,
                            drivers::channel_swizzle::blue, drivers::channel_swizzle::one };
                    }
                }
            }

            if (!builder && !update_cmd) {
                delete[] data_pointer;
            }
        }

        timestamps[idx] = crr_timestamp;

        bitmap_sizes[idx].first = static_cast<std::uint64_t>(bmp->header_.size_pixels.x) | (static_cast<std::uint64_t>(suit_bpp) << 32);
        bitmap_sizes[idx].second = static_cast<std::uint32_t>(bmp->header_.size_pixels.y);

        return driver_textures[idx];
    }
}
