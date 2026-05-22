/*
 * Copyright (c) 2022 EKA2L1 Team
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

#pragma once

#include <services/framework.h>
#include <services/ui/appserver.h>

#include <cstdint>
#include <memory>

namespace eka2l1 {
    class alf_app_session : public app_ui_based_session {
    private:
        struct pending_alf_event {
            std::unique_ptr<service::ipc_context> context_;

            void request(service::ipc_context *ctx);
            void cancel();
        };

        int refresh_mode_;
        int idle_threshold_;
        int parent_window_group_id_;
        int texture_owner_process_id_;
        bool visible_;

        pending_alf_event pointer_event_;
        pending_alf_event system_event_;
        pending_alf_event texture_info_event_;
        pending_alf_event screen_buffer_event_;

        bool write_int_result(service::ipc_context *ctx, const int argument_index, const int value);
        void handle_create_subsession(service::ipc_context *ctx);
        void handle_close_subsession(service::ipc_context *ctx);
        void handle_subsession_command(service::ipc_context *ctx);
        void handle_texture_command(service::ipc_context *ctx);

    public:
        explicit alf_app_session(service::typical_server *svr, kernel::uid client_ss_uid, epoc::version client_version);
        ~alf_app_session() override;

        void fetch(service::ipc_context *ctx) override;
    };

    class alf_app_server : public app_ui_based_server {
    public:
        explicit alf_app_server(system *sys, std::uint32_t server_differentiator);
        void connect(service::ipc_context &ctx) override;
    };

    class alf_streamer_session : public service::typical_session {
    private:
        int screen_number_;
        int composition_source_handle_;

        void complete_native_window_handles(service::ipc_context *ctx);
        void handle_composition_op(service::ipc_context *ctx, const int opcode);

    public:
        explicit alf_streamer_session(service::typical_server *svr, kernel::uid client_ss_uid, epoc::version client_version);
        void fetch(service::ipc_context *ctx) override;
    };

    class alf_streamer_server : public service::typical_server {
    public:
        static constexpr std::uint32_t app_uid = 0x10282845;
        static constexpr std::uint32_t service_uid = 0x10282847;
        static constexpr const char *server_name = "alfstreamerserver";
        static constexpr const char16_t *host_launch_name = u"alfstreamerserver";

        explicit alf_streamer_server(system *sys);
        void connect(service::ipc_context &context) override;
    };
}
