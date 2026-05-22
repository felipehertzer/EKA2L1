/*
 * Copyright (c) 2020 EKA2L1 Team
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

#include <kernel/kernel.h>
#include <services/ui/plugins/notenof.h>

#include <common/log.h>
#include <utils/err.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace eka2l1::epoc::notifier {
    static std::string byte_preview(const std::uint8_t *data, const std::uint32_t size) {
        std::string result;
        const std::uint32_t count = std::min<std::uint32_t>(size, 192);

        for (std::uint32_t i = 0; i < count; i++) {
            char byte_text[4] = {};
            std::snprintf(byte_text, sizeof(byte_text), "%02X", data[i]);

            if (!result.empty()) {
                result += ' ';
            }

            result += byte_text;
        }

        if (count < size) {
            result += " ...";
        }

        return result;
    }

    static std::string ascii_strings_preview(const std::uint8_t *data, const std::uint32_t size) {
        std::string result;
        std::string current;

        for (std::uint32_t i = 0; i <= size; i++) {
            const bool printable = (i < size) && std::isprint(static_cast<unsigned char>(data[i])) && data[i] != '\\';

            if (printable) {
                current += static_cast<char>(data[i]);
                continue;
            }

            if (current.size() >= 3) {
                if (!result.empty()) {
                    result += " | ";
                }

                result += current;
            }

            current.clear();
        }

        return result;
    }

    static std::string utf16_strings_preview(const std::uint8_t *data, const std::uint32_t size) {
        std::string result;

        for (std::uint32_t start = 0; start < 2; start++) {
            std::string current;

            for (std::uint32_t i = start; i + 1 < size; i += 2) {
                const std::uint16_t ch = static_cast<std::uint16_t>(data[i] | (data[i + 1] << 8));

                if ((ch >= 0x20) && (ch < 0x7F) && (ch != '\\')) {
                    current += static_cast<char>(ch);
                    continue;
                }

                if (current.size() >= 3) {
                    if (!result.empty()) {
                        result += " | ";
                    }

                    result += current;
                }

                current.clear();
            }
        }

        return result;
    }

    void note_display_plugin::handle(epoc::desc8 *request, epoc::des8 *respone, epoc::notify_info &complete_info) {
        if (outstanding_) {
            complete_info.complete(epoc::error_in_use);
            return;
        }

        kernel::process *pr_req = complete_info.requester->owning_process();

        std::uint8_t *data_ptr = reinterpret_cast<std::uint8_t *>(request->get_pointer(pr_req));
        std::uint32_t data_size = request->get_length();

        if (!data_ptr || !data_size) {
            complete_info.complete(epoc::error_argument);
            return;
        }

        common::chunkyseri seri(data_ptr, data_size, common::chunkyseri_mode::SERI_MODE_READ);

        if (kern_->is_eka1()) {
            std::uint16_t type = 0;
            char unk = 0;

            seri.absorb(type);
            seri.absorb(unk);

            std::string to_display(reinterpret_cast<const char *>(data_ptr + sizeof(std::uint16_t) + sizeof(char)));
            LOG_TRACE(SERVICE_UI, "Trying to display dialog type: {} with message: {}", type, to_display);

            if (callback_) {
                callback_(static_cast<note_type>(type), to_display, complete_info);
                outstanding_ = true;
            } else {
                complete_info.complete(epoc::error_none);
                outstanding_ = false;
            }
        } else {
            LOG_INFO(SERVICE_UI, "EKA2 note display request: size={}, hex=[{}], ascii=[{}], utf16=[{}]",
                data_size, byte_preview(data_ptr, data_size), ascii_strings_preview(data_ptr, data_size),
                utf16_strings_preview(data_ptr, data_size));

            complete_info.complete(epoc::error_none);
            outstanding_ = false;
        }
    }

    void note_display_plugin::cancel() {
        if (!outstanding_) {
            return;
        }

        outstanding_ = false;
        if (cancel_callback_) {
            cancel_callback_();
        }
    }
}
