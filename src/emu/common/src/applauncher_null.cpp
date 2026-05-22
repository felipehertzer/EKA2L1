/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#include <common/applauncher.h>

namespace eka2l1::common {
    bool __attribute__((weak)) launch_browser(const std::string &url) {
        (void)url;
        return false;
    }
}
