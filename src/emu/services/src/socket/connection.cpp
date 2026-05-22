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

#include <services/socket/common.h>
#include <services/socket/connection.h>
#include <services/socket/server.h>
#include <services/socket/socket.h>

#include <common/cvt.h>
#include <common/log.h>
#include <system/epoc.h>
#include <utils/err.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace eka2l1::epoc::socket {
    connection::connection(protocol *pr, saddress dest)
        : pr_(pr)
        , sock_(nullptr)
        , dest_(dest) {
    }

    std::size_t connection::register_progress_advance_callback(progress_advance_callback cb) {
        return progress_callbacks_.add(cb);
    }

    bool connection::remove_progress_advance_callback(const std::size_t handle) {
        return progress_callbacks_.remove(handle);
    }

    socket_connection_proxy::socket_connection_proxy(socket_client_session *parent, connection *conn)
        : socket_subsession(parent)
        , conn_(conn)
        , progress_reported_(false) {
    }

    void socket_connection_proxy::start(service::ipc_context *ctx) {
        if (conn_) {
            ctx->complete(epoc::error_none);
            return;
        }

        conn_preferences prefs{};
        if (auto supplied_prefs = ctx->get_argument_data_from_descriptor<conn_preferences>(0, true)) {
            prefs = supplied_prefs.value();
        }

        connect_agent *agent = parent_->server<socket_server>()->get_connect_agent(u"GenConn");
        if (!agent) {
            LOG_ERROR(SERVICE_ESOCK, "Generic connection agent is not registered");
            ctx->complete(epoc::error_not_found);
            return;
        }

        owned_conn_ = agent->start_connection(prefs);
        conn_ = owned_conn_.get();
        progress_reported_ = false;

        if (!conn_) {
            ctx->complete(epoc::error_not_ready);
            return;
        }

        ctx->complete(epoc::error_none);
    }

    void socket_connection_proxy::stop(service::ipc_context *ctx) {
        owned_conn_.reset();
        conn_ = nullptr;
        progress_reported_ = false;
        ctx->complete(epoc::error_none);
    }

    void socket_connection_proxy::get_setting(service::ipc_context *ctx, const setting_type type, const int result_slot) {
        if (!conn_) {
            ctx->complete(epoc::error_not_ready);
            return;
        }

        const auto setting_name = ctx->get_argument_value<std::u16string>(0);
        if (!setting_name) {
            ctx->complete(epoc::error_argument);
            return;
        }

        const std::size_t max_size = ctx->get_argument_max_data_size(result_slot);
        if (max_size == static_cast<std::size_t>(-1)) {
            ctx->complete(epoc::error_argument);
            return;
        }

        std::array<std::uint8_t, 512> setting_data{};
        const std::size_t write_size = conn_->get_setting(setting_name.value(), type, setting_data.data(),
            std::min(max_size, setting_data.size()));

        if (write_size == static_cast<std::size_t>(-1)) {
            LOG_ERROR(SERVICE_ESOCK, "Connection setting {} is unavailable", common::ucs2_to_utf8(setting_name.value()));
            ctx->complete(epoc::error_not_found);
            return;
        }

        int write_error = 0;
        if (!ctx->write_data_to_descriptor_argument(result_slot, setting_data.data(), static_cast<std::uint32_t>(write_size),
                &write_error)) {
            if ((write_error == -1) && (write_size == sizeof(std::uint32_t))) {
                std::uint32_t int_value = 0;
                std::memcpy(&int_value, setting_data.data(), sizeof(int_value));
                if (ctx->write_arg(result_slot, int_value)) {
                    ctx->complete(epoc::error_none);
                    return;
                }
            }

            ctx->complete(epoc::error_argument);
            return;
        }

        ctx->complete(epoc::error_none);
    }

    void socket_connection_proxy::progress_notify(service::ipc_context *ctx) {
        if (!progress_reported_) {
            epoc::socket::conn_progress progress;
            progress.error_ = conn_ ? epoc::error_none : epoc::error_not_ready;
            progress.stage_ = conn_ ? epoc::socket::conn_progress_connection_opened : epoc::socket::conn_progress_connection_closed;

            ctx->write_data_to_descriptor_argument<epoc::socket::conn_progress>(0, progress);
            ctx->complete(progress.error_);

            progress_reported_ = true;

            LOG_TRACE(SERVICE_ESOCK, "Connection progress notification completed with stage={} error={}", progress.stage_,
                progress.error_);
        }

        // Keep later notifications pending until a real state transition is available.
    }

    void socket_connection_proxy::dispatch(service::ipc_context *ctx) {
        if (parent_->is_oldarch()) {
            switch (ctx->msg->function) {
            default:
                LOG_ERROR(SERVICE_ESOCK, "Unimplemented socket connection opcode: {}", ctx->msg->function);
                ctx->complete(epoc::error_none);

                break;
            }
        } else {
            if (ctx->sys->get_symbian_version_use() >= epocver::epoc95) {
                switch (ctx->msg->function) {
                case socket_cn_start:
                    start(ctx);
                    break;

                case socket_cn_stop:
                    stop(ctx);
                    break;

                case socket_cn_progress_notification:
                    progress_notify(ctx);
                    break;

                case socket_cn_get_int_setting:
                    get_setting(ctx, setting_type_int, 1);
                    break;

                case socket_cn_get_des_setting:
                case socket_cn_get_long_des_setting:
                    get_setting(ctx, setting_type_des, 1);
                    break;

                default:
                    LOG_ERROR(SERVICE_ESOCK, "Unimplemented socket connection opcode: {}", ctx->msg->function);
                    ctx->complete(epoc::error_none);

                    break;
                }
            } else {
                switch (ctx->msg->function) {
                case socket_cm_api_ext_interface_send_receive:
                    // Async, but we should complete it in sometimes
                    // Complete with not right result will create stuck or crash sometimes
                    break;

                case socket_cn_start:
                    start(ctx);
                    break;

                case socket_cn_stop:
                    stop(ctx);
                    break;

                case socket_cn_progress_notification:
                    progress_notify(ctx);
                    break;

                case socket_cn_get_int_setting:
                    get_setting(ctx, setting_type_int, 1);
                    break;

                case socket_cn_get_des_setting:
                case socket_cn_get_long_des_setting:
                    get_setting(ctx, setting_type_des, 1);
                    break;

                default:
                    LOG_ERROR(SERVICE_ESOCK, "Unimplemented socket connection opcode: {}", ctx->msg->function);
                    ctx->complete(epoc::error_none);

                    break;
                }
            }
        }
    }
}
