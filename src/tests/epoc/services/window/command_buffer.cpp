/*
 * Copyright (c) 2026 EKA2L1 Team
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

#include <catch2/catch_all.hpp>

#include <services/window/op.h>
#include <services/window/window.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {
    template <typename T>
    void append(std::vector<std::uint8_t> &buffer, const T &value) {
        const std::size_t offset = buffer.size();
        buffer.resize(offset + sizeof(T));
        std::memcpy(buffer.data() + offset, &value, sizeof(T));
    }
}

TEST_CASE("window command buffer parser defaults client ops to the session handle", "[epoc][window]") {
    constexpr std::uint32_t session_handle = 0xCAFE1234;
    constexpr std::uint32_t payload = 0x10203040;

    std::vector<std::uint8_t> buffer;
    append(buffer, eka2l1::ws_cmd_header{ static_cast<std::uint16_t>(eka2l1::ws_cl_op_create_gc), static_cast<std::uint16_t>(sizeof(payload)) });
    append(buffer, payload);

    std::vector<eka2l1::ws_cmd> cmds;
    REQUIRE(eka2l1::epoc::parse_window_command_buffer(buffer.data(), buffer.size(), session_handle, cmds));

    REQUIRE(cmds.size() == 1);
    CHECK(cmds[0].header.op == eka2l1::ws_cl_op_create_gc);
    CHECK(cmds[0].header.cmd_len == sizeof(payload));
    CHECK(cmds[0].obj_handle == session_handle);

    std::uint32_t parsed_payload = 0;
    std::memcpy(&parsed_payload, cmds[0].data_ptr, sizeof(parsed_payload));
    CHECK(parsed_payload == payload);
}

TEST_CASE("window command buffer parser handles object-bound commands", "[epoc][window]") {
    constexpr std::uint32_t session_handle = 0xCAFE1234;
    constexpr std::uint32_t object_handle = 0xABCDEF01;
    constexpr std::uint16_t payload = 0x7654;

    std::vector<std::uint8_t> buffer;
    append(buffer, eka2l1::ws_cmd_header{ static_cast<std::uint16_t>(0x8000 | eka2l1::ws_cl_op_get_event), static_cast<std::uint16_t>(sizeof(payload)) });
    append(buffer, object_handle);
    append(buffer, payload);

    std::vector<eka2l1::ws_cmd> cmds;
    REQUIRE(eka2l1::epoc::parse_window_command_buffer(buffer.data(), buffer.size(), session_handle, cmds));

    REQUIRE(cmds.size() == 1);
    CHECK(cmds[0].header.op == eka2l1::ws_cl_op_get_event);
    CHECK(cmds[0].header.cmd_len == sizeof(payload));
    CHECK(cmds[0].obj_handle == object_handle);
}

TEST_CASE("window command buffer parser accepts unaligned buffers", "[epoc][window]") {
    constexpr std::uint32_t session_handle = 0x10101010;
    constexpr std::uint32_t payload = 0xA0B0C0D0;

    std::vector<std::uint8_t> storage(1, 0xFF);
    append(storage, eka2l1::ws_cmd_header{ static_cast<std::uint16_t>(eka2l1::ws_cl_op_num_window_groups), static_cast<std::uint16_t>(sizeof(payload)) });
    append(storage, payload);

    std::vector<eka2l1::ws_cmd> cmds;
    REQUIRE(eka2l1::epoc::parse_window_command_buffer(storage.data() + 1, storage.size() - 1, session_handle, cmds));

    REQUIRE(cmds.size() == 1);
    CHECK(cmds[0].header.op == eka2l1::ws_cl_op_num_window_groups);
    CHECK(cmds[0].obj_handle == session_handle);
}

TEST_CASE("window command buffer parser rejects truncated buffers", "[epoc][window]") {
    constexpr std::uint32_t session_handle = 0x10101010;

    std::vector<eka2l1::ws_cmd> cmds(1);
    std::vector<std::uint8_t> buffer;

    SECTION("partial header") {
        buffer.resize(sizeof(eka2l1::ws_cmd_header) - 1);
    }

    SECTION("missing object handle") {
        append(buffer, eka2l1::ws_cmd_header{ static_cast<std::uint16_t>(0x8000 | eka2l1::ws_cl_op_get_event), 0 });
    }

    SECTION("short payload") {
        append(buffer, eka2l1::ws_cmd_header{ static_cast<std::uint16_t>(eka2l1::ws_cl_op_create_gc), 4 });
        buffer.push_back(0);
    }

    REQUIRE_FALSE(eka2l1::epoc::parse_window_command_buffer(buffer.data(), buffer.size(), session_handle, cmds));
    CHECK(cmds.empty());
}
