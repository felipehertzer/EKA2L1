/*
 * Copyright (c) 2019 EKA2L1 Team
 *
 * This file is part of EKA2L1 project
 * (see bentokun.github.com/EKA2L1).
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

#include <services/window/classes/plugins/animdll.h>
#include <services/window/classes/winuser.h>
#include <services/window/op.h>
#include <services/window/window.h>
#include <utils/err.h>

namespace eka2l1::epoc {
    namespace {
        struct noop_anim_executor : public anim_executor {
            explicit noop_anim_executor(canvas_base *canvas)
                : anim_executor(canvas) {
            }

            std::int32_t handle_request(const std::int32_t opcode, void *args) override {
                (void)opcode;
                (void)args;
                return epoc::error_none;
            }
        };
    }

    anim_dll::anim_dll(window_server_client_ptr client, screen *scr, anim_executor_factory *factory)
        : window_client_obj(client, scr)
        , factory_(factory) {
    }

    bool anim_dll::execute_command(service::ipc_context &ctx, ws_cmd &cmd) {
        ws_anim_dll_opcode op = static_cast<decltype(op)>(cmd.header.op);
        bool quit = false;

        switch (op) {
        case ws_anim_dll_op_create_instance: {
            anim_create_instance_args *anim_args = reinterpret_cast<anim_create_instance_args *>(cmd.data_ptr);
            canvas_base *canvas = reinterpret_cast<canvas_base *>(client->get_object(anim_args->win_handle_));

            if (canvas == nullptr) {
                ctx.complete(epoc::error_bad_handle);
                break;
            }

            auto executor = factory_ ? factory_->new_executor(canvas, anim_args->anim_type_) : std::make_unique<noop_anim_executor>(canvas);
            if (executor == nullptr) {
                LOG_TRACE(SERVICE_WINDOW, "Can't create animation from animation factory \"{}\"!", factory_->name());
                ctx.complete(epoc::error_general);

                break;
            }

            ctx.complete(static_cast<int>(executors_.add(executor)));
            break;
        }

        case ws_anim_dll_op_command_reply: {
            anim_request_info *req_info = reinterpret_cast<anim_request_info *>(cmd.data_ptr);
            std::unique_ptr<anim_executor> *executor = executors_.get(req_info->handle_);

            if (!executor) {
                ctx.complete(epoc::error_bad_handle);
                break;
            }

            void *data_ptr = ctx.get_descriptor_argument_ptr(1);
            ctx.complete((*executor)->handle_request(req_info->opcode_, data_ptr));

            break;
        }

        case ws_anim_dll_op_free: {
            ctx.complete(epoc::error_none);
            client->delete_object(cmd.obj_handle);

            quit = true;
            break;
        }

        case ws_anim_dll_op_destroy_instance: {
            executors_.remove(*reinterpret_cast<std::uint32_t *>(cmd.data_ptr));
            ctx.complete(epoc::error_none);
            break;
        }

        default: {
            LOG_ERROR(SERVICE_WINDOW, "Unimplemented AnimDll opcode: 0x{:x}", cmd.header.op);
            ctx.complete(epoc::error_none);
            break;
        }
        }

        return quit;
    }
}
