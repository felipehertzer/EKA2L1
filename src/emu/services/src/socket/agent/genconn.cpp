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

#include <common/log.h>
#include <services/internet/protocols/common.h>
#include <services/socket/agent/genconn.h>
#include <services/socket/server.h>

#include <algorithm>
#include <cstring>

namespace eka2l1::epoc::socket {
    namespace {
        constexpr std::uint32_t HOST_IAP_ID = 1;
        constexpr std::uint32_t HOST_SERVICE_ID = 1;
        constexpr std::uint32_t HOST_NETWORK_ID = 1;

        std::u16string normalize_setting_name(std::u16string name) {
            std::replace(name.begin(), name.end(), u'/', u'\\');
            std::transform(name.begin(), name.end(), name.begin(), [](const char16_t ch) {
                if ((ch >= u'A') && (ch <= u'Z')) {
                    return static_cast<char16_t>(ch + (u'a' - u'A'));
                }

                return ch;
            });

            return name;
        }

        bool write_uint_setting(std::uint8_t *dest_buffer, const std::size_t avail_size, const std::uint32_t value) {
            if (avail_size < sizeof(value)) {
                return false;
            }

            std::memcpy(dest_buffer, &value, sizeof(value));
            return true;
        }

        std::size_t write_des_setting(std::uint8_t *dest_buffer, const std::size_t avail_size, const std::u16string &value) {
            const std::size_t byte_size = value.size() * sizeof(char16_t);
            if (avail_size < byte_size) {
                return static_cast<std::size_t>(-1);
            }

            std::memcpy(dest_buffer, value.data(), byte_size);
            return byte_size;
        }

        class generic_host_connection final : public connection {
        public:
            explicit generic_host_connection(protocol *pr)
                : connection(pr, saddress{}) {
            }

            std::size_t get_setting(const std::u16string &setting_name, const setting_type type, std::uint8_t *dest_buffer,
                std::size_t avail_size) override {
                const std::u16string normalized_name = normalize_setting_name(setting_name);

                if ((type == setting_type_int) || (type == setting_type_bool)) {
                    std::uint32_t value = 0;

                    if ((normalized_name == u"iap\\id") || (normalized_name == u"iap\\iapservice")
                        || (normalized_name == u"lanservice\\id") || (normalized_name == u"network\\id")) {
                        value = HOST_IAP_ID;
                    } else if (normalized_name == u"iap\\network") {
                        value = HOST_NETWORK_ID;
                    } else if ((normalized_name == u"iap\\service") || (normalized_name == u"iap\\serviceid")) {
                        value = HOST_SERVICE_ID;
                    } else if ((normalized_name == u"lanservice\\ipaddrfromserver")
                        || (normalized_name == u"lanservice\\ipdnsaddrfromserver")) {
                        value = 1;
                    } else if ((normalized_name == u"iap\\bearer") || (normalized_name == u"iap\\bearertype")
                        || (normalized_name == u"iap\\iapbearer") || (normalized_name == u"connectionpreferences\\iapid")) {
                        value = HOST_IAP_ID;
                    } else {
                        return static_cast<std::size_t>(-1);
                    }

                    return write_uint_setting(dest_buffer, avail_size, value) ? sizeof(value) : static_cast<std::size_t>(-1);
                }

                if (type == setting_type_des) {
                    if ((normalized_name == u"iap\\name") || (normalized_name == u"lanservice\\name")) {
                        return write_des_setting(dest_buffer, avail_size, u"EKA2L1 Host Internet");
                    }

                    if ((normalized_name == u"iap\\iapservicetype") || (normalized_name == u"iap\\servicetype")) {
                        return write_des_setting(dest_buffer, avail_size, u"LANService");
                    }

                    if ((normalized_name == u"lanservice\\configdaemonmanagername")
                        || (normalized_name == u"lanservice\\configdaemonname")) {
                        return write_des_setting(dest_buffer, avail_size, u"ConfigDaemon");
                    }

                    if (normalized_name == u"network\\name") {
                        return write_des_setting(dest_buffer, avail_size, u"EKA2L1");
                    }
                }

                return static_cast<std::size_t>(-1);
            }
        };
    }

    std::unique_ptr<connection> generic_connect_agent::start_connection(conn_preferences &prefs) {
        protocol *inet = sock_serv_->find_protocol(epoc::internet::INET_ADDRESS_FAMILY, epoc::internet::INET_TCP_PROTOCOL_ID);
        if (!inet) {
            LOG_ERROR(SERVICE_ESOCK, "Generic connection agent cannot find the INet TCP protocol");
            return nullptr;
        }

        LOG_TRACE(SERVICE_ESOCK, "Generic connection agent opened host-routed INet connection, prefs=0x{:X}",
            prefs.reserved_);
        return std::make_unique<generic_host_connection>(inet);
    }
}
