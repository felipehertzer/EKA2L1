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
#include <vector>

namespace eka2l1 {
    class agenda_server : public service::typical_server {
        std::int64_t next_file_id_ = 1;
        std::uint8_t next_collection_id_ = 1;

    public:
        explicit agenda_server(eka2l1::system *sys);

        void connect(service::ipc_context &context) override;
        std::int64_t allocate_file_id();
        std::uint8_t allocate_collection_id();
    };

    class agenda_session : public service::typical_session {
        std::int64_t file_id_ = 0;
        std::uint8_t collection_id_ = 0;
        bool entry_iterator_has_current_ = false;
        std::unique_ptr<service::ipc_context> change_request_;
        std::unique_ptr<service::ipc_context> progress_request_;
        std::vector<std::uint8_t> transmit_buffer_;

    public:
        explicit agenda_session(service::typical_server *serv, const kernel::uid ss_id, epoc::version client_version);
        ~agenda_session() override;

        void fetch(service::ipc_context *ctx) override;
    };
}
