/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#include <kernel/kernel.h>
#include <services/ui/plugins/globalquery.h>

#include <common/log.h>
#include <drivers/ui/input_dialog.h>
#include <utils/err.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace eka2l1::epoc::notifier {
    static std::string confirmation_query_byte_preview(const std::uint8_t *data, const std::uint32_t size) {
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

    static std::vector<std::u16string> extract_utf16_printable_runs(const std::uint8_t *data, const std::uint32_t size) {
        std::vector<std::u16string> result;

        for (std::uint32_t start = 0; start < 2; start++) {
            std::u16string current;

            for (std::uint32_t i = start; i + 1 < size; i += 2) {
                const char16_t ch = static_cast<char16_t>(data[i] | (data[i + 1] << 8));

                if (((ch >= 0x20) && (ch < 0xD800)) || (ch == u'\n') || (ch == u'\r') || (ch == u'\t')) {
                    current.push_back(ch);
                    continue;
                }

                if (current.size() >= 3) {
                    result.push_back(current);
                }

                current.clear();
            }

            if (current.size() >= 3) {
                result.push_back(current);
            }
        }

        return result;
    }

    static std::u16string query_text_from_request(const std::uint8_t *data, const std::uint32_t size) {
        const std::vector<std::u16string> strings = extract_utf16_printable_runs(data, size);
        if (strings.empty()) {
            return {};
        }

        return *std::max_element(strings.begin(), strings.end(), [](const std::u16string &lhs, const std::u16string &rhs) {
            return lhs.size() < rhs.size();
        });
    }

    static void write_confirmation_result(kernel::process *process, epoc::des8 *response, const std::uint8_t result) {
        if (!process || !response || !response->get_pointer_raw(process) || (response->get_max_length(process) == 0)) {
            return;
        }

        response->assign(process, &result, sizeof(result));
    }

    void global_confirmation_query_plugin::handle(epoc::desc8 *request, epoc::des8 *respone, epoc::notify_info &complete_info) {
        if (outstanding_) {
            complete_info.complete(epoc::error_in_use);
            return;
        }

        if (!request) {
            complete_info.complete(epoc::error_argument);
            return;
        }

        kernel::process *process = complete_info.requester->owning_process();
        std::uint8_t *request_ptr = reinterpret_cast<std::uint8_t *>(request->get_pointer(process));
        const std::uint32_t request_size = request->get_length();

        if (request_size && !request_ptr) {
            complete_info.complete(epoc::error_argument);
            return;
        }

        const std::u16string text = request_size ? query_text_from_request(request_ptr, request_size) : std::u16string();

        LOG_INFO(SERVICE_UI, "AVKON global confirmation query: request_size={}, response_max={}, hex=[{}]",
            request_size, respone ? respone->get_max_length(process) : 0,
            request_ptr ? confirmation_query_byte_preview(request_ptr, request_size) : std::string());

        if (text.empty()) {
            const std::uint8_t accepted = 1;
            write_confirmation_result(process, respone, accepted);
            complete_info.complete(epoc::error_none);
            outstanding_ = false;
            return;
        }

        outstanding_ = true;
        drivers::ui::show_yes_no_dialog(text, u"No", u"Yes", [this, process, respone, complete_info](const int value) mutable {
            const std::uint8_t accepted = value ? 1 : 0;
            write_confirmation_result(process, respone, accepted);

            kernel_system *kern = complete_info.requester->get_kernel_object_owner();
            kern->lock();
            complete_info.complete(epoc::error_none);
            kern->unlock();

            outstanding_ = false;
        });
    }

    void global_confirmation_query_plugin::cancel() {
        outstanding_ = false;
    }
}
