/*
 * Copyright (c) 2018 EKA2L1 Team
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

#pragma once

#include <services/framework.h>

#include <cstdint>
#include <string>
#include <vector>

namespace eka2l1 {
    class drm_helper_server : public service::typical_server {
        friend struct drm_helper_session;

        struct automated_entry {
            std::string uri;
            std::uint8_t permission_type;
            std::uint8_t registration_type;
            std::uint8_t automated_type;
        };

        std::vector<automated_entry> automated_entries_;

        void register_content(const std::string &uri, const std::uint8_t permission_type,
            const std::uint8_t registration_type, const std::uint8_t automated_type);

        bool remove_content(const std::string &uri, const std::uint8_t permission_type,
            const std::uint8_t registration_type, const std::uint8_t automated_type);

        bool is_registered(const std::string &uri, const std::uint8_t registration_type,
            const std::uint8_t automated_type) const;

    public:
        explicit drm_helper_server(eka2l1::system *sys);
        void connect(service::ipc_context &ctx) override;
    };

    struct drm_helper_session : public service::typical_session {
        explicit drm_helper_session(service::typical_server *serv, const kernel::uid ss_id,
            epoc::version client_version);

        void fetch(service::ipc_context *ctx) override;
    };
}
