/*
 * Copyright (c) 2026 EKA2L1 Team
 *
 * This file is part of EKA2L1 project
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

#include <services/contacts/contacts.h>
#include <system/epoc.h>

#include <utils/err.h>

#include <cstdint>
#include <string_view>

namespace eka2l1 {
    static constexpr const char *CONTACTS_SERVER_NAME = "CNTSRV";

    namespace {
        // Keep these in sync with Symbian Contacts Model's TCntClientIpcCodes.
        // The high ranges are grouped by platform capability in the original server.
        enum contacts_opcode {
            cnt_resource_count = 0x10,
            cnt_db_contact_count = 0x11,
            cnt_request_event = 0x15,
            cnt_cancel_event_request = 0x16,
            cnt_connection_id = 0x17,
            cnt_close_database = 0x1D,
            cnt_cancel_async_open_database = 0x1E,
            cnt_set_sort_prefs = 0x27,
            cnt_fetch_template_ids = 0x29,
            cnt_fetch_group_id_lists = 0x2A,
            cnt_change_view_def = 0x2C,
            cnt_get_sort_prefs = 0x2D,
            cnt_item_close = 0x2E,
            cnt_get_database_ready = 0x31,
            cnt_predictive_search_list = 0x62,
            cnt_search_result_list = 0x63,
            cnt_open_database = 0x64
        };

        constexpr std::uint32_t EMPTY_RESULT_SENTINEL = 0;

        std::string_view contacts_opcode_name(const int opcode) {
            switch (opcode) {
            case cnt_resource_count:
                return "ECntResourceCount";
            case cnt_db_contact_count:
                return "ECntDbContactCount";
            case cnt_request_event:
                return "ECntRequestEvent";
            case cnt_cancel_event_request:
                return "ECntCancelEventRequest";
            case cnt_connection_id:
                return "ECntConnectionId";
            case cnt_close_database:
                return "ECntCloseDataBase";
            case cnt_cancel_async_open_database:
                return "ECntCancelAsyncOpenDatabase";
            case cnt_set_sort_prefs:
                return "ECntSetSortPrefs";
            case cnt_fetch_template_ids:
                return "ECntFetchTemplateIds";
            case cnt_fetch_group_id_lists:
                return "ECntFetchGroupIdLists";
            case cnt_change_view_def:
                return "ECntChangeViewDef";
            case cnt_get_sort_prefs:
                return "ECntGetSortPrefs";
            case cnt_item_close:
                return "ECntItemClose";
            case cnt_get_database_ready:
                return "ECntGetDatabaseReady";
            case cnt_predictive_search_list:
                return "ECntPredictiveSearchList";
            case cnt_search_result_list:
                return "ECntSearchResultList";
            case cnt_open_database:
                return "ECntOpenDataBase";
            default:
                return "unknown";
            }
        }

        void trace_contacts_ipc(service::ipc_context *ctx) {
            const auto &args = ctx->msg->args.args;
            LOG_TRACE(SERVICE_TRACK,
                "CNTSRV opcode 0x{:X} ({}) flag=0x{:X} args=[0x{:X}, 0x{:X}, 0x{:X}, 0x{:X}]",
                ctx->msg->function, contacts_opcode_name(ctx->msg->function), ctx->flag(),
                static_cast<std::uint32_t>(args[0]), static_cast<std::uint32_t>(args[1]),
                static_cast<std::uint32_t>(args[2]), static_cast<std::uint32_t>(args[3]));
        }

        void complete_empty_id_stream(service::ipc_context *ctx, const int slot) {
            if (ctx->get_argument_max_data_size(slot) < sizeof(EMPTY_RESULT_SENTINEL)) {
                ctx->complete(sizeof(EMPTY_RESULT_SENTINEL));
                return;
            }

            int write_error = 0;
            if (!ctx->write_data_to_descriptor_argument(slot, EMPTY_RESULT_SENTINEL, &write_error)) {
                LOG_WARN(SERVICE_TRACK, "CNTSRV failed to write empty result descriptor: {}", write_error);
                ctx->complete(epoc::error_argument);
                return;
            }

            ctx->complete(epoc::error_none);
        }
    }

    contacts_server::contacts_server(eka2l1::system *sys)
        : service::typical_server(sys, CONTACTS_SERVER_NAME) {
    }

    void contacts_server::connect(service::ipc_context &context) {
        create_session<contacts_session>(&context);
        context.complete(epoc::error_none);
    }

    std::uint32_t contacts_server::allocate_connection_id() {
        return next_connection_id_++;
    }

    contacts_session::contacts_session(service::typical_server *serv, const kernel::uid ss_id, epoc::version client_version)
        : service::typical_session(serv, ss_id, client_version)
        , connection_id_(server<contacts_server>()->allocate_connection_id()) {
    }

    contacts_session::~contacts_session() {
        if (event_request_) {
            event_request_->complete(epoc::error_cancel);
        }
    }

    void contacts_session::fetch(service::ipc_context *ctx) {
        trace_contacts_ipc(ctx);

        switch (ctx->msg->function) {
        case cnt_connection_id:
            ctx->complete(connection_id_);
            break;

        case cnt_request_event:
            if (event_request_) {
                event_request_->complete(epoc::error_cancel);
            }
            event_request_ = ctx->move_to_new();
            break;

        case cnt_cancel_event_request:
            if (event_request_) {
                event_request_->complete(epoc::error_cancel);
                event_request_.reset();
            }
            ctx->complete(epoc::error_none);
            break;

        case cnt_open_database:
        case cnt_close_database:
        case cnt_cancel_async_open_database:
        case cnt_change_view_def:
        case cnt_item_close:
        case cnt_set_sort_prefs:
            ctx->complete(epoc::error_none);
            break;

        case cnt_resource_count:
        case cnt_db_contact_count:
            ctx->complete(0);
            break;

        case cnt_get_database_ready:
            ctx->complete(1);
            break;

        case cnt_fetch_template_ids:
        case cnt_fetch_group_id_lists:
        case cnt_get_sort_prefs:
        case cnt_predictive_search_list:
        case cnt_search_result_list:
            complete_empty_id_stream(ctx, 0);
            break;

        default:
            LOG_WARN(SERVICE_TRACK, "CNTSRV opcode 0x{:X} ({}) is not implemented, returning KErrNotSupported",
                ctx->msg->function, contacts_opcode_name(ctx->msg->function));
            ctx->complete(epoc::error_not_supported);
            break;
        }
    }
}
