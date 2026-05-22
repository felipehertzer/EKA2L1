
/*
 * Copyright (c) 2023 EKA2L1 Team
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

#include <services/applist/applist.h>
#include <services/audio/alf/alf.h>
#include <services/host_launch.h>
#include <services/internet/browser.h>
#include <services/ui/appserver.h>

#include <kernel/kernel.h>
#include <system/epoc.h>

#include <common/applauncher.h>
#include <common/log.h>
#include <utils/apacmd.h>

namespace eka2l1::service {
    static constexpr epoc::uid BROWSER_APP_UID = 0x10008D39;
    static const std::u16string BROWSER_APP_HOST_NAME_MAP = u"browser";

    using handle_host_launch_callback = std::function<bool(kernel::process *, const epoc::apa::command_line &, const std::u16string &, epoc::uid)>;

    struct host_launch_entry {
        epoc::uid app_uid;
        handle_host_launch_callback callback;
    };

    bool handle_launch_browser(kernel::process *pr, const epoc::apa::command_line &cmd_line, const std::u16string &, epoc::uid) {
        kernel_system *kern = pr->get_kernel_object_owner();

        if (cmd_line.server_differentiator_ != 0) {
            kern->create_no_kernel_param_and_add_thread<browser_for_app_server>(kernel::owner_type::process, pr->get_primary_thread(),
                kern->get_system(), cmd_line.server_differentiator_);
        }

        if (!cmd_line.document_name_.empty()) {
            // Launch right away, else it will probably send a command to the server to launch later.
            return common::launch_browser(common::ucs2_to_utf8(cmd_line.document_name_));
        }

        return true;
    }

    bool handle_launch_hle_server(kernel::process *pr, const epoc::apa::command_line &cmd_line, const std::u16string &run_command,
        epoc::uid app_uid) {
        kernel_system *kern = pr->get_kernel_object_owner();

        if (!kern->get_by_name<service::server>(common::ucs2_to_utf8(run_command))) {
            return false;
        }

        if (app_uid != 0) {
            const std::uint32_t server_differentiator = (cmd_line.server_differentiator_ != 0)
                ? cmd_line.server_differentiator_
                : app_uid;
            const std::string app_server_name = app_ui_based_server::server_name(server_differentiator, app_uid);
            if (!kern->get_by_name<service::server>(app_server_name)) {
                kern->create_no_kernel_param_and_add_thread<app_ui_based_server>(
                    kernel::owner_type::process, pr->get_primary_thread(), kern->get_system(), server_differentiator, app_uid);
            }
        }

        return true;
    }

    bool handle_launch_alf_server(kernel::process *pr, const epoc::apa::command_line &cmd_line, const std::u16string &run_command,
        epoc::uid app_uid) {
        kernel_system *kern = pr->get_kernel_object_owner();

        if (!kern->get_by_name<service::server>(common::ucs2_to_utf8(run_command))) {
            return false;
        }

        const std::uint32_t server_differentiator = (cmd_line.server_differentiator_ != 0)
            ? cmd_line.server_differentiator_
            : app_uid;
        const std::string app_server_name = app_ui_based_server::server_name(server_differentiator, app_uid);
        if (!kern->get_by_name<service::server>(app_server_name)) {
            kern->create_no_kernel_param_and_add_thread<alf_app_server>(
                kernel::owner_type::process, pr->get_primary_thread(), kern->get_system(), server_differentiator);
        }

        return true;
    }

    static const std::map<std::u16string, host_launch_entry> HANDLE_HOST_LAUNCH_CALLBACKS_LOOKUP = {
        { BROWSER_APP_HOST_NAME_MAP, { BROWSER_APP_UID, handle_launch_browser } },
        { alf_streamer_server::host_launch_name, { alf_streamer_server::app_uid, handle_launch_alf_server } }
    };

    void init_symbian_app_launch_to_host_launch(system *sys) {
        kernel_system *kern = sys->get_kernel_system();

        applist_server *serv = kern->get_by_name<applist_server>(
            get_app_list_server_name_by_epocver(kern->get_epoc_version()));

        if (!serv) {
            LOG_ERROR(SERVICE_TRACK, "Unable to initialize Symbian app launch to host app launch: Applist server is not available!");
            return;
        }

        for (const auto &host_launch_callback : HANDLE_HOST_LAUNCH_CALLBACKS_LOOKUP) {
            serv->add_app_uid_to_host_launch_name(host_launch_callback.second.app_uid, host_launch_callback.first);
        }

        kern->register_guomen_process_run_callback([](kernel::process *pr) {
            std::optional<kernel::pass_arg> launch_arg_raw = pr->get_arg_slot(epoc::apa::PROCESS_ENVIRONMENT_ARG_SLOT_MAIN);
            epoc::apa::command_line launch_cmdline;
            std::u16string run_command;

            if (launch_arg_raw.has_value()) {
                common::chunkyseri seri(launch_arg_raw->data.data(), launch_arg_raw->data.size(),
                    common::SERI_MODE_READ);

                launch_cmdline.do_it_newarch(seri);
                run_command = launch_cmdline.executable_path_;
            } else {
                run_command = pr->get_cmd_args();
            }

            if (std::optional<std::u16string> mapped_executable_name = parse_mapped_executable_name(run_command)) {
                run_command = *mapped_executable_name;
            }

            auto launch_callback_ite = HANDLE_HOST_LAUNCH_CALLBACKS_LOOKUP.find(run_command);
            if (launch_callback_ite == HANDLE_HOST_LAUNCH_CALLBACKS_LOOKUP.end()) {
                LOG_ERROR(SERVICE_APPLIST, "No host launcher correspond for launch command: {}", common::ucs2_to_utf8(run_command));
                return false;
            }

            return launch_callback_ite->second.callback(pr, launch_cmdline, run_command, launch_callback_ite->second.app_uid);
        });
    }
}
