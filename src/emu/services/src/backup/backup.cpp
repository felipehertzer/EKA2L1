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

#include <services/backup/backup.h>
#include <system/epoc.h>
#include <utils/err.h>

#include <cstdint>
#include <string>

namespace eka2l1 {
    namespace {
        enum old_backup_opcode {
            old_backup_event_ready = 0x14,
            old_backup_get_event = 0x15,
            old_backup_close_all_files = 0x16,
            old_backup_restart_all = 0x17,
            old_backup_close_file = 0x18,
            old_backup_restart_file = 0x19,
            old_backup_notify_lock_change = 0x1A,
            old_backup_notify_lock_change_cancel = 0x1B,
            old_backup_close_server = 0x1C,
            old_backup_notify_backup_operation = 0x1D,
            old_backup_cancel_backup_operation_event = 0x1E,
            old_backup_get_backup_operation_state = 0x1F,
            old_backup_operation_event_ready = 0x20,
            old_backup_get_backup_operation_event = 0x21,
            old_backup_set_backup_operation_observer_present = 0x22,
            old_backup_stop_notifications = 0x23
        };

        struct old_backup_operation_attributes {
            std::int32_t file_flag;
            std::int32_t operation;
        };

        constexpr old_backup_operation_attributes NO_BACKUP_OPERATION = {
            0, // MBackupObserver::ETakeLock
            0 // MBackupOperationObserver::ENone
        };

        void complete_pending(std::unique_ptr<service::ipc_context> &ctx, const int error) {
            if (!ctx) {
                return;
            }

            ctx->complete(error);
            ctx.reset();
        }
    }

    backup_old_server::backup_old_server(eka2l1::system *sys)
        : service::typical_server(sys, sys->get_symbian_version_use() <= epocver::eka2 ? "BackupServer" : "!BackupServer") {
    }

    void backup_old_server::connect(service::ipc_context &context) {
        create_session<backup_old_session>(&context);
        context.complete(epoc::error_none);
    }

    backup_old_session::backup_old_session(service::typical_server *serv, const kernel::uid ss_id,
        epoc::version client_version)
        : service::typical_session(serv, ss_id, client_version) {
    }

    backup_old_session::~backup_old_session() {
        complete_pending(file_lock_event_msg_, epoc::error_cancel);
        complete_pending(backup_operation_event_msg_, epoc::error_cancel);
    }

    void backup_old_session::fetch(service::ipc_context *ctx) {
        switch (ctx->msg->function) {
        case old_backup_event_ready:
            if (file_lock_event_msg_) {
                ctx->complete(epoc::error_in_use);
                break;
            }

            file_lock_event_msg_ = ctx->move_to_new();
            break;

        case old_backup_get_event: {
            // BAFL's RBaBackupSession::GetEvent always reads the first UTF-16
            // character as a lock flag. Return an empty ETakeLock event instead
            // of an empty descriptor so old clients do not panic.
            const std::u16string no_lock_event(1, u'0');
            if (!ctx->write_arg(0, no_lock_event)) {
                ctx->complete(epoc::error_bad_descriptor);
                break;
            }

            ctx->complete(epoc::error_none);
            break;
        }

        case old_backup_close_all_files:
        case old_backup_restart_all:
        case old_backup_close_file:
        case old_backup_restart_file:
        case old_backup_notify_lock_change:
        case old_backup_notify_lock_change_cancel:
            ctx->complete(epoc::error_none);
            break;

        case old_backup_close_server:
            ctx->complete(epoc::error_not_supported);
            break;

        case old_backup_notify_backup_operation: {
            old_backup_operation_attributes operation = NO_BACKUP_OPERATION;
            if (auto incoming_operation = ctx->get_argument_data_from_descriptor<old_backup_operation_attributes>(0, true)) {
                operation = incoming_operation.value();
            }

            if (backup_operation_event_msg_) {
                backup_operation_event_msg_->write_data_to_descriptor_argument(0, operation, nullptr, true);
                complete_pending(backup_operation_event_msg_, epoc::error_none);
            }

            ctx->complete(epoc::error_none);
            break;
        }

        case old_backup_cancel_backup_operation_event:
            backup_operation_observer_present_ = false;
            complete_pending(backup_operation_event_msg_, epoc::error_cancel);
            ctx->complete(epoc::error_none);
            break;

        case old_backup_get_backup_operation_state: {
            const std::int32_t is_running = 0;
            ctx->write_data_to_descriptor_argument(0, is_running, nullptr, true);
            ctx->complete(epoc::error_none);
            break;
        }

        case old_backup_operation_event_ready:
            if (!backup_operation_observer_present_) {
                ctx->complete(epoc::error_none);
                break;
            }

            if (backup_operation_event_msg_) {
                ctx->complete(epoc::error_in_use);
                break;
            }

            backup_operation_event_msg_ = ctx->move_to_new();
            break;

        case old_backup_get_backup_operation_event:
            ctx->write_data_to_descriptor_argument(0, NO_BACKUP_OPERATION, nullptr, true);
            ctx->complete(epoc::error_none);
            break;

        case old_backup_set_backup_operation_observer_present:
            backup_operation_observer_present_ = ctx->get_argument_value<std::int32_t>(0).value_or(0) != 0;
            if (!backup_operation_observer_present_) {
                complete_pending(backup_operation_event_msg_, epoc::error_cancel);
            }

            ctx->complete(epoc::error_none);
            break;

        case old_backup_stop_notifications:
            complete_pending(file_lock_event_msg_, epoc::error_cancel);
            ctx->complete(epoc::error_none);
            break;

        default:
            LOG_WARN(SERVICE_BACKUP, "Unimplemented opcode for Old Backup server 0x{:X}", ctx->msg->function);
            ctx->complete(epoc::error_not_supported);
            break;
        }
    }
}
