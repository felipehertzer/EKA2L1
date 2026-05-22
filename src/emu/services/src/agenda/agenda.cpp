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

#include <services/agenda/agenda.h>
#include <system/epoc.h>

#include <utils/des.h>
#include <utils/err.h>

#include <vfs/vfs.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace eka2l1 {
    static constexpr const char *AGENDA_SERVER_NAME = "!AgendaServer";

    namespace {
        // Keep these in sync with Symbian Agenda Model's TAgendaServerFunction.
        enum agenda_opcode {
            agn_open_agenda = 0,
            agn_close_agenda = 1,
            agn_transmit_buffer = 2,
            agn_get_instance_extractor = 3,
            agn_previous_instances = 4,
            agn_next_instances = 5,
            agn_create_entry_iterator = 6,
            agn_entry_iterator_next = 7,
            agn_get_entry_uids_since_date = 8,
            agn_get_category_list_count = 9,
            agn_category_filter = 10,
            agn_start_build_index = 11,
            agn_get_list_file_names = 12,
            agn_cancel_task = 13,
            agn_resource_count = 14,
            agn_heap_size_count = 15,
            agn_set_heap_failure = 16,
            agn_agenda_file_exists = 17,
            agn_disable_change_broadcast = 18,
            agn_enable_change_broadcast = 19,
            agn_request_change_notification_parameters = 20,
            agn_request_change_notification = 21,
            agn_cancel_change_notification = 22,
            agn_request_progress = 23,
            agn_set_update_alarm = 24,
            agn_set_enable_pub_sub_notification = 25,
            agn_restore_alarm_action = 26,
            agn_tz_db_changed_time = 27,

            agn_fetch_entry = 100,
            agn_fetch_entry_by_uid = 101,
            agn_fetch_simple_entry = 102,
            agn_fetch_simple_entries = 103,
            agn_restore_text = 104,
            agn_get_category_list_item = 105,
            agn_get_changes_since_last_notification = 106,
            agn_find_instances = 107,
            agn_fetch_entry_by_guid = 108,
            agn_fetch_simple_entries_by_guid = 109,
            agn_transfer_attachment_file_to_client = 110,
            agn_fetch_sorted_attachments = 111,
            agn_entries_with_attachment = 112,
            agn_fetch_attachment_by_id = 113,
            agn_instance_iterator_create = 114,
            agn_instance_iterator_destroy = 115,
            agn_instance_iterator_next = 116,
            agn_instance_iterator_previous = 117,
            agn_instance_iterator_count = 118,
            agn_instance_iterator_locate_index = 119,
            agn_fetch_simple_entry_by_uid = 120,
            agn_entry_iterator_position = 121,
            agn_get_calendar_info = 122,
            agn_get_property_value = 123,
            agn_get_file_changes_since_last_notification = 124,

            agn_update_entry = 200,
            agn_add_entry = 201,
            agn_delete_entry = 202,
            agn_add_category_to_list = 203,
            agn_delete_agenda_file = 204,
            agn_tidy_by_date_read_params = 205,
            agn_tidy_by_date_start = 206,
            agn_category_start = 207,
            agn_category_start_async = 208,
            agn_create_agenda_file = 209,
            agn_delete_entries_by_local_uid = 210,
            agn_delete_entry_by_guid = 211,
            agn_commit = 212,
            agn_rollback = 213,
            agn_transfer_attachment_file_to_server = 214,
            agn_transfer_file_to_client_to_write = 215,
            agn_move_file_to_server = 216,
            agn_set_calendar_info = 217
        };

        constexpr std::int32_t FALSE_VALUE = 0;
        constexpr std::int32_t TRUE_VALUE = 1;
        constexpr std::int32_t EMPTY_COUNT = 0;
        constexpr std::int32_t CURRENT_FILE_VERSION = 0;
        constexpr std::int64_t ZERO_TIME = 0;
        constexpr const char16_t *DEFAULT_CALENDAR_STORE_PATH = u"C:\\private\\10003a5b\\calendar";

        std::string_view agenda_opcode_name(const int opcode) {
            switch (opcode) {
            case agn_open_agenda:
                return "EOpenAgenda";
            case agn_close_agenda:
                return "ECloseAgenda";
            case agn_transmit_buffer:
                return "ETransmitBuffer";
            case agn_get_instance_extractor:
                return "EGetInstanceExtractor";
            case agn_previous_instances:
                return "EPreviousInstances";
            case agn_next_instances:
                return "ENextInstances";
            case agn_create_entry_iterator:
                return "ECreateEntryIterator";
            case agn_entry_iterator_next:
                return "EEntryIteratorNext";
            case agn_get_entry_uids_since_date:
                return "EGetEntryUidsSinceDate";
            case agn_get_category_list_count:
                return "EGetCategoryListCount";
            case agn_category_filter:
                return "ECategoryFilter";
            case agn_start_build_index:
                return "EStartBuildIndex";
            case agn_get_list_file_names:
                return "EGetListFileNames";
            case agn_cancel_task:
                return "ECancelTask";
            case agn_resource_count:
                return "EAgnResourceCount";
            case agn_heap_size_count:
                return "EAgnHeapSizeCount";
            case agn_set_heap_failure:
                return "EAgnSetHeapFailure";
            case agn_agenda_file_exists:
                return "EAgendaFileExists";
            case agn_disable_change_broadcast:
                return "EDisableChangeBroadcast";
            case agn_enable_change_broadcast:
                return "EEnableChangeBroadcast";
            case agn_request_change_notification_parameters:
                return "ERequestChangeNotificationParameters";
            case agn_request_change_notification:
                return "ERequestChangeNotification";
            case agn_cancel_change_notification:
                return "ECancelChangeNotification";
            case agn_request_progress:
                return "ERequestProgress";
            case agn_set_update_alarm:
                return "ESetUpdateAlarm";
            case agn_set_enable_pub_sub_notification:
                return "ESetEnablePubSubNotification";
            case agn_restore_alarm_action:
                return "ERestoreAlarmAction";
            case agn_tz_db_changed_time:
                return "ETzDbChangedTime";
            case agn_fetch_entry:
                return "EFetchEntry";
            case agn_fetch_entry_by_uid:
                return "EFetchEntryByUID";
            case agn_fetch_simple_entry:
                return "EFetchSimpleEntry";
            case agn_fetch_simple_entries:
                return "EFetchSimpleEntries";
            case agn_restore_text:
                return "ERestoreText";
            case agn_get_category_list_item:
                return "EGetCategoryListItem";
            case agn_get_changes_since_last_notification:
                return "EGetChangesSinceLastNotification";
            case agn_find_instances:
                return "EFindInstances";
            case agn_fetch_entry_by_guid:
                return "EFetchEntryByGuid";
            case agn_fetch_simple_entries_by_guid:
                return "EFetchSimpleEntriesByGuid";
            case agn_transfer_attachment_file_to_client:
                return "ETransferAttachmentFileToClient";
            case agn_fetch_sorted_attachments:
                return "EFetchSortedAttachments";
            case agn_entries_with_attachment:
                return "EEntriesWithAttachment";
            case agn_fetch_attachment_by_id:
                return "EFetchAttachmentById";
            case agn_instance_iterator_create:
                return "EInstanceIteratorCreate";
            case agn_instance_iterator_destroy:
                return "EInstanceIteratorDestroy";
            case agn_instance_iterator_next:
                return "EInstanceIteratorNext";
            case agn_instance_iterator_previous:
                return "EInstanceIteratorPrevious";
            case agn_instance_iterator_count:
                return "EInstanceIteratorCount";
            case agn_instance_iterator_locate_index:
                return "EInstanceIteratorLocateIndex";
            case agn_fetch_simple_entry_by_uid:
                return "EFetchSimpleEntryByUID";
            case agn_entry_iterator_position:
                return "EEntryIteratorPosition";
            case agn_get_calendar_info:
                return "EGetCalendarInfo";
            case agn_get_property_value:
                return "EGetPropertyValue";
            case agn_get_file_changes_since_last_notification:
                return "EGetFileChangesSinceLastNotification";
            case agn_update_entry:
                return "EUpdateEntry";
            case agn_add_entry:
                return "EAddEntry";
            case agn_delete_entry:
                return "EDeleteEntry";
            case agn_add_category_to_list:
                return "EAddCategoryToList";
            case agn_delete_agenda_file:
                return "EDeleteAgendaFile";
            case agn_tidy_by_date_read_params:
                return "ETidyByDateReadParams";
            case agn_tidy_by_date_start:
                return "ETidyByDateStart";
            case agn_category_start:
                return "ECategoryStart";
            case agn_category_start_async:
                return "ECategoryStartAsyn";
            case agn_create_agenda_file:
                return "ECreateAgendaFile";
            case agn_delete_entries_by_local_uid:
                return "EDeleteEntriesByLocalUid";
            case agn_delete_entry_by_guid:
                return "EDeleteEntryByGuid";
            case agn_commit:
                return "ECommit";
            case agn_rollback:
                return "ERollback";
            case agn_transfer_attachment_file_to_server:
                return "ETransferAttachmentFileToServer";
            case agn_transfer_file_to_client_to_write:
                return "ETransferFileToClientToWrite";
            case agn_move_file_to_server:
                return "EMoveFileToServer";
            case agn_set_calendar_info:
                return "ESetCalendarInfo";
            default:
                return "unknown";
            }
        }

        void trace_agenda_ipc(service::ipc_context *ctx) {
            const auto &args = ctx->msg->args.args;
            LOG_TRACE(SERVICE_TRACK,
                "!AgendaServer opcode 0x{:X} ({}) flag=0x{:X} args=[0x{:X}, 0x{:X}, 0x{:X}, 0x{:X}]",
                ctx->msg->function, agenda_opcode_name(ctx->msg->function), ctx->flag(),
                static_cast<std::uint32_t>(args[0]), static_cast<std::uint32_t>(args[1]),
                static_cast<std::uint32_t>(args[2]), static_cast<std::uint32_t>(args[3]));
        }

        bool write_descriptor(service::ipc_context *ctx, const int slot, const void *data, const std::uint32_t size) {
            int write_error = 0;
            if (!ctx->write_data_to_descriptor_argument(slot, reinterpret_cast<const std::uint8_t *>(data), size, &write_error)) {
                LOG_WARN(SERVICE_TRACK, "!AgendaServer failed to write slot {} descriptor: {}", slot, write_error);
                return false;
            }
            return true;
        }

        template <typename T>
        bool write_descriptor(service::ipc_context *ctx, const int slot, const T &data) {
            return write_descriptor(ctx, slot, &data, static_cast<std::uint32_t>(sizeof(T)));
        }

        void write_inline_package_buffer(std::uint8_t *package, const void *data, const std::uint32_t size) {
            auto *words = reinterpret_cast<std::uint32_t *>(package);
            words[0] = (static_cast<std::uint32_t>(epoc::buf) << 28) | size;
            words[1] = size;
            std::memcpy(package + (sizeof(std::uint32_t) * 2), data, size);
        }

        template <typename T>
        bool try_write_descriptor(service::ipc_context *ctx, const int slot, const T &data) {
            const auto *raw_data = reinterpret_cast<const std::uint8_t *>(&data);
            const auto raw_size = static_cast<std::uint32_t>(sizeof(T));

            int write_error = 0;
            if (ctx->write_data_to_descriptor_argument(slot, raw_data, raw_size, &write_error, true)) {
                return true;
            }

            if (write_error != -1) {
                LOG_WARN(SERVICE_TRACK, "!AgendaServer failed to write slot {} descriptor: {}", slot, write_error);
                return false;
            }

            const auto arg_addr = static_cast<std::uint32_t>(ctx->msg->args.args[slot]);
            if (arg_addr < 0x1000) {
                return true;
            }

            kernel::process *own_pr = ctx->msg->own_thr->owning_process();
            std::uint8_t *arg_ptr = ptr<std::uint8_t>(arg_addr).get(own_pr);
            if (!arg_ptr) {
                LOG_WARN(SERVICE_TRACK, "!AgendaServer failed to resolve slot {} package at 0x{:X}", slot, arg_addr);
                return false;
            }

            const auto *package_words = reinterpret_cast<const std::uint32_t *>(arg_ptr);
            LOG_TRACE(SERVICE_TRACK,
                "!AgendaServer slot {} fallback write size={} addr=0x{:X} words=[0x{:X}, 0x{:X}, 0x{:X}, 0x{:X}] argtype=0x{:X}",
                slot, raw_size, arg_addr, package_words[0], package_words[1], package_words[2], package_words[3],
                static_cast<int>(ctx->msg->args.get_arg_type(slot)));

            epoc::des8 *des = reinterpret_cast<epoc::des8 *>(arg_ptr);
            if (des->is_valid_descriptor()) {
                const epoc::des_type dtype = des->get_descriptor_type();
                const bool is_mutable_descriptor = (dtype == epoc::buf) || (dtype == epoc::ptr) || (dtype == epoc::ptr_to_buf);
                if (is_mutable_descriptor && des->get_pointer_raw(own_pr) && (des->get_max_length(own_pr) >= raw_size)) {
                    if (des->assign(own_pr, raw_data, raw_size) != 0) {
                        LOG_WARN(SERVICE_TRACK, "!AgendaServer failed to write legacy slot {} descriptor", slot);
                        return false;
                    }
                    LOG_TRACE(SERVICE_TRACK,
                        "!AgendaServer slot {} wrote descriptor dtype={} max={}",
                        slot, static_cast<int>(dtype), des->get_max_length(own_pr));
                    return true;
                }
            }

            const std::uint32_t indirect_arg_addr = package_words[0];
            if (indirect_arg_addr >= 0x1000) {
                std::uint8_t *indirect_arg_ptr = ptr<std::uint8_t>(indirect_arg_addr).get(own_pr);
                if (indirect_arg_ptr) {
                    epoc::des8 *indirect_des = reinterpret_cast<epoc::des8 *>(indirect_arg_ptr);
                    if (indirect_des->is_valid_descriptor()) {
                        const epoc::des_type dtype = indirect_des->get_descriptor_type();
                        const bool is_mutable_descriptor = (dtype == epoc::buf) || (dtype == epoc::ptr) || (dtype == epoc::ptr_to_buf);
                        if (is_mutable_descriptor && indirect_des->get_pointer_raw(own_pr) && (indirect_des->get_max_length(own_pr) >= raw_size)) {
                            if (indirect_des->assign(own_pr, raw_data, raw_size) != 0) {
                                LOG_WARN(SERVICE_TRACK, "!AgendaServer failed to write indirect slot {} descriptor", slot);
                                return false;
                            }
                            LOG_TRACE(SERVICE_TRACK,
                                "!AgendaServer slot {} wrote indirect descriptor at 0x{:X} dtype={} max={}",
                                slot, indirect_arg_addr, static_cast<int>(dtype), indirect_des->get_max_length(own_pr));
                            return true;
                        }
                    }

                    const auto *indirect_package_words = reinterpret_cast<const std::uint32_t *>(indirect_arg_ptr);
                    if ((raw_size <= sizeof(std::uint32_t)) && (indirect_package_words[0] == 0) && (indirect_package_words[1] == 0)) {
                        write_inline_package_buffer(indirect_arg_ptr, raw_data, raw_size);
                        LOG_TRACE(SERVICE_TRACK,
                            "!AgendaServer slot {} initialised indirect zeroed package buffer at 0x{:X}",
                            slot, indirect_arg_addr);
                        return true;
                    }

                    LOG_TRACE(SERVICE_TRACK,
                        "!AgendaServer slot {} indirect cell 0x{:X} words=[0x{:X}, 0x{:X}, 0x{:X}, 0x{:X}]",
                        slot, indirect_arg_addr, indirect_package_words[0], indirect_package_words[1],
                        indirect_package_words[2], indirect_package_words[3]);

                    LOG_TRACE(SERVICE_TRACK,
                        "!AgendaServer slot {} skipped non-descriptor pointer cell at 0x{:X}",
                        slot, indirect_arg_addr);
                    return true;
                }
            }

            auto *mutable_package_words = reinterpret_cast<std::uint32_t *>(arg_ptr);
            const std::uint32_t package_max_length = mutable_package_words[1] & 0x00FFFFFF;
            if ((package_max_length >= raw_size) && (package_max_length <= 0x100000)) {
                mutable_package_words[0] = (mutable_package_words[0] & 0xF0000000) | raw_size;
                std::memcpy(arg_ptr + (sizeof(std::uint32_t) * 2), raw_data, raw_size);
                LOG_TRACE(SERVICE_TRACK,
                    "!AgendaServer slot {} wrote package buffer max={}",
                    slot, package_max_length);
                return true;
            }

            if ((raw_size <= sizeof(std::uint32_t)) && (package_words[0] == 0) && (package_words[1] == 0)) {
                write_inline_package_buffer(arg_ptr, raw_data, raw_size);
                LOG_TRACE(SERVICE_TRACK,
                    "!AgendaServer slot {} initialised zeroed inline package buffer",
                    slot);
                return true;
            }

            LOG_TRACE(SERVICE_TRACK, "!AgendaServer slot {} wrote raw package bytes", slot);
            std::memcpy(arg_ptr, raw_data, raw_size);
            return true;
        }

        bool write_empty_stream(service::ipc_context *ctx, std::vector<std::uint8_t> &buffer, const int data_slot, const int size_slot) {
            buffer.resize(sizeof(EMPTY_COUNT));
            std::memcpy(buffer.data(), &EMPTY_COUNT, sizeof(EMPTY_COUNT));

            if (!write_descriptor(ctx, data_slot, buffer.data(), static_cast<std::uint32_t>(buffer.size()))) {
                return false;
            }

            return try_write_descriptor(ctx, size_slot, static_cast<std::int32_t>(buffer.size()));
        }

        bool write_empty_file_list(service::ipc_context *ctx, std::vector<std::uint8_t> &buffer) {
            buffer.assign(1, 0);

            if (!write_descriptor(ctx, 0, buffer.data(), static_cast<std::uint32_t>(buffer.size()))) {
                return false;
            }

            return try_write_descriptor(ctx, 1, static_cast<std::int32_t>(buffer.size()));
        }

        void append_u8(std::vector<std::uint8_t> &buffer, const std::uint8_t value) {
            buffer.push_back(value);
        }

        void append_u16(std::vector<std::uint8_t> &buffer, const std::uint16_t value) {
            buffer.push_back(static_cast<std::uint8_t>(value & 0xFF));
            buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        }

        void append_u32(std::vector<std::uint8_t> &buffer, const std::uint32_t value) {
            buffer.push_back(static_cast<std::uint8_t>(value & 0xFF));
            buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
            buffer.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
            buffer.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
        }

        void append_i32(std::vector<std::uint8_t> &buffer, const std::int32_t value) {
            append_u32(buffer, static_cast<std::uint32_t>(value));
        }

        void append_u64(std::vector<std::uint8_t> &buffer, const std::uint64_t value) {
            append_u32(buffer, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
            append_u32(buffer, static_cast<std::uint32_t>((value >> 32) & 0xFFFFFFFFULL));
        }

        void append_i64(std::vector<std::uint8_t> &buffer, const std::int64_t value) {
            append_u64(buffer, static_cast<std::uint64_t>(value));
        }

        void append_real64(std::vector<std::uint8_t> &buffer, const double value) {
            std::uint64_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            append_u64(buffer, bits);
        }

        void append_null_calendar_time(std::vector<std::uint8_t> &buffer) {
            constexpr std::uint32_t NULL_TIME_LOW = 0;
            constexpr std::uint32_t NULL_TIME_HIGH_WITH_NEW_FORMAT = 0xC0000000;

            append_u32(buffer, NULL_TIME_LOW);
            append_u32(buffer, NULL_TIME_HIGH_WITH_NEW_FORMAT);
            append_u32(buffer, 0);
            append_u32(buffer, 0);
        }

        void build_synthetic_calendar_entry(std::vector<std::uint8_t> &buffer) {
            constexpr std::uint32_t ENTRY_TYPE_APPOINTMENT = 0;
            constexpr std::uint32_t ENTRY_ID = 1;
            constexpr std::int32_t ALARM_NOT_SET = 0x7FFFFFFF;
            constexpr std::uint8_t STATUS_NULL = 6;
            constexpr std::uint64_t SYMBIAN_NULL_TIME = 0x8000000000000000ULL;
            constexpr std::uint32_t LOCAL_UID = 1;
            constexpr std::uint8_t REPLICATION_OPEN = 0;
            constexpr std::uint8_t BUSY = 0;
            constexpr double GEO_DEFAULT = -999.999;
            constexpr std::uint8_t GS_PARENT = 0;
            constexpr std::int32_t METHOD_NONE = 0;
            constexpr std::int32_t SEQUENCE_NUMBER = 0;
            constexpr std::string_view GUID = "__eka2l1_empty_calendar__";

            buffer.clear();
            buffer.reserve(128);

            append_u32(buffer, ENTRY_TYPE_APPOINTMENT);

            append_u32(buffer, ENTRY_ID);
            append_u8(buffer, FALSE_VALUE);
            append_i32(buffer, ALARM_NOT_SET);
            append_u8(buffer, STATUS_NULL);
            append_i64(buffer, 0);
            append_u8(buffer, 0);
            append_null_calendar_time(buffer);
            append_null_calendar_time(buffer);
            append_u64(buffer, SYMBIAN_NULL_TIME);
            append_u32(buffer, LOCAL_UID);

            append_u8(buffer, REPLICATION_OPEN);
            append_u32(buffer, 0);
            append_u32(buffer, 0);
            append_u32(buffer, 0);
            append_u32(buffer, 0);
            append_u8(buffer, FALSE_VALUE);
            append_i64(buffer, 0);
            append_u32(buffer, 0);
            append_u32(buffer, 0);
            append_u32(buffer, 0);
            append_u8(buffer, BUSY);
            append_real64(buffer, GEO_DEFAULT);
            append_real64(buffer, GEO_DEFAULT);
            append_u16(buffer, 0);
            append_i32(buffer, 0);
            append_u32(buffer, sizeof(std::int32_t));
            append_i32(buffer, 0);

            append_u8(buffer, GS_PARENT);
            append_i32(buffer, SEQUENCE_NUMBER);
            append_u8(buffer, METHOD_NONE);
            append_u16(buffer, static_cast<std::uint16_t>(GUID.size()));
            buffer.insert(buffer.end(), GUID.begin(), GUID.end());
            append_i32(buffer, 0);

            append_u8(buffer, FALSE_VALUE);
            append_u8(buffer, FALSE_VALUE);
            append_u8(buffer, FALSE_VALUE);
            append_u8(buffer, FALSE_VALUE);
        }

        bool write_synthetic_calendar_entry(service::ipc_context *ctx, std::vector<std::uint8_t> &buffer) {
            build_synthetic_calendar_entry(buffer);

            if (!write_descriptor(ctx, 0, buffer.data(), static_cast<std::uint32_t>(buffer.size()))) {
                return false;
            }

            return try_write_descriptor(ctx, 1, static_cast<std::int32_t>(buffer.size()));
        }
    }

    agenda_server::agenda_server(eka2l1::system *sys)
        : service::typical_server(sys, AGENDA_SERVER_NAME) {
    }

    void agenda_server::connect(service::ipc_context &context) {
        create_session<agenda_session>(&context);
        context.complete(epoc::error_none);
    }

    std::int64_t agenda_server::allocate_file_id() {
        return next_file_id_++;
    }

    std::uint8_t agenda_server::allocate_collection_id() {
        return next_collection_id_++;
    }

    agenda_session::agenda_session(service::typical_server *serv, const kernel::uid ss_id, epoc::version client_version)
        : service::typical_session(serv, ss_id, client_version) {
    }

    agenda_session::~agenda_session() {
        if (change_request_) {
            change_request_->complete(epoc::error_cancel);
        }

        if (progress_request_) {
            progress_request_->complete(epoc::error_cancel);
        }
    }

    void agenda_session::fetch(service::ipc_context *ctx) {
        trace_agenda_ipc(ctx);

        switch (ctx->msg->function) {
        case agn_open_agenda: {
            io_system *io = ctx->sys->get_io_system();
            symfile backing_store = io->open_file(DEFAULT_CALENDAR_STORE_PATH, READ_MODE | BIN_MODE);
            if (!backing_store || (backing_store->size() == 0)) {
                LOG_TRACE(SERVICE_TRACK, "!AgendaServer default calendar store is missing or empty; using an empty in-memory view");
            }

            if (file_id_ == 0) {
                file_id_ = server<agenda_server>()->allocate_file_id();
            }

            if (collection_id_ == 0) {
                collection_id_ = server<agenda_server>()->allocate_collection_id();
            }

            const std::uint8_t collection_id = collection_id_;
            if (!try_write_descriptor(ctx, 1, CURRENT_FILE_VERSION)
                || !try_write_descriptor(ctx, 2, file_id_)
                || !try_write_descriptor(ctx, 3, collection_id)) {
                ctx->complete(epoc::error_argument);
                return;
            }

            ctx->complete(epoc::error_none);
            break;
        }

        case agn_close_agenda:
            ctx->complete(epoc::error_none);
            break;

        case agn_transmit_buffer:
            if (!transmit_buffer_.empty() && !write_descriptor(ctx, 0, transmit_buffer_.data(), static_cast<std::uint32_t>(transmit_buffer_.size()))) {
                ctx->complete(epoc::error_argument);
                return;
            }

            ctx->complete(epoc::error_none);
            break;

        case agn_start_build_index:
        case agn_request_progress:
        case agn_tidy_by_date_start:
        case agn_category_start_async:
            ctx->complete(epoc::error_none);
            break;

        case agn_request_change_notification:
            if (change_request_) {
                change_request_->complete(epoc::error_cancel);
            }
            change_request_ = ctx->move_to_new();
            break;

        case agn_cancel_change_notification:
            if (change_request_) {
                change_request_->complete(epoc::error_cancel);
                change_request_.reset();
            }
            ctx->complete(epoc::error_none);
            break;

        case agn_get_list_file_names:
            if (!write_empty_file_list(ctx, transmit_buffer_)) {
                ctx->complete(epoc::error_argument);
                return;
            }
            ctx->complete(epoc::error_none);
            break;

        case agn_previous_instances:
        case agn_next_instances:
        case agn_get_entry_uids_since_date:
        case agn_get_changes_since_last_notification:
        case agn_find_instances:
        case agn_fetch_simple_entries_by_guid:
        case agn_get_file_changes_since_last_notification:
        case agn_instance_iterator_next:
        case agn_instance_iterator_previous:
            if (!write_empty_stream(ctx, transmit_buffer_, 0, 1)) {
                ctx->complete(epoc::error_argument);
                return;
            }
            ctx->complete(epoc::error_none);
            break;

        case agn_create_entry_iterator:
            entry_iterator_has_current_ = true;
            if (!try_write_descriptor(ctx, 1, TRUE_VALUE)) {
                ctx->complete(epoc::error_argument);
                return;
            }
            ctx->complete(epoc::error_none);
            break;

        case agn_entry_iterator_next:
            entry_iterator_has_current_ = false;
            if (!try_write_descriptor(ctx, 1, FALSE_VALUE)) {
                ctx->complete(epoc::error_argument);
                return;
            }
            ctx->complete(epoc::error_none);
            break;

        case agn_agenda_file_exists:
            if (!write_descriptor(ctx, 0, TRUE_VALUE)) {
                ctx->complete(epoc::error_argument);
                return;
            }
            ctx->complete(epoc::error_none);
            break;

        case agn_get_category_list_count:
        case agn_resource_count:
        case agn_heap_size_count:
        case agn_instance_iterator_count:
        case agn_instance_iterator_locate_index:
            if (!write_descriptor(ctx, (ctx->msg->function == agn_instance_iterator_count) ? 1 : 0, EMPTY_COUNT)) {
                ctx->complete(epoc::error_argument);
                return;
            }
            ctx->complete(epoc::error_none);
            break;

        case agn_instance_iterator_create:
            if (!write_descriptor(ctx, 2, static_cast<std::int32_t>(epoc::error_not_found))) {
                ctx->complete(epoc::error_argument);
                return;
            }
            ctx->complete(epoc::error_none);
            break;

        case agn_tz_db_changed_time:
            if (!write_descriptor(ctx, 0, ZERO_TIME)) {
                ctx->complete(epoc::error_argument);
                return;
            }
            ctx->complete(epoc::error_none);
            break;

        case agn_cancel_task:
        case agn_set_heap_failure:
        case agn_disable_change_broadcast:
        case agn_enable_change_broadcast:
        case agn_request_change_notification_parameters:
        case agn_set_update_alarm:
        case agn_set_enable_pub_sub_notification:
        case agn_restore_alarm_action:
        case agn_category_filter:
        case agn_update_entry:
        case agn_add_entry:
        case agn_delete_entry:
        case agn_add_category_to_list:
        case agn_delete_agenda_file:
        case agn_tidy_by_date_read_params:
        case agn_category_start:
        case agn_create_agenda_file:
        case agn_delete_entries_by_local_uid:
        case agn_delete_entry_by_guid:
        case agn_commit:
        case agn_rollback:
        case agn_set_calendar_info:
        case agn_instance_iterator_destroy:
            ctx->complete(epoc::error_none);
            break;

        case agn_fetch_entry:
        case agn_fetch_entry_by_uid:
        case agn_fetch_simple_entry:
        case agn_restore_text:
        case agn_get_category_list_item:
        case agn_fetch_entry_by_guid:
        case agn_transfer_attachment_file_to_client:
        case agn_fetch_sorted_attachments:
        case agn_entries_with_attachment:
        case agn_fetch_attachment_by_id:
        case agn_fetch_simple_entry_by_uid:
        case agn_get_calendar_info:
        case agn_get_property_value:
        case agn_get_instance_extractor:
        case agn_transfer_attachment_file_to_server:
        case agn_transfer_file_to_client_to_write:
        case agn_move_file_to_server:
            ctx->complete(epoc::error_not_found);
            break;

        case agn_entry_iterator_position:
            if (!entry_iterator_has_current_) {
                if (!write_descriptor(ctx, 1, EMPTY_COUNT)) {
                    ctx->complete(epoc::error_argument);
                    return;
                }

                ctx->complete(epoc::error_none);
                break;
            }

            if (!write_synthetic_calendar_entry(ctx, transmit_buffer_)) {
                ctx->complete(epoc::error_argument);
                return;
            }

            ctx->complete(epoc::error_none);
            break;

        default:
            LOG_WARN(SERVICE_TRACK, "!AgendaServer opcode 0x{:X} ({}) is not implemented, returning KErrNotSupported",
                ctx->msg->function, agenda_opcode_name(ctx->msg->function));
            ctx->complete(epoc::error_not_supported);
            break;
        }
    }
}
