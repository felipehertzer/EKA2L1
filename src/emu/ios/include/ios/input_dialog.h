/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#pragma once

#include <drivers/ui/input_dialog.h>

namespace eka2l1::ios {
    void finish_text_input(const std::u16string &text);
    void finish_question_dialog(int result);
}
