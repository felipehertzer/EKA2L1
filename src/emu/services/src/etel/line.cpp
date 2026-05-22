/*
 * Copyright (c) 2020 EKA2L1 Team.
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

#include <services/context.h>
#include <services/etel/line.h>
#include <services/etel/subsess.h>
#include <utils/err.h>

#include <common/log.h>

namespace eka2l1 {
    etel_line::etel_line(const epoc::etel_line_info &info, const std::string &name, const std::uint32_t caps)
        : info_(info)
        , caps_(caps)
        , name_(name) {
    }

    etel_line::~etel_line() {
    }

    etel_line_subsession::etel_line_subsession(etel_session *session, etel_line *line, const etel_legacy_level lvl)
        : etel_subsession(session, lvl)
        , line_(line) {
    }

    void etel_line_subsession::dispatch(service::ipc_context *ctx) {
        if (legacy_level_ <= ETEL_LEGACY_LEVEL_TRANSITION) {
            switch (ctx->msg->function) {
            case epoc::etel_old_line_get_info:
                get_info(ctx);
                break;

            case epoc::etel_old_line_notify_hook_change:
                notify_hook_change(ctx);
                break;

            case epoc::etel_old_line_notify_hook_change_cancel:
                cancel_notify_hook_change(ctx);
                break;

            case epoc::etel_old_line_notify_call_added:
                notify_call_added(ctx);
                break;

            case epoc::etel_old_line_notify_call_added_cancel:
                cancel_notify_call_added(ctx);
                break;

            case epoc::etel_old_line_notify_cap_changes:
                notify_caps_change(ctx);
                break;

            case epoc::etel_old_line_notify_cap_changes_cancel:
                cancel_notify_caps_change(ctx);
                break;

            case epoc::etel_old_line_get_caps:
                get_caps(ctx);
                break;

            case epoc::etel_old_line_get_status:
                get_status(ctx);
                break;

            case epoc::etel_old_line_get_hook_status:
                get_hook_status(ctx);
                break;

            case epoc::etel_old_line_notify_incoming_call:
                notify_incoming_call(ctx);
                break;

            case epoc::etel_old_line_notify_incoming_call_cancel:
                cancel_notify_incoming_call(ctx);
                break;

            case epoc::etel_old_line_notify_status_change:
                notify_status_change(ctx);
                break;

            case epoc::etel_old_line_notify_status_change_cancel:
                cancel_notify_status_change(ctx);
                break;

            default:
                LOG_ERROR(SERVICE_ETEL, "Unimplemented etel line opcode {}", ctx->msg->function);
                ctx->complete(epoc::error_not_supported);
                break;
            }
        } else {
            switch (ctx->msg->function) {
            case epoc::etel_line_get_info:
                get_info(ctx);
                break;

            case epoc::etel_line_get_status:
            case epoc::etel_mobile_line_get_mobile_line_status: // Note: Not the same, just stub
                get_status(ctx);
                break;

            case epoc::etel_line_get_caps:
                get_caps(ctx);
                break;

            case epoc::etel_line_get_hook_status:
                get_hook_status(ctx);
                break;

            case epoc::etel_line_notify_call_added:
                notify_call_added(ctx);
                break;

            case epoc::etel_line_cancel_notify_call_added:
                cancel_notify_call_added(ctx);
                break;

            case epoc::etel_line_notify_hook_change:
                notify_hook_change(ctx);
                break;

            case epoc::etel_line_cancel_notify_hook_change:
                cancel_notify_hook_change(ctx);
                break;

            case epoc::etel_line_notify_status_change:
                notify_status_change(ctx);
                break;

            case epoc::etel_line_cancel_notify_status_change:
                cancel_notify_status_change(ctx);
                break;

            case epoc::etel_line_notify_incoming_call:
                notify_incoming_call(ctx);
                break;

            case epoc::etel_line_cancel_notify_incoming_call:
                cancel_notify_incoming_call(ctx);
                break;

            case epoc::etel_mobile_line_notify_status_change:
                notify_status_change(ctx);
                break;

            case epoc::etel_mobile_line_cancel_notify_status_change:
                cancel_notify_status_change(ctx);
                break;

            default:
                LOG_ERROR(SERVICE_ETEL, "Unimplemented etel line opcode {}", ctx->msg->function);
                ctx->complete(epoc::error_not_supported);
                break;
            }
        }
    }

    void etel_line_subsession::get_info(service::ipc_context *ctx) {
        ctx->write_data_to_descriptor_argument<epoc::etel_line_info>(0, line_->info_);
        ctx->complete(epoc::error_none);
    }

    void etel_line_subsession::get_caps(service::ipc_context *ctx) {
        if (!ctx->write_data_to_descriptor_argument<std::uint32_t>(0, line_->caps_)) {
            ctx->complete(epoc::error_argument);
            return;
        }

        ctx->complete(epoc::error_none);
    }

    void etel_line_subsession::get_status(service::ipc_context *ctx) {
        if (ctx->msg->function == epoc::etel_mobile_line_get_mobile_line_status) {
            LOG_TRACE(SERVICE_ETEL, "Mobile line get status stubbed with normal get status");
        }

        ctx->write_data_to_descriptor_argument<epoc::etel_line_status>(0, line_->info_.sts_);
        ctx->complete(epoc::error_none);
    }

    void etel_line_subsession::get_hook_status(service::ipc_context *ctx) {
        ctx->write_data_to_descriptor_argument<epoc::etel_line_hook_sts>(0, line_->info_.hook_sts_);
        ctx->complete(epoc::error_none);
    }

    void etel_line_subsession::notify_hook_change(service::ipc_context *ctx) {
        hook_change_nof_ = epoc::notify_info(ctx->msg->request_sts, ctx->msg->own_thr);
    }

    void etel_line_subsession::cancel_notify_hook_change(service::ipc_context *ctx) {
        ctx->complete(epoc::error_none);
        hook_change_nof_.complete(epoc::error_cancel);
    }

    void etel_line_subsession::notify_call_added(service::ipc_context *ctx) {
        call_added_nof_ = epoc::notify_info(ctx->msg->request_sts, ctx->msg->own_thr);
    }

    void etel_line_subsession::cancel_notify_call_added(service::ipc_context *ctx) {
        ctx->complete(epoc::error_none);
        call_added_nof_.complete(epoc::error_cancel);
    }

    void etel_line_subsession::notify_caps_change(service::ipc_context *ctx) {
        caps_change_nof_ = epoc::notify_info(ctx->msg->request_sts, ctx->msg->own_thr);
    }

    void etel_line_subsession::cancel_notify_caps_change(service::ipc_context *ctx) {
        ctx->complete(epoc::error_none);
        caps_change_nof_.complete(epoc::error_cancel);
    }

    void etel_line_subsession::notify_status_change(service::ipc_context *ctx) {
        status_change_nof_ = epoc::notify_info(ctx->msg->request_sts, ctx->msg->own_thr);
    }

    void etel_line_subsession::cancel_notify_status_change(service::ipc_context *ctx) {
        ctx->complete(epoc::error_none);
        status_change_nof_.complete(epoc::error_cancel);
    }

    void etel_line_subsession::notify_incoming_call(service::ipc_context *ctx) {
        incoming_call_nof_ = epoc::notify_info(ctx->msg->request_sts, ctx->msg->own_thr);
    }

    void etel_line_subsession::cancel_notify_incoming_call(service::ipc_context *ctx) {
        ctx->complete(epoc::error_none);
        incoming_call_nof_.complete(epoc::error_cancel);
    }
}
