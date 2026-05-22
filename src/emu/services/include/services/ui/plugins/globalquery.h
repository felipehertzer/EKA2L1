/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#pragma once

#include <services/notifier/plugin.h>

#include <cstdint>
#include <string>

namespace eka2l1::epoc::notifier {
    class global_confirmation_query_plugin : public plugin_base {
        bool outstanding_;

    public:
        explicit global_confirmation_query_plugin(kernel_system *kern)
            : plugin_base(kern)
            , outstanding_(false) {
        }

        ~global_confirmation_query_plugin() override {
        }

        epoc::uid unique_id() const override {
            return 0x101F467A;
        }

        void handle(epoc::desc8 *request, epoc::des8 *respone, epoc::notify_info &complete_info) override;
        void cancel() override;
    };
}
