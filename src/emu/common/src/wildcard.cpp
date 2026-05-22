/*
 * Copyright (c) 2019 EKA2L1 Team.
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

#include <common/algorithm.h>
#include <common/cvt.h>
#include <common/wildcard.h>

#include <regex>

namespace eka2l1::common {
    namespace {
        std::regex_constants::syntax_option_type wildcard_regex_flags(const bool case_sensitive) {
            auto flags = std::regex_constants::ECMAScript;

            if (!case_sensitive) {
                flags |= std::regex_constants::icase;
            }

            return flags;
        }

        template <typename T>
        std::basic_string<T> wildcard_to_regex_string_impl(const std::basic_string<T> &wildcard) {
            std::basic_string<T> regexstr;
            regexstr.reserve(wildcard.size() * 2);

            for (const T ch : wildcard) {
                switch (ch) {
                case static_cast<T>('*'):
                    regexstr += static_cast<T>('.');
                    regexstr += static_cast<T>('*');
                    break;
                case static_cast<T>('?'):
                    regexstr += static_cast<T>('.');
                    break;
                case static_cast<T>('\\'):
                case static_cast<T>('.'):
                case static_cast<T>('^'):
                case static_cast<T>('$'):
                case static_cast<T>('|'):
                case static_cast<T>('('):
                case static_cast<T>(')'):
                case static_cast<T>('['):
                case static_cast<T>(']'):
                case static_cast<T>('{'):
                case static_cast<T>('}'):
                case static_cast<T>('+'):
                    regexstr += static_cast<T>('\\');
                    regexstr += ch;
                    break;
                default:
                    regexstr += ch;
                    break;
                }
            }

            return regexstr;
        }
    }

    template <>
    std::basic_string<char> wildcard_to_regex_string(std::basic_string<char> regexstr, bool case_sensitive) {
        (void)case_sensitive;
        return wildcard_to_regex_string_impl(regexstr);
    }

    template <>
    std::basic_string<wchar_t> wildcard_to_regex_string(std::basic_string<wchar_t> regexstr, bool case_sensitive) {
        (void)case_sensitive;
        return wildcard_to_regex_string_impl(regexstr);
    }

    bool full_wildcard_match_impl(const std::string &reference, const std::string &match_pattern,
        const bool is_fold) {
        const std::regex regex(wildcard_to_regex_string(match_pattern, !is_fold), wildcard_regex_flags(!is_fold));
        return std::regex_match(reference, regex);
    }

    template <>
    bool full_wildcard_match<char>(const std::string &reference, const std::string &match_pattern,
        const bool is_fold) {
        return full_wildcard_match_impl(reference, match_pattern, is_fold);
    }

    template <>
    bool full_wildcard_match<wchar_t>(const std::wstring &reference, const std::wstring &match_pattern,
        const bool is_fold) {
        return full_wildcard_match_impl(common::wstr_to_utf8(reference), common::wstr_to_utf8(match_pattern), is_fold);
    }

    std::size_t wildcard_match_impl(const std::string &reference, const std::string &match_pattern,
        const bool is_fold) {
        const std::regex regex(wildcard_to_regex_string(match_pattern, !is_fold), wildcard_regex_flags(!is_fold));
        std::smatch result;

        if (!std::regex_search(reference, result, regex)) {
            return std::string::npos;
        }

        return static_cast<std::size_t>(result.position(0));
    }

    template <>
    std::size_t wildcard_match<char>(const std::string &reference, const std::string &match_pattern,
        const bool is_fold) {
        return wildcard_match_impl(reference, match_pattern, is_fold);
    }

    template <>
    std::size_t wildcard_match<wchar_t>(const std::wstring &reference, const std::wstring &match_pattern,
        const bool is_fold) {
        return wildcard_match_impl(common::wstr_to_utf8(reference), common::wstr_to_utf8(match_pattern), is_fold);
    }
}
