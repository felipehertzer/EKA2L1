/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#include <drivers/ui/input_dialog.h>

namespace eka2l1::drivers::ui {
    bool __attribute__((weak)) open_input_view(const std::u16string &initial_text, const int max_len,
        input_dialog_complete_callback complete_callback) {
        std::u16string result = initial_text;
        if (max_len >= 0 && result.size() > static_cast<std::size_t>(max_len)) {
            result.resize(static_cast<std::size_t>(max_len));
        }

        if (complete_callback) {
            complete_callback(result);
        }
        return true;
    }

    void __attribute__((weak)) close_input_view() {
    }

    void __attribute__((weak)) show_yes_no_dialog(const std::u16string &text, const std::u16string &button1_text,
        const std::u16string &button2_text, yes_no_dialog_complete_callback complete_callback) {
        (void)text;
        (void)button1_text;
        (void)button2_text;
        if (complete_callback) {
            complete_callback(0);
        }
    }
}
