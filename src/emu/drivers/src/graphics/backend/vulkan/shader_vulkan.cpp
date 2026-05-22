/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <common/configure.h>
#include <common/platform.h>

#define EKA2L1_USE_VULKAN_BACKEND (BUILD_WITH_VULKAN && (EKA2L1_PLATFORM(WIN32) || EKA2L1_PLATFORM(ANDROID) || EKA2L1_PLATFORM(MACOS) || EKA2L1_PLATFORM(IOS) || EKA2L1_PLATFORM(UNIX)))

#if EKA2L1_USE_VULKAN_BACKEND

#include <common/log.h>
#include <drivers/graphics/backend/vulkan/graphics_vulkan.h>
#include <drivers/graphics/backend/vulkan/shader_vulkan.h>
#include <drivers/graphics/backend/vulkan/texture_vulkan.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>

#if EKA2L1_VULKAN_USE_SHADERC
#include <shaderc/shaderc.h>
#endif

namespace eka2l1::drivers {
    namespace {
        struct shader_variable {
            std::string name;
            shader_var_type type = shader_var_type::none;
            std::int32_t binding = -1;
            std::int32_t array_size = 1;
        };

        static bool is_identifier_start(const char ch) {
            return std::isalpha(static_cast<unsigned char>(ch)) || (ch == '_');
        }

        static bool is_identifier_char(const char ch) {
            return std::isalnum(static_cast<unsigned char>(ch)) || (ch == '_');
        }

        static std::string remove_comments(const char *data, const std::size_t size) {
            std::string result;
            result.reserve(size);

            for (std::size_t i = 0; i < size; i++) {
                if ((i + 1 < size) && (data[i] == '/') && (data[i + 1] == '/')) {
                    while ((i < size) && (data[i] != '\n')) {
                        i++;
                    }
                    if (i < size) {
                        result.push_back('\n');
                    }
                    continue;
                }

                if ((i + 1 < size) && (data[i] == '/') && (data[i + 1] == '*')) {
                    i += 2;
                    while ((i + 1 < size) && !((data[i] == '*') && (data[i + 1] == '/'))) {
                        if (data[i] == '\n') {
                            result.push_back('\n');
                        }
                        i++;
                    }
                    if (i + 1 < size) {
                        i++;
                    }
                    continue;
                }

                result.push_back(data[i]);
            }

            return result;
        }

        static bool next_token(const std::string &source, std::size_t &position, std::string &token) {
            while ((position < source.size()) && std::isspace(static_cast<unsigned char>(source[position]))) {
                position++;
            }

            if (position >= source.size()) {
                token.clear();
                return false;
            }

            const char ch = source[position];
            if (is_identifier_start(ch)) {
                const std::size_t start = position++;
                while ((position < source.size()) && is_identifier_char(source[position])) {
                    position++;
                }
                token = source.substr(start, position - start);
                return true;
            }

            if (std::isdigit(static_cast<unsigned char>(ch))) {
                const std::size_t start = position++;
                while ((position < source.size()) && std::isdigit(static_cast<unsigned char>(source[position]))) {
                    position++;
                }
                token = source.substr(start, position - start);
                return true;
            }

            token.assign(1, ch);
            position++;
            return true;
        }

        static bool token_is_number(const std::string &token) {
            return !token.empty() && std::all_of(token.begin(), token.end(), [](const char ch) {
                return std::isdigit(static_cast<unsigned char>(ch));
            });
        }

        static shader_var_type shader_type_from_name(const std::string &type) {
            if ((type == "int") || (type == "uint")) {
                return shader_var_type::integer;
            }
            if (type == "float") {
                return shader_var_type::real;
            }
            if (type == "bool") {
                return shader_var_type::boolean;
            }
            if (type == "vec2") {
                return shader_var_type::vec2;
            }
            if (type == "vec3") {
                return shader_var_type::vec3;
            }
            if (type == "vec4") {
                return shader_var_type::vec4;
            }
            if ((type == "ivec2") || (type == "uvec2")) {
                return shader_var_type::ivec2;
            }
            if ((type == "ivec3") || (type == "uvec3")) {
                return shader_var_type::ivec3;
            }
            if ((type == "ivec4") || (type == "uvec4")) {
                return shader_var_type::ivec4;
            }
            if (type == "bvec2") {
                return shader_var_type::bvec2;
            }
            if (type == "bvec3") {
                return shader_var_type::bvec3;
            }
            if (type == "bvec4") {
                return shader_var_type::bvec4;
            }
            if (type == "sampler1D") {
                return shader_var_type::sampler1d;
            }
            if (type == "sampler2D") {
                return shader_var_type::sampler2d;
            }
            if (type == "samplerCube") {
                return shader_var_type::sampler_cube;
            }
            if (type == "mat2") {
                return shader_var_type::mat2;
            }
            if (type == "mat3") {
                return shader_var_type::mat3;
            }
            if (type == "mat4") {
                return shader_var_type::mat4;
            }

            return shader_var_type::none;
        }

        static void skip_to_statement_end(const std::string &source, std::size_t &position);
        static bool parse_array_size(const std::string &source, std::size_t &position, std::int32_t &array_size);

        static std::unordered_map<std::string, std::vector<shader_variable>> collect_shader_structs(const std::string &source) {
            std::unordered_map<std::string, std::vector<shader_variable>> result;

            std::size_t position = 0;
            std::string token;
            while (next_token(source, position, token)) {
                if (token != "struct") {
                    continue;
                }

                std::string struct_name;
                if (!next_token(source, position, struct_name)) {
                    break;
                }

                if (!next_token(source, position, token) || (token != "{")) {
                    continue;
                }

                std::vector<shader_variable> fields;
                while (next_token(source, position, token)) {
                    if (token == "}") {
                        skip_to_statement_end(source, position);
                        break;
                    }

                    const std::string &type_token = token;
                    std::string field_name;
                    if (!next_token(source, position, field_name)) {
                        break;
                    }

                    shader_variable field;
                    field.name = field_name;
                    field.type = shader_type_from_name(type_token);
                    parse_array_size(source, position, field.array_size);
                    if (field.type != shader_var_type::none) {
                        fields.push_back(std::move(field));
                    }
                    skip_to_statement_end(source, position);
                }

                if (!struct_name.empty() && !fields.empty()) {
                    result[struct_name] = std::move(fields);
                }
            }

            return result;
        }

        static void skip_to_statement_end(const std::string &source, std::size_t &position) {
            std::string token;
            int brace_depth = 0;
            while (next_token(source, position, token)) {
                if (token == "{") {
                    brace_depth++;
                } else if (token == "}") {
                    brace_depth = std::max(0, brace_depth - 1);
                } else if ((token == ";") && (brace_depth == 0)) {
                    return;
                }
            }
        }

        static int parse_layout_location(const std::string &source, std::size_t &position) {
            std::string token;
            if (!next_token(source, position, token) || (token != "(")) {
                return -1;
            }

            int location = -1;
            int depth = 1;
            bool saw_location = false;
            bool saw_equal = false;
            while ((depth > 0) && next_token(source, position, token)) {
                if (token == "(") {
                    depth++;
                    continue;
                }

                if (token == ")") {
                    depth--;
                    continue;
                }

                if (token == "location") {
                    saw_location = true;
                    saw_equal = false;
                    continue;
                }

                if (saw_location && (token == "=")) {
                    saw_equal = true;
                    continue;
                }

                if (saw_location && saw_equal && token_is_number(token)) {
                    location = std::stoi(token);
                    saw_location = false;
                    saw_equal = false;
                }
            }

            return location;
        }

        static bool parse_array_size(const std::string &source, std::size_t &position, std::int32_t &array_size) {
            const std::size_t saved = position;
            std::string token;
            if (!next_token(source, position, token) || (token != "[")) {
                position = saved;
                array_size = 1;
                return false;
            }

            array_size = 1;
            if (next_token(source, position, token) && token_is_number(token)) {
                array_size = std::max(1, std::stoi(token));
            }

            while (next_token(source, position, token) && (token != "]")) {
            }

            return true;
        }

        static void add_unique_variable(std::vector<shader_variable> &variables, shader_variable variable) {
            if (variable.name.empty() || (variable.type == shader_var_type::none)) {
                return;
            }

            const auto existing = std::find_if(variables.begin(), variables.end(), [&](const shader_variable &item) {
                return item.name == variable.name;
            });
            if (existing != variables.end()) {
                return;
            }

            if (variable.binding < 0) {
                variable.binding = static_cast<std::int32_t>(variables.size());
            }

            variables.push_back(std::move(variable));
        }

        static void collect_shader_variables(const std::string &source, const bool vertex_shader,
            std::vector<shader_variable> &attributes, std::vector<shader_variable> &uniforms) {
            const auto shader_structs = collect_shader_structs(source);
            std::size_t position = 0;
            int pending_layout_location = -1;
            std::string token;

            while (next_token(source, position, token)) {
                if (token == "struct") {
                    skip_to_statement_end(source, position);
                    pending_layout_location = -1;
                    continue;
                }

                if (token == "layout") {
                    pending_layout_location = parse_layout_location(source, position);
                    continue;
                }

                if ((token == "uniform")) {
                    std::string type_token;
                    std::string name_token;
                    if (!next_token(source, position, type_token)) {
                        break;
                    }

                    if (!next_token(source, position, name_token)) {
                        break;
                    }

                    if (name_token == "{") {
                        skip_to_statement_end(source, position);
                        pending_layout_location = -1;
                        continue;
                    }

                    shader_variable variable;
                    variable.name = name_token;
                    variable.type = shader_type_from_name(type_token);
                    parse_array_size(source, position, variable.array_size);

                    const auto struct_match = shader_structs.find(type_token);
                    if ((variable.type == shader_var_type::none) && (struct_match != shader_structs.end())) {
                        for (const shader_variable &field : struct_match->second) {
                            shader_variable struct_member;
                            struct_member.name = name_token + "." + field.name;
                            struct_member.type = field.type;
                            struct_member.array_size = field.array_size;
                            add_unique_variable(uniforms, std::move(struct_member));
                        }
                    } else {
                        add_unique_variable(uniforms, std::move(variable));
                    }

                    skip_to_statement_end(source, position);
                    pending_layout_location = -1;
                    continue;
                }

                if (vertex_shader && ((token == "in") || (token == "attribute"))) {
                    std::string type_token;
                    std::string name_token;
                    if (!next_token(source, position, type_token) || !next_token(source, position, name_token)) {
                        break;
                    }

                    shader_variable variable;
                    variable.name = name_token;
                    variable.type = shader_type_from_name(type_token);
                    variable.binding = pending_layout_location;
                    parse_array_size(source, position, variable.array_size);
                    add_unique_variable(attributes, std::move(variable));
                    skip_to_statement_end(source, position);
                    pending_layout_location = -1;
                    continue;
                }

                if ((token != "in") && (token != "attribute") && (token != "uniform")) {
                    pending_layout_location = -1;
                }
            }
        }

        static void append_u16(std::vector<std::uint8_t> &data, const std::uint16_t value) {
            const std::uint8_t *bytes = reinterpret_cast<const std::uint8_t *>(&value);
            data.insert(data.end(), bytes, bytes + sizeof(value));
        }

        static void append_i32(std::vector<std::uint8_t> &data, const std::int32_t value) {
            const std::uint8_t *bytes = reinterpret_cast<const std::uint8_t *>(&value);
            data.insert(data.end(), bytes, bytes + sizeof(value));
        }

        static void append_shader_type(std::vector<std::uint8_t> &data, const shader_var_type value) {
            const std::uint8_t *bytes = reinterpret_cast<const std::uint8_t *>(&value);
            data.insert(data.end(), bytes, bytes + sizeof(value));
        }

        static void append_variable_record(std::vector<std::uint8_t> &data, const shader_variable &variable) {
            const std::size_t name_length = std::min<std::size_t>(variable.name.size(), 255);
            data.push_back(static_cast<std::uint8_t>(name_length));
            data.insert(data.end(), variable.name.begin(), variable.name.begin() + name_length);
            append_i32(data, variable.binding);
            append_shader_type(data, variable.type);
            append_i32(data, variable.array_size);
        }

        static void build_metadata(const vulkan_shader_module *vertex_module, const vulkan_shader_module *fragment_module,
            std::vector<std::uint8_t> &metadata) {
            std::vector<shader_variable> attributes;
            std::vector<shader_variable> uniforms;

            if (vertex_module && !vertex_module->source().empty()) {
                const std::string source = remove_comments(vertex_module->source().data(), vertex_module->source().size());
                collect_shader_variables(source, true, attributes, uniforms);
            }

            if (fragment_module && !fragment_module->source().empty()) {
                const std::string source = remove_comments(fragment_module->source().data(), fragment_module->source().size());
                collect_shader_variables(source, false, attributes, uniforms);
            }

            metadata.clear();
            metadata.resize(16);
            reinterpret_cast<std::uint16_t *>(metadata.data())[0] = 16;

            std::vector<std::uint16_t> offsets;
            offsets.reserve(attributes.size());
            for (const shader_variable &attribute : attributes) {
                offsets.push_back(static_cast<std::uint16_t>(metadata.size()));
                append_variable_record(metadata, attribute);
            }
            for (const std::uint16_t offset : offsets) {
                append_u16(metadata, offset);
            }

            std::uint16_t *header = reinterpret_cast<std::uint16_t *>(metadata.data());
            header[1] = static_cast<std::uint16_t>(metadata.size());
            header[2] = static_cast<std::uint16_t>(attributes.size());
            header[3] = static_cast<std::uint16_t>(uniforms.size());

            std::size_t max_attribute_name_length = 0;
            for (const shader_variable &attribute : attributes) {
                max_attribute_name_length = std::max(max_attribute_name_length, attribute.name.size() + 1);
            }

            std::size_t max_uniform_name_length = 0;
            for (const shader_variable &uniform : uniforms) {
                max_uniform_name_length = std::max(max_uniform_name_length, uniform.name.size() + 1);
            }

            header[4] = static_cast<std::uint16_t>(std::min<std::size_t>(max_attribute_name_length, UINT16_MAX));
            header[5] = static_cast<std::uint16_t>(std::min<std::size_t>(max_uniform_name_length, UINT16_MAX));

            offsets.clear();
            offsets.reserve(uniforms.size());
            for (const shader_variable &uniform : uniforms) {
                offsets.push_back(static_cast<std::uint16_t>(metadata.size()));
                append_variable_record(metadata, uniform);
            }
            for (const std::uint16_t offset : offsets) {
                append_u16(metadata, offset);
            }

            reinterpret_cast<std::uint16_t *>(metadata.data())[6] = static_cast<std::uint16_t>(metadata.size());
            reinterpret_cast<std::uint16_t *>(metadata.data())[7] = 0;
        }

        static bool is_spirv_data(const char *data, const std::size_t size) {
            if (!data || (size < sizeof(std::uint32_t)) || ((size % sizeof(std::uint32_t)) != 0)) {
                return false;
            }

            std::uint32_t magic = 0;
            std::memcpy(&magic, data, sizeof(magic));
            return magic == 0x07230203;
        }

        static bool is_sampler_type(const shader_var_type type) {
            return (type == shader_var_type::sampler1d) || (type == shader_var_type::sampler2d) || (type == shader_var_type::sampler_cube);
        }

        static bool vulkan_texture_trace_enabled() {
            const char *value = std::getenv("EKA2L1_VULKAN_TEXTURE_TRACE");
            return value && value[0] && (std::strcmp(value, "0") != 0);
        }

        static std::string shader_var_type_to_glsl(const shader_var_type type) {
            switch (type) {
            case shader_var_type::integer:
                return "int";
            case shader_var_type::real:
                return "float";
            case shader_var_type::boolean:
                return "bool";
            case shader_var_type::vec2:
                return "vec2";
            case shader_var_type::vec3:
                return "vec3";
            case shader_var_type::vec4:
                return "vec4";
            case shader_var_type::ivec2:
                return "ivec2";
            case shader_var_type::ivec3:
                return "ivec3";
            case shader_var_type::ivec4:
                return "ivec4";
            case shader_var_type::bvec2:
                return "bvec2";
            case shader_var_type::bvec3:
                return "bvec3";
            case shader_var_type::bvec4:
                return "bvec4";
            case shader_var_type::sampler1d:
                return "sampler1D";
            case shader_var_type::sampler2d:
                return "sampler2D";
            case shader_var_type::sampler_cube:
                return "samplerCube";
            case shader_var_type::mat2:
                return "mat2";
            case shader_var_type::mat3:
                return "mat3";
            case shader_var_type::mat4:
                return "mat4";
            default:
                return {};
            }
        }

        static std::size_t align_up(const std::size_t value, const std::size_t alignment) {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        static std::size_t std140_base_alignment(const shader_var_type type) {
            switch (type) {
            case shader_var_type::vec2:
            case shader_var_type::ivec2:
            case shader_var_type::bvec2:
                return 8;

            case shader_var_type::vec3:
            case shader_var_type::vec4:
            case shader_var_type::ivec3:
            case shader_var_type::ivec4:
            case shader_var_type::bvec3:
            case shader_var_type::bvec4:
            case shader_var_type::mat2:
            case shader_var_type::mat3:
            case shader_var_type::mat4:
                return 16;

            default:
                return 4;
            }
        }

        static std::size_t std140_value_size(const shader_var_type type) {
            switch (type) {
            case shader_var_type::vec2:
            case shader_var_type::ivec2:
            case shader_var_type::bvec2:
                return 8;

            case shader_var_type::vec3:
            case shader_var_type::vec4:
            case shader_var_type::ivec3:
            case shader_var_type::ivec4:
            case shader_var_type::bvec3:
            case shader_var_type::bvec4:
                return 16;

            case shader_var_type::mat2:
                return 32;

            case shader_var_type::mat3:
                return 48;

            case shader_var_type::mat4:
                return 64;

            default:
                return 4;
            }
        }

        static std::size_t std140_storage_size(const shader_var_type type, const std::int32_t array_size) {
            const std::size_t element_size = std140_value_size(type);
            if (array_size <= 1) {
                return element_size;
            }

            return align_up(element_size, 16) * static_cast<std::size_t>(array_size);
        }

        static std::size_t packed_value_size(const shader_var_type type) {
            switch (type) {
            case shader_var_type::vec2:
            case shader_var_type::ivec2:
            case shader_var_type::bvec2:
                return 8;

            case shader_var_type::vec3:
            case shader_var_type::ivec3:
            case shader_var_type::bvec3:
                return 12;

            case shader_var_type::vec4:
            case shader_var_type::ivec4:
            case shader_var_type::bvec4:
                return 16;

            case shader_var_type::mat2:
                return 16;

            case shader_var_type::mat3:
                return 36;

            case shader_var_type::mat4:
                return 64;

            default:
                return 4;
            }
        }

        static void copy_single_value_to_std140(std::uint8_t *destination, const std::size_t destination_size,
            const shader_var_type type, const std::uint8_t *source, const std::size_t source_size) {
            if (!destination || !source || (destination_size == 0) || (source_size == 0)) {
                return;
            }

            if ((type == shader_var_type::mat2) || (type == shader_var_type::mat3) || (type == shader_var_type::mat4)) {
                const std::size_t dimension = (type == shader_var_type::mat2) ? 2 : ((type == shader_var_type::mat3) ? 3 : 4);
                const std::size_t packed_column_size = dimension * sizeof(float);
                for (std::size_t column = 0; column < dimension; column++) {
                    const std::size_t source_offset = column * packed_column_size;
                    const std::size_t destination_offset = column * 16;
                    if ((source_offset >= source_size) || (destination_offset >= destination_size)) {
                        break;
                    }

                    const std::size_t bytes_to_copy = std::min({ packed_column_size,
                        source_size - source_offset,
                        destination_size - destination_offset });
                    std::memcpy(destination + destination_offset, source + source_offset, bytes_to_copy);
                }
                return;
            }

            const std::size_t bytes_to_copy = std::min({ packed_value_size(type), source_size, destination_size });
            std::memcpy(destination, source, bytes_to_copy);
        }

        static void copy_uniform_value_to_std140(std::uint8_t *destination, const std::size_t destination_size,
            const shader_var_type type, const std::int32_t array_size, const std::uint8_t *source, const std::size_t source_size) {
            if (!destination || !source || (destination_size == 0) || (source_size == 0)) {
                return;
            }

            const std::size_t source_stride = packed_value_size(type);
            const std::size_t destination_stride = (array_size > 1) ? align_up(std140_value_size(type), 16) : std140_value_size(type);
            const std::size_t count = std::max<std::int32_t>(1, array_size);
            for (std::size_t index = 0; index < count; index++) {
                const std::size_t source_offset = index * source_stride;
                const std::size_t destination_offset = index * destination_stride;
                if ((source_offset >= source_size) || (destination_offset >= destination_size)) {
                    break;
                }

                copy_single_value_to_std140(
                    destination + destination_offset,
                    destination_size - destination_offset,
                    type,
                    source + source_offset,
                    source_size - source_offset);
            }
        }

        static std::string trim_copy(const std::string &value) {
            const std::size_t first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                return {};
            }

            const std::size_t last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        static std::string replace_word(std::string source, const std::string &from, const std::string &to) {
            return std::regex_replace(source, std::regex("\\b" + from + "\\b"), to);
        }

        static bool parse_interface_name(const std::string &line, std::string &qualifier, std::string &name) {
            static const std::regex pattern(R"(^\s*(?:layout\s*\([^\)]*\)\s*)?(in|out)\s+[A-Za-z_][A-Za-z0-9_]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]+\])?\s*;)");
            std::smatch match;
            if (!std::regex_search(line, match, pattern)) {
                return false;
            }

            qualifier = match[1].str();
            name = match[2].str();
            return true;
        }

        static std::string add_layout_location(const std::string &line, const int location) {
            if ((location < 0) || (line.find("layout") != std::string::npos)) {
                return line;
            }

            return "layout(location = " + std::to_string(location) + ") " + trim_copy(line);
        }

        static void collect_interface_locations(const std::string &source, const bool vertex_shader,
            std::unordered_map<std::string, int> &locations) {
            const std::string clean_source = remove_comments(source.data(), source.size());
            std::istringstream stream(clean_source);
            std::string line;

            while (std::getline(stream, line)) {
                line = replace_word(line, "attribute", "in");
                line = replace_word(line, "varying", vertex_shader ? "out" : "in");

                std::string qualifier;
                std::string name;
                if (!parse_interface_name(line, qualifier, name)) {
                    continue;
                }

                if ((vertex_shader && (qualifier == "out")) || (!vertex_shader && (qualifier == "in"))) {
                    if (locations.find(name) == locations.end()) {
                        locations[name] = static_cast<int>(locations.size());
                    }
                }
            }
        }

        static std::vector<std::string> extract_struct_declarations(const std::string &source) {
            std::vector<std::string> declarations;
            std::istringstream stream(source);
            std::string line;
            std::string current;
            bool in_struct = false;
            int brace_depth = 0;

            while (std::getline(stream, line)) {
                const std::string trimmed = trim_copy(line);
                if (!in_struct && (trimmed.rfind("struct ", 0) != 0)) {
                    continue;
                }

                in_struct = true;
                current += line;
                current += '\n';

                for (const char ch : line) {
                    if (ch == '{') {
                        brace_depth++;
                    } else if (ch == '}') {
                        brace_depth--;
                    }
                }

                if ((brace_depth <= 0) && (line.find(';') != std::string::npos)) {
                    declarations.push_back(current);
                    current.clear();
                    in_struct = false;
                    brace_depth = 0;
                }
            }

            return declarations;
        }

        static std::string glsl_identifier_for_uniform(const std::string &name) {
            std::string result = name;
            std::replace(result.begin(), result.end(), '.', '_');
            return result;
        }

        static void collect_struct_uniform_replacements(const std::string &source,
            std::unordered_map<std::string, std::string> &replacements) {
            const std::string clean_source = remove_comments(source.data(), source.size());
            const auto shader_structs = collect_shader_structs(clean_source);
            if (shader_structs.empty()) {
                return;
            }

            static const std::regex uniform_pattern(R"(^\s*uniform\s+([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]+\])?\s*;)");
            std::istringstream stream(clean_source);
            std::string line;
            while (std::getline(stream, line)) {
                std::smatch uniform_match;
                if (!std::regex_search(line, uniform_match, uniform_pattern)) {
                    continue;
                }

                const std::string type_name = uniform_match[1].str();
                const std::string uniform_name = uniform_match[2].str();
                const auto struct_match = shader_structs.find(type_name);
                if (struct_match == shader_structs.end()) {
                    continue;
                }

                std::ostringstream replacement;
                replacement << type_name << "(";
                for (std::size_t i = 0; i < struct_match->second.size(); i++) {
                    if (i != 0) {
                        replacement << ", ";
                    }
                    replacement << glsl_identifier_for_uniform(uniform_name + "." + struct_match->second[i].name);
                }
                replacement << ")";
                replacements[uniform_name] = replacement.str();
            }
        }

        static std::string transform_glsl_for_vulkan(const std::string &source, const bool vertex_shader,
            const std::vector<std::string> &uniform_block_members,
            const std::unordered_map<std::string, std::uint32_t> &sampler_descriptor_bindings,
            const std::unordered_map<std::string, int> &attribute_locations,
            const std::unordered_map<std::string, int> &varying_locations,
            const std::unordered_map<std::string, std::string> &struct_uniform_replacements) {
            std::ostringstream out;
            const std::string clean_source = remove_comments(source.data(), source.size());
            const bool needs_frag_color_output = !vertex_shader && (clean_source.find("gl_FragColor") != std::string::npos);

            out << "#version 450\n";

            for (const std::string &struct_declaration : extract_struct_declarations(source)) {
                out << struct_declaration;
            }

            if (!uniform_block_members.empty()) {
                out << "layout(set = 0, binding = 0, std140) uniform EKA2L1Uniforms {\n";
                for (const std::string &member : uniform_block_members) {
                    out << "    " << member << "\n";
                }
                out << "};\n";
            }

            if (needs_frag_color_output) {
                out << "layout(location = 0) out vec4 eka2l1_frag_color;\n";
            }

            std::istringstream stream(source);
            std::string line;
            static const std::regex uniform_pattern(R"(^\s*uniform\s+([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(\[[^\]]+\])?\s*;)");
            bool skipping_struct_declaration = false;
            int struct_brace_depth = 0;

            while (std::getline(stream, line)) {
                std::string trimmed = trim_copy(line);
                if (trimmed.empty()) {
                    out << "\n";
                    continue;
                }

                if (!skipping_struct_declaration && (trimmed.rfind("struct ", 0) == 0)) {
                    skipping_struct_declaration = true;
                    struct_brace_depth = 0;
                }

                if (skipping_struct_declaration) {
                    for (const char ch : line) {
                        if (ch == '{') {
                            struct_brace_depth++;
                        } else if (ch == '}') {
                            struct_brace_depth--;
                        }
                    }

                    if ((struct_brace_depth <= 0) && (line.find(';') != std::string::npos)) {
                        skipping_struct_declaration = false;
                    }
                    continue;
                }

                if ((trimmed.rfind("#version", 0) == 0) || (trimmed.rfind("#extension", 0) == 0) || (trimmed.rfind("precision ", 0) == 0)) {
                    continue;
                }

                line = replace_word(line, "highp", "");
                line = replace_word(line, "mediump", "");
                line = replace_word(line, "lowp", "");
                line = replace_word(line, "attribute", "in");
                line = replace_word(line, "varying", vertex_shader ? "out" : "in");
                line = std::regex_replace(line, std::regex("\\btexture2D\\s*\\("), "texture(");
                line = std::regex_replace(line, std::regex("\\btextureCube\\s*\\("), "texture(");

                if (needs_frag_color_output) {
                    line = replace_word(line, "gl_FragColor", "eka2l1_frag_color");
                }

                std::smatch uniform_match;
                if (std::regex_search(line, uniform_match, uniform_pattern)) {
                    const std::string type_name = uniform_match[1].str();
                    const std::string uniform_name = uniform_match[2].str();
                    const std::string array_suffix = uniform_match[3].matched ? uniform_match[3].str() : "";
                    const shader_var_type type = shader_type_from_name(type_name);

                    if (is_sampler_type(type)) {
                        const auto descriptor_binding = sampler_descriptor_bindings.find(uniform_name);
                        if (descriptor_binding != sampler_descriptor_bindings.end()) {
                            out << "layout(set = 0, binding = " << descriptor_binding->second << ") uniform "
                                << type_name << " " << uniform_name << array_suffix << ";\n";
                        }
                    }

                    continue;
                }

                for (const auto &replacement : struct_uniform_replacements) {
                    line = replace_word(line, replacement.first, replacement.second);
                }

                std::string qualifier;
                std::string name;
                if (parse_interface_name(line, qualifier, name)) {
                    int location = -1;
                    if (vertex_shader && (qualifier == "in")) {
                        const auto found = attribute_locations.find(name);
                        if (found != attribute_locations.end()) {
                            location = found->second;
                        }
                    } else if ((vertex_shader && (qualifier == "out")) || (!vertex_shader && (qualifier == "in"))) {
                        const auto found = varying_locations.find(name);
                        if (found != varying_locations.end()) {
                            location = found->second;
                        }
                    } else if (!vertex_shader && (qualifier == "out")) {
                        location = 0;
                    }

                    line = add_layout_location(line, location);
                }

                out << line << "\n";
                if (vertex_shader && (line.find("gl_Position") != std::string::npos)
                    && (line.find('=') != std::string::npos) && (line.find(';') != std::string::npos)) {
                    out << "gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;\n";
                }
            }

            return out.str();
        }

#if EKA2L1_VULKAN_USE_SHADERC
        static bool compile_vulkan_glsl(vulkan_graphics_driver *driver, const std::string &source,
            const shader_module_type type, vk::UniqueShaderModule &module, std::string *compile_log) {
            shaderc_compiler_t compiler = shaderc_compiler_initialize();
            shaderc_compile_options_t options = shaderc_compile_options_initialize();
            if (!compiler || !options) {
                if (compile_log) {
                    *compile_log = "Unable to initialize shaderc";
                }
                if (options) {
                    shaderc_compile_options_release(options);
                }
                if (compiler) {
                    shaderc_compiler_release(compiler);
                }
                return false;
            }

            shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
            shaderc_compile_options_set_source_language(options, shaderc_source_language_glsl);
            shaderc_compile_options_set_auto_map_locations(options, true);

            const shaderc_shader_kind kind = (type == shader_module_type::vertex)
                ? shaderc_vertex_shader
                : ((type == shader_module_type::fragment) ? shaderc_fragment_shader : shaderc_geometry_shader);
            shaderc_compilation_result_t result = shaderc_compile_into_spv(
                compiler, source.data(), source.size(), kind, "eka2l1-vulkan-shader", "main", options);

            const shaderc_compilation_status status = shaderc_result_get_compilation_status(result);
            if (status != shaderc_compilation_status_success) {
                if (compile_log) {
                    *compile_log = shaderc_result_get_error_message(result);
                }
                shaderc_result_release(result);
                shaderc_compile_options_release(options);
                shaderc_compiler_release(compiler);
                return false;
            }

            const std::size_t spirv_size = shaderc_result_get_length(result);
            const char *spirv_data = shaderc_result_get_bytes(result);
            try {
                vk::ShaderModuleCreateInfo create_info(
                    vk::ShaderModuleCreateFlags{},
                    spirv_size,
                    reinterpret_cast<const std::uint32_t *>(spirv_data));
                module = driver->device().createShaderModuleUnique(create_info);
            } catch (std::exception &e) {
                if (compile_log) {
                    *compile_log = e.what();
                }
                shaderc_result_release(result);
                shaderc_compile_options_release(options);
                shaderc_compiler_release(compiler);
                return false;
            }

            shaderc_result_release(result);
            shaderc_compile_options_release(options);
            shaderc_compiler_release(compiler);
            if (compile_log) {
                compile_log->clear();
            }
            return true;
        }
#endif
    }

    vulkan_shader_module::vulkan_shader_module()
        : type_(shader_module_type::vertex) {
    }

    vulkan_shader_module::~vulkan_shader_module() = default;

    bool vulkan_shader_module::create(graphics_driver *driver, const char *data, const std::size_t size,
        const shader_module_type type, std::string *compile_log) {
        type_ = type;
        source_.clear();
        module_.reset();

        if (!data || (size == 0)) {
            if (compile_log) {
                *compile_log = "Vulkan shader source is empty";
            }
            return false;
        }

        source_.assign(data, data + size);

        if (!is_spirv_data(data, size)) {
            if (compile_log) {
                compile_log->clear();
            }
            return true;
        }

        vulkan_graphics_driver *vulkan_driver = reinterpret_cast<vulkan_graphics_driver *>(driver);
        if (!vulkan_driver || !vulkan_driver->device()) {
            if (compile_log) {
                *compile_log = "Vulkan device is not available for SPIR-V shader module creation";
            }
            return false;
        }

        try {
            vk::ShaderModuleCreateInfo create_info(
                vk::ShaderModuleCreateFlags{},
                size,
                reinterpret_cast<const std::uint32_t *>(data));
            module_ = vulkan_driver->device().createShaderModuleUnique(create_info);
        } catch (std::exception &e) {
            if (compile_log) {
                *compile_log = e.what();
            } else {
                LOG_ERROR(DRIVER_GRAPHICS, "Failed to create Vulkan shader module: {}", e.what());
            }
            return false;
        }

        if (compile_log) {
            compile_log->clear();
        }
        return true;
    }

    vulkan_shader_program::vulkan_shader_program()
        : vertex_module_(nullptr)
        , fragment_module_(nullptr)
        , uniform_buffer_size_(0)
        , descriptor_set_(nullptr) {
    }

    vulkan_shader_program::~vulkan_shader_program() = default;

    bool vulkan_shader_program::compile_glsl_modules(vulkan_graphics_driver *driver, std::string *link_log) {
        compiled_vertex_module_.reset();
        compiled_fragment_module_.reset();

        if (!driver || !driver->device() || !vertex_module_ || !fragment_module_) {
            if (link_log) {
                *link_log = "Vulkan device or shader modules are not available";
            }
            return false;
        }

        if (vertex_module_->module() && fragment_module_->module()) {
            if (link_log) {
                link_log->clear();
            }
            return true;
        }

#if !EKA2L1_VULKAN_USE_SHADERC
        if (link_log) {
            *link_log = "Vulkan GLSL shader compilation requires shaderc";
        }
        return false;
#else
        if (vertex_module_->source().empty() || fragment_module_->source().empty()) {
            if (link_log) {
                *link_log = "Vulkan shader source is empty";
            }
            return false;
        }

        shader_program_metadata metadata(metadata_.data());
        std::unordered_map<std::string, int> attribute_locations;
        for (std::uint16_t i = 0; i < metadata.get_attribute_count(); i++) {
            std::string name;
            std::int32_t binding = -1;
            shader_var_type type = shader_var_type::none;
            std::int32_t array_size = 1;
            if (metadata.get_attribute_info(i, name, binding, type, array_size) && (binding >= 0)) {
                attribute_locations[name] = binding;
            }
        }

        std::vector<std::string> uniform_block_members;
        std::unordered_map<std::string, std::uint32_t> sampler_name_descriptor_bindings;
        std::set<std::string> emitted_uniform_members;
        for (std::uint16_t i = 0; i < metadata.get_uniform_count(); i++) {
            std::string name;
            std::int32_t binding = -1;
            shader_var_type type = shader_var_type::none;
            std::int32_t array_size = 1;
            if (!metadata.get_uniform_info(i, name, binding, type, array_size) || (binding < 0)) {
                continue;
            }

            if (is_sampler_type(type)) {
                const std::uint32_t descriptor_binding = 1U + static_cast<std::uint32_t>(sampler_descriptor_bindings_.size());
                sampler_descriptor_bindings_[binding] = descriptor_binding;
                sampler_name_descriptor_bindings[name] = descriptor_binding;
                continue;
            }

            const std::string glsl_type = shader_var_type_to_glsl(type);
            if (glsl_type.empty()) {
                continue;
            }

            const std::size_t offset = align_up(uniform_buffer_size_, std140_base_alignment(type));
            uniform_offsets_[binding] = offset;
            uniform_buffer_size_ = offset + std140_storage_size(type, array_size);

            const std::string block_member_name = glsl_identifier_for_uniform(name);
            if (emitted_uniform_members.insert(block_member_name).second) {
                std::ostringstream declaration;
                declaration << glsl_type << " " << block_member_name;
                if (array_size > 1) {
                    declaration << "[" << array_size << "]";
                }
                declaration << ";";
                uniform_block_members.push_back(declaration.str());
            }
        }

        if (uniform_buffer_size_ > 0) {
            uniform_buffer_size_ = align_up(uniform_buffer_size_, 16);
        }

        const std::string vertex_source(vertex_module_->source().begin(), vertex_module_->source().end());
        const std::string fragment_source(fragment_module_->source().begin(), fragment_module_->source().end());

        std::unordered_map<std::string, int> varying_locations;
        collect_interface_locations(vertex_source, true, varying_locations);
        collect_interface_locations(fragment_source, false, varying_locations);

        std::unordered_map<std::string, std::string> struct_uniform_replacements;
        collect_struct_uniform_replacements(vertex_source, struct_uniform_replacements);
        collect_struct_uniform_replacements(fragment_source, struct_uniform_replacements);

        const std::string transformed_vertex = transform_glsl_for_vulkan(
            vertex_source,
            true,
            uniform_block_members,
            sampler_name_descriptor_bindings,
            attribute_locations,
            varying_locations,
            struct_uniform_replacements);
        const std::string transformed_fragment = transform_glsl_for_vulkan(
            fragment_source,
            false,
            uniform_block_members,
            sampler_name_descriptor_bindings,
            attribute_locations,
            varying_locations,
            struct_uniform_replacements);

        std::string compile_log;
        if (!compile_vulkan_glsl(driver, transformed_vertex, shader_module_type::vertex, compiled_vertex_module_, &compile_log)) {
            if (link_log) {
                *link_log = "Vertex shader: " + compile_log;
            }
            LOG_WARN(DRIVER_GRAPHICS, "Failed to compile Vulkan vertex GLSL: {}", compile_log);
            return false;
        }

        if (!compile_vulkan_glsl(driver, transformed_fragment, shader_module_type::fragment, compiled_fragment_module_, &compile_log)) {
            if (link_log) {
                *link_log = "Fragment shader: " + compile_log;
            }
            LOG_WARN(DRIVER_GRAPHICS, "Failed to compile Vulkan fragment GLSL: {}", compile_log);
            return false;
        }

        if (link_log) {
            link_log->clear();
        }
        return true;
#endif
    }

    bool vulkan_shader_program::create_descriptor_resources(vulkan_graphics_driver *driver) {
        descriptor_set_layout_.reset();
        descriptor_pool_.reset();
        descriptor_set_ = nullptr;
        uniform_buffer_.reset();
        uniform_memory_.reset();

        if (!driver || !driver->device()) {
            return false;
        }

        if ((uniform_buffer_size_ == 0) && sampler_descriptor_bindings_.empty()) {
            return true;
        }

        try {
            std::vector<vk::DescriptorSetLayoutBinding> bindings;
            if (uniform_buffer_size_ > 0) {
                bindings.emplace_back(
                    0,
                    vk::DescriptorType::eUniformBuffer,
                    1,
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
            }

            for (const auto &sampler_binding : sampler_descriptor_bindings_) {
                bindings.emplace_back(
                    sampler_binding.second,
                    vk::DescriptorType::eCombinedImageSampler,
                    1,
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
            }

            vk::DescriptorSetLayoutCreateInfo layout_create_info;
            layout_create_info.bindingCount = static_cast<std::uint32_t>(bindings.size());
            layout_create_info.pBindings = bindings.empty() ? nullptr : bindings.data();
            descriptor_set_layout_ = driver->device().createDescriptorSetLayoutUnique(layout_create_info);

            std::vector<vk::DescriptorPoolSize> pool_sizes;
            if (uniform_buffer_size_ > 0) {
                pool_sizes.emplace_back(vk::DescriptorType::eUniformBuffer, 1);
            }
            if (!sampler_descriptor_bindings_.empty()) {
                pool_sizes.emplace_back(
                    vk::DescriptorType::eCombinedImageSampler,
                    static_cast<std::uint32_t>(sampler_descriptor_bindings_.size()));
            }

            vk::DescriptorPoolCreateInfo pool_create_info;
            pool_create_info.maxSets = 1;
            pool_create_info.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
            pool_create_info.pPoolSizes = pool_sizes.empty() ? nullptr : pool_sizes.data();
            descriptor_pool_ = driver->device().createDescriptorPoolUnique(pool_create_info);

            const vk::DescriptorSetLayout set_layout = descriptor_set_layout_.get();
            vk::DescriptorSetAllocateInfo allocate_info(descriptor_pool_.get(), 1, &set_layout);
            descriptor_set_ = driver->device().allocateDescriptorSets(allocate_info).front();

            if (uniform_buffer_size_ > 0) {
                vk::BufferCreateInfo buffer_create_info(
                    vk::BufferCreateFlags{},
                    static_cast<vk::DeviceSize>(uniform_buffer_size_),
                    vk::BufferUsageFlagBits::eUniformBuffer,
                    vk::SharingMode::eExclusive);
                uniform_buffer_ = driver->device().createBufferUnique(buffer_create_info);

                const vk::MemoryRequirements memory_requirements = driver->device().getBufferMemoryRequirements(uniform_buffer_.get());
                vk::MemoryAllocateInfo allocate_memory_info(
                    memory_requirements.size,
                    driver->find_memory_type(
                        memory_requirements.memoryTypeBits,
                        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
                uniform_memory_ = driver->device().allocateMemoryUnique(allocate_memory_info);
                driver->device().bindBufferMemory(uniform_buffer_.get(), uniform_memory_.get(), 0);

                vk::DescriptorBufferInfo buffer_info(
                    uniform_buffer_.get(),
                    0,
                    static_cast<vk::DeviceSize>(uniform_buffer_size_));
                vk::WriteDescriptorSet descriptor_write;
                descriptor_write.dstSet = descriptor_set_;
                descriptor_write.dstBinding = 0;
                descriptor_write.descriptorCount = 1;
                descriptor_write.descriptorType = vk::DescriptorType::eUniformBuffer;
                descriptor_write.pBufferInfo = &buffer_info;
                driver->device().updateDescriptorSets(descriptor_write, nullptr);
            }
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Failed to create Vulkan shader descriptor resources: {}", e.what());
            descriptor_set_layout_.reset();
            descriptor_pool_.reset();
            descriptor_set_ = nullptr;
            uniform_buffer_.reset();
            uniform_memory_.reset();
            return false;
        }

        return true;
    }

    bool vulkan_shader_program::create(graphics_driver *driver, shader_module *vertex_module,
        shader_module *fragment_module, std::string *link_log) {
        vulkan_graphics_driver *vulkan_driver = reinterpret_cast<vulkan_graphics_driver *>(driver);
        vertex_module_ = reinterpret_cast<vulkan_shader_module *>(vertex_module);
        fragment_module_ = reinterpret_cast<vulkan_shader_module *>(fragment_module);
        uniform_values_.clear();
        texture_bindings_.clear();
        uniform_offsets_.clear();
        sampler_descriptor_bindings_.clear();
        uniform_buffer_size_ = 0;
        descriptor_set_ = nullptr;
        compiled_vertex_module_.reset();
        compiled_fragment_module_.reset();
        descriptor_set_layout_.reset();
        descriptor_pool_.reset();
        uniform_buffer_.reset();
        uniform_memory_.reset();

        if (!vertex_module_ || !fragment_module_) {
            if (link_log) {
                *link_log = "Vulkan shader program requires vertex and fragment modules";
            }
            return false;
        }

        build_metadata(vertex_module_, fragment_module_, metadata_);
        if (!compile_glsl_modules(vulkan_driver, link_log)) {
            return false;
        }

        if (!create_descriptor_resources(vulkan_driver)) {
            if (link_log) {
                *link_log = "Failed to create Vulkan shader descriptor resources";
            }
            return false;
        }

        if (link_log) {
            link_log->clear();
        }
        return true;
    }

    bool vulkan_shader_program::use(graphics_driver *driver) {
        vulkan_graphics_driver *vulkan_driver = reinterpret_cast<vulkan_graphics_driver *>(driver);
        if (!vulkan_driver) {
            return false;
        }

        vulkan_driver->set_active_shader_program(this);
        return true;
    }

    std::optional<int> vulkan_shader_program::get_uniform_location(const std::string &name) {
        shader_program_metadata metadata(metadata_.data());
        const std::int32_t binding = metadata.get_uniform_binding(name.c_str());
        if (binding < 0) {
            return std::optional<int>{};
        }
        return binding;
    }

    std::optional<int> vulkan_shader_program::get_attrib_location(const std::string &name) {
        shader_program_metadata metadata(metadata_.data());
        const std::int32_t binding = metadata.get_attribute_binding(name.c_str());
        if (binding < 0) {
            return std::optional<int>{};
        }
        return binding;
    }

    void *vulkan_shader_program::get_metadata() {
        return metadata_.empty() ? nullptr : metadata_.data();
    }

    void vulkan_shader_program::set_uniform_value(const int binding, const std::uint8_t *data, const std::size_t size) {
        if ((binding < 0) || !data || (size == 0)) {
            return;
        }

        uniform_values_[binding] = std::vector<std::uint8_t>(data, data + size);
        if ((sampler_descriptor_bindings_.find(binding) != sampler_descriptor_bindings_.end()) && (size >= sizeof(std::int32_t))) {
            std::int32_t texture_slot = -1;
            std::memcpy(&texture_slot, data, sizeof(texture_slot));
            if (texture_slot >= 0) {
                texture_bindings_[binding] = texture_slot;
            }
        }
    }

    void vulkan_shader_program::set_texture_binding(const int shader_binding, const int texture_slot) {
        if ((shader_binding < 0) || (texture_slot < 0)) {
            return;
        }

        texture_bindings_[shader_binding] = texture_slot;
    }

    void vulkan_shader_program::capture_descriptor_state(vulkan_shader_descriptor_state &state) const {
        state.uniform_values = uniform_values_;
        state.texture_bindings = texture_bindings_;
    }

    const std::vector<std::uint8_t> *vulkan_shader_program::uniform_value(const int binding) const {
        const auto found = uniform_values_.find(binding);
        if (found == uniform_values_.end()) {
            return nullptr;
        }

        return &found->second;
    }

    int vulkan_shader_program::texture_binding(const int shader_binding) const {
        const auto found = texture_bindings_.find(shader_binding);
        if (found == texture_bindings_.end()) {
            return -1;
        }

        return found->second;
    }

    bool vulkan_shader_program::prepare_descriptors(vulkan_graphics_driver *driver,
        const std::array<vulkan_texture *, VULKAN_SHADER_MAX_TEXTURE_SLOTS> &texture_slots,
        const vulkan_shader_descriptor_state *state) {
        if (!descriptor_set_) {
            return true;
        }

        if (!driver || !driver->device()) {
            return false;
        }

        const auto &uniform_values = state ? state->uniform_values : uniform_values_;
        const auto &texture_bindings = state ? state->texture_bindings : texture_bindings_;
        auto texture_binding_for = [&](const int shader_binding) {
            const auto found = texture_bindings.find(shader_binding);
            return (found == texture_bindings.end()) ? -1 : found->second;
        };

        if (uniform_buffer_size_ > 0) {
            if (!uniform_memory_) {
                return false;
            }

            std::vector<std::uint8_t> uniform_buffer_data(uniform_buffer_size_, 0);
            shader_program_metadata metadata(metadata_.data());
            for (std::uint16_t i = 0; i < metadata.get_uniform_count(); i++) {
                std::string name;
                std::int32_t binding = -1;
                shader_var_type type = shader_var_type::none;
                std::int32_t array_size = 1;
                if (!metadata.get_uniform_info(i, name, binding, type, array_size) || is_sampler_type(type)) {
                    continue;
                }

                const auto value = uniform_values.find(binding);
                const auto offset = uniform_offsets_.find(binding);
                if ((value == uniform_values.end()) || (offset == uniform_offsets_.end()) || (offset->second >= uniform_buffer_data.size())) {
                    continue;
                }

                copy_uniform_value_to_std140(
                    uniform_buffer_data.data() + offset->second,
                    uniform_buffer_data.size() - offset->second,
                    type,
                    array_size,
                    value->second.data(),
                    value->second.size());
            }

            void *mapped = driver->device().mapMemory(
                uniform_memory_.get(),
                0,
                static_cast<vk::DeviceSize>(uniform_buffer_size_));
            std::memcpy(mapped, uniform_buffer_data.data(), uniform_buffer_data.size());
            driver->device().unmapMemory(uniform_memory_.get());
        }

        std::vector<std::uint32_t> descriptor_bindings;
        std::vector<vk::DescriptorImageInfo> image_infos;
        descriptor_bindings.reserve(sampler_descriptor_bindings_.size());
        image_infos.reserve(sampler_descriptor_bindings_.size());

        for (const auto &sampler_binding : sampler_descriptor_bindings_) {
            int texture_slot = texture_binding_for(sampler_binding.first);
            if (texture_slot < 0) {
                const auto uniform_value = uniform_values.find(sampler_binding.first);
                if ((uniform_value != uniform_values.end()) && (uniform_value->second.size() >= sizeof(std::int32_t))) {
                    std::memcpy(&texture_slot, uniform_value->second.data(), sizeof(texture_slot));
                }
            }

            if ((texture_slot < 0) || (static_cast<std::size_t>(texture_slot) >= texture_slots.size())) {
                static bool warned = false;
                if (!warned) {
                    LOG_WARN(DRIVER_GRAPHICS, "Skipping Vulkan draw with missing sampler texture binding");
                    warned = true;
                }
                return false;
            }

            vulkan_texture *texture = texture_slots[static_cast<std::size_t>(texture_slot)];
            if (!texture || !texture->image_view() || !texture->sampler()) {
                static bool warned = false;
                if (!warned) {
                    LOG_WARN(DRIVER_GRAPHICS, "Skipping Vulkan draw with incomplete sampler texture");
                    warned = true;
                }
                return false;
            }

            const vk::ImageLayout image_layout = (texture->layout() == vk::ImageLayout::eUndefined)
                ? vk::ImageLayout::eShaderReadOnlyOptimal
                : texture->layout();
            if (vulkan_texture_trace_enabled()) {
                static std::uint32_t trace_count = 0;
                if (trace_count < 256) {
                    const eka2l1::vec2 texture_size = texture->get_size();
                    LOG_INFO(DRIVER_GRAPHICS,
                        "Vulkan sampler descriptor #{} shader_binding={} descriptor_binding={} texture_slot={} texture=0x{:X} size={}x{} layout={}",
                        ++trace_count,
                        sampler_binding.first,
                        sampler_binding.second,
                        texture_slot,
                        reinterpret_cast<std::uintptr_t>(texture),
                        texture_size.x,
                        texture_size.y,
                        vk::to_string(image_layout));
                }
            }
            descriptor_bindings.push_back(sampler_binding.second);
            image_infos.emplace_back(texture->sampler(), texture->image_view(), image_layout);
        }

        if (!image_infos.empty()) {
            std::vector<vk::WriteDescriptorSet> descriptor_writes;
            descriptor_writes.reserve(image_infos.size());
            for (std::size_t i = 0; i < image_infos.size(); i++) {
                vk::WriteDescriptorSet descriptor_write;
                descriptor_write.dstSet = descriptor_set_;
                descriptor_write.dstBinding = descriptor_bindings[i];
                descriptor_write.descriptorCount = 1;
                descriptor_write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
                descriptor_write.pImageInfo = &image_infos[i];
                descriptor_writes.push_back(descriptor_write);
            }

            driver->device().updateDescriptorSets(descriptor_writes, nullptr);
        }

        return true;
    }

    bool vulkan_shader_program::prepare_descriptor_snapshot(vulkan_graphics_driver *driver,
        const std::array<vulkan_texture *, VULKAN_SHADER_MAX_TEXTURE_SLOTS> &texture_slots,
        vulkan_shader_descriptor_snapshot &snapshot,
        const vulkan_shader_descriptor_state *state) {
        snapshot = {};

        if (!driver || !driver->device()) {
            return false;
        }

        if ((uniform_buffer_size_ == 0) && sampler_descriptor_bindings_.empty()) {
            return true;
        }

        if (!descriptor_set_layout_) {
            return false;
        }

        const auto &uniform_values = state ? state->uniform_values : uniform_values_;
        const auto &texture_bindings = state ? state->texture_bindings : texture_bindings_;
        auto texture_binding_for = [&](const int shader_binding) {
            const auto found = texture_bindings.find(shader_binding);
            return (found == texture_bindings.end()) ? -1 : found->second;
        };

        try {
            std::vector<vk::DescriptorPoolSize> pool_sizes;
            if (uniform_buffer_size_ > 0) {
                pool_sizes.emplace_back(vk::DescriptorType::eUniformBuffer, 1);
            }

            if (!sampler_descriptor_bindings_.empty()) {
                pool_sizes.emplace_back(
                    vk::DescriptorType::eCombinedImageSampler,
                    static_cast<std::uint32_t>(sampler_descriptor_bindings_.size()));
            }

            vk::DescriptorPoolCreateInfo pool_create_info;
            pool_create_info.maxSets = 1;
            pool_create_info.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
            pool_create_info.pPoolSizes = pool_sizes.empty() ? nullptr : pool_sizes.data();
            snapshot.descriptor_pool = driver->device().createDescriptorPoolUnique(pool_create_info);

            const vk::DescriptorSetLayout set_layout = descriptor_set_layout_.get();
            vk::DescriptorSetAllocateInfo allocate_info(snapshot.descriptor_pool.get(), 1, &set_layout);
            snapshot.descriptor_set = driver->device().allocateDescriptorSets(allocate_info).front();

            std::vector<vk::WriteDescriptorSet> descriptor_writes;
            descriptor_writes.reserve((uniform_buffer_size_ > 0 ? 1 : 0) + sampler_descriptor_bindings_.size());

            vk::DescriptorBufferInfo buffer_info;
            if (uniform_buffer_size_ > 0) {
                std::vector<std::uint8_t> uniform_buffer_data(uniform_buffer_size_, 0);
                shader_program_metadata metadata(metadata_.data());
                for (std::uint16_t i = 0; i < metadata.get_uniform_count(); i++) {
                    std::string name;
                    std::int32_t binding = -1;
                    shader_var_type type = shader_var_type::none;
                    std::int32_t array_size = 1;
                    if (!metadata.get_uniform_info(i, name, binding, type, array_size) || is_sampler_type(type)) {
                        continue;
                    }

                    const auto value = uniform_values.find(binding);
                    const auto offset = uniform_offsets_.find(binding);
                    if ((value == uniform_values.end()) || (offset == uniform_offsets_.end()) || (offset->second >= uniform_buffer_data.size())) {
                        continue;
                    }

                    copy_uniform_value_to_std140(
                        uniform_buffer_data.data() + offset->second,
                        uniform_buffer_data.size() - offset->second,
                        type,
                        array_size,
                        value->second.data(),
                        value->second.size());
                }

                vk::BufferCreateInfo buffer_create_info(
                    vk::BufferCreateFlags{},
                    static_cast<vk::DeviceSize>(uniform_buffer_size_),
                    vk::BufferUsageFlagBits::eUniformBuffer,
                    vk::SharingMode::eExclusive);
                snapshot.uniform_buffer = driver->device().createBufferUnique(buffer_create_info);

                const vk::MemoryRequirements memory_requirements = driver->device().getBufferMemoryRequirements(snapshot.uniform_buffer.get());
                vk::MemoryAllocateInfo allocate_memory_info(
                    memory_requirements.size,
                    driver->find_memory_type(
                        memory_requirements.memoryTypeBits,
                        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
                snapshot.uniform_memory = driver->device().allocateMemoryUnique(allocate_memory_info);
                driver->device().bindBufferMemory(snapshot.uniform_buffer.get(), snapshot.uniform_memory.get(), 0);

                void *mapped = driver->device().mapMemory(
                    snapshot.uniform_memory.get(),
                    0,
                    static_cast<vk::DeviceSize>(uniform_buffer_size_));
                std::memcpy(mapped, uniform_buffer_data.data(), uniform_buffer_data.size());
                driver->device().unmapMemory(snapshot.uniform_memory.get());

                buffer_info = vk::DescriptorBufferInfo(
                    snapshot.uniform_buffer.get(),
                    0,
                    static_cast<vk::DeviceSize>(uniform_buffer_size_));

                vk::WriteDescriptorSet descriptor_write;
                descriptor_write.dstSet = snapshot.descriptor_set;
                descriptor_write.dstBinding = 0;
                descriptor_write.descriptorCount = 1;
                descriptor_write.descriptorType = vk::DescriptorType::eUniformBuffer;
                descriptor_write.pBufferInfo = &buffer_info;
                descriptor_writes.push_back(descriptor_write);
            }

            std::vector<vk::DescriptorImageInfo> image_infos;
            image_infos.reserve(sampler_descriptor_bindings_.size());
            for (const auto &sampler_binding : sampler_descriptor_bindings_) {
                int texture_slot = texture_binding_for(sampler_binding.first);
                if (texture_slot < 0) {
                    const auto uniform_value = uniform_values.find(sampler_binding.first);
                    if ((uniform_value != uniform_values.end()) && (uniform_value->second.size() >= sizeof(std::int32_t))) {
                        std::memcpy(&texture_slot, uniform_value->second.data(), sizeof(texture_slot));
                    }
                }

                if ((texture_slot < 0) || (static_cast<std::size_t>(texture_slot) >= texture_slots.size())) {
                    static bool warned = false;
                    if (!warned) {
                        LOG_WARN(DRIVER_GRAPHICS, "Skipping Vulkan draw with missing sampler texture binding");
                        warned = true;
                    }
                    return false;
                }

                vulkan_texture *texture = texture_slots[static_cast<std::size_t>(texture_slot)];
                if (!texture || !texture->image_view() || !texture->sampler()) {
                    static bool warned = false;
                    if (!warned) {
                        LOG_WARN(DRIVER_GRAPHICS, "Skipping Vulkan draw with incomplete sampler texture");
                        warned = true;
                    }
                    return false;
                }

                const vk::ImageLayout image_layout = (texture->layout() == vk::ImageLayout::eUndefined)
                    ? vk::ImageLayout::eShaderReadOnlyOptimal
                    : texture->layout();
                if (vulkan_texture_trace_enabled()) {
                    static std::uint32_t trace_count = 0;
                    if (trace_count < 256) {
                        const eka2l1::vec2 texture_size = texture->get_size();
                        LOG_INFO(DRIVER_GRAPHICS,
                            "Vulkan sampler descriptor #{} shader_binding={} descriptor_binding={} texture_slot={} texture=0x{:X} size={}x{} layout={}",
                            ++trace_count,
                            sampler_binding.first,
                            sampler_binding.second,
                            texture_slot,
                            reinterpret_cast<std::uintptr_t>(texture),
                            texture_size.x,
                            texture_size.y,
                            vk::to_string(image_layout));
                    }
                }

                image_infos.emplace_back(texture->sampler(), texture->image_view(), image_layout);
                vk::WriteDescriptorSet descriptor_write;
                descriptor_write.dstSet = snapshot.descriptor_set;
                descriptor_write.dstBinding = sampler_binding.second;
                descriptor_write.descriptorCount = 1;
                descriptor_write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
                descriptor_write.pImageInfo = &image_infos.back();
                descriptor_writes.push_back(descriptor_write);
            }

            if (!descriptor_writes.empty()) {
                driver->device().updateDescriptorSets(descriptor_writes, nullptr);
            }
        } catch (std::exception &e) {
            LOG_ERROR(DRIVER_GRAPHICS, "Failed to create Vulkan shader descriptor snapshot: {}", e.what());
            snapshot = {};
            return false;
        }

        return true;
    }
}

#endif

#undef EKA2L1_USE_VULKAN_BACKEND
