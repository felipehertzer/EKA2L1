/*
 * Copyright (c) 2021 EKA2L1 Team
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

#include <services/internet/protocols/overall.h>

#if EKA2L1_ENABLE_NETWORKING
#include <services/internet/protocols/inet.h>
#include <services/socket/server.h>

#include <common/log.h>

#if EKA2L1_PLATFORM(WIN32)
#include <ws2tcpip.h>
#endif
#endif

namespace eka2l1::epoc::internet {
#if EKA2L1_ENABLE_NETWORKING
    inet_bridged_protocol::inet_bridged_protocol(kernel_system *kern, const bool oldarch)
        : socket::protocol(oldarch)
        , looper_(libuv::default_looper)
        , kern_(kern) {
#if EKA2L1_PLATFORM(WIN32)
        WSADATA init_data;
        WSAStartup(MAKEWORD(2, 0), &init_data);
#endif
    }

    void add_internet_stack_protocols(socket_server *sock, const bool oldarch) {
        std::unique_ptr<epoc::socket::protocol> inet_br_pr = std::make_unique<inet_bridged_protocol>(
            sock->get_kernel_object_owner(), oldarch);

        if (!sock->add_protocol(inet_br_pr)) {
            LOG_ERROR(SERVICE_BLUETOOTH, "Failed to add INET bridged protocol");
        }
    }
#else
    void add_internet_stack_protocols(socket_server *sock, const bool oldarch) {
        (void)sock;
        (void)oldarch;
    }
#endif
}
