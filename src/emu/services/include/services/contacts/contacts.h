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

#pragma once

#include <services/framework.h>

#include <cstdint>
#include <memory>

namespace eka2l1 {
    class contacts_server : public service::typical_server {
        std::uint32_t next_connection_id_ = 1;

    public:
        explicit contacts_server(eka2l1::system *sys);

        void connect(service::ipc_context &context) override;
        std::uint32_t allocate_connection_id();
    };

    class contacts_session : public service::typical_session {
        std::uint32_t connection_id_;
        std::unique_ptr<service::ipc_context> event_request_;

    public:
        explicit contacts_session(service::typical_server *serv, const kernel::uid ss_id, epoc::version client_version);
        ~contacts_session() override;

        void fetch(service::ipc_context *ctx) override;
    };
}
