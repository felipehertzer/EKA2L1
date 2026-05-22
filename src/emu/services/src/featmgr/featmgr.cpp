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

#include <common/log.h>

#include <kernel/kernel.h>
#include <services/context.h>
#include <services/featmgr/featmgr.h>
#include <services/featmgr/op.h>
#include <system/epoc.h>
#include <utils/des.h>
#include <utils/err.h>
#include <vfs/vfs.h>

#include <algorithm>
#include <cstring>

namespace eka2l1 {
    namespace {
        constexpr std::uint32_t feature_flag_supported = 1;

        bool is_descriptor_argument(service::ipc_context &ctx, const int idx) {
            if (idx < 0 || idx >= 4) {
                return false;
            }

            return ctx.sys->get_kernel_system()->is_eka1()
                || (static_cast<int>(ctx.msg->args.get_arg_type(idx)) & static_cast<int>(ipc_arg_type::flag_des));
        }

        bool write_int_result(service::ipc_context &ctx, const int idx, const std::int32_t value) {
            if (ctx.write_arg(idx, static_cast<std::uint32_t>(value))) {
                return true;
            }

            return ctx.write_data_to_descriptor_argument<std::int32_t>(idx, value, nullptr, true);
        }

        std::optional<epoc::uid> read_feature_uid(service::ipc_context &ctx, const int idx) {
            if (is_descriptor_argument(ctx, idx)) {
                const std::uint8_t *data = ctx.get_descriptor_argument_ptr(idx);
                if (!data || ctx.get_argument_data_size(idx) < sizeof(epoc::uid)) {
                    return std::nullopt;
                }

                epoc::uid feature_id = 0;
                std::memcpy(&feature_id, data, sizeof(feature_id));
                return feature_id;
            }

            return ctx.get_argument_value<epoc::uid>(idx);
        }

        std::uint32_t feature_count_from_request(service::ipc_context &ctx, const int count_idx, const std::size_t entry_size, const std::size_t data_size) {
            const std::optional<std::uint32_t> requested_count = ctx.get_argument_value<std::uint32_t>(count_idx);
            const std::uint32_t descriptor_count = static_cast<std::uint32_t>(data_size / entry_size);

            if (!requested_count || *requested_count == 0) {
                return descriptor_count;
            }

            return std::min(*requested_count, descriptor_count);
        }

        bool request_matches_element_size(service::ipc_context &ctx, const int count_idx, const std::size_t element_size, const std::size_t data_size) {
            const std::optional<std::uint32_t> requested_count = ctx.get_argument_value<std::uint32_t>(count_idx);
            return requested_count && (*requested_count > 0) && (data_size >= (*requested_count * element_size));
        }
    }

    featmgr_server::featmgr_server(system *sys)
        : service::server(sys->get_kernel_system(), sys, nullptr, "!FeatMgrServer", true) {
        REGISTER_IPC(featmgr_server, feature_supported, EFeatMgrFeatureSupported, "FeatMgr::FeatureSupported");
        REGISTER_IPC(featmgr_server, features_supported, EFeatMgrFeaturesSupported, "FeatMgr::FeaturesSupported");
        REGISTER_IPC(featmgr_server, list_supported_features, EFeatMgrListSupportedFeatures, "FeatMgr::ListSupportedFeatures");
        REGISTER_IPC(featmgr_server, number_of_supported_features, EFeatMgrNumberOfSupportedFeatures, "FeatMgr::NumberOfSupportedFeatures");
        REGISTER_IPC(featmgr_server, notify_stub, EFeatMgrReqNotify, "FeatMgr::ReqNotify");
        REGISTER_IPC(featmgr_server, notify_stub, EFeatMgrReqNotifyUids, "FeatMgr::ReqNotifyUids");
        REGISTER_IPC(featmgr_server, notify_stub, EFeatMgrReqNotifyCancel, "FeatMgr::ReqNotifyCancel");
        REGISTER_IPC(featmgr_server, notify_stub, EFeatMgrReqNotifyCancelAll, "FeatMgr::ReqNotifyCancelAll");
        REGISTER_IPC(featmgr_server, modify_feature, EFeatMgrEnableFeature, "FeatMgr::EnableFeature");
        REGISTER_IPC(featmgr_server, modify_feature, EFeatMgrDisableFeature, "FeatMgr::DisableFeature");
        REGISTER_IPC(featmgr_server, modify_feature, EFeatMgrAddFeature, "FeatMgr::AddFeature");
        REGISTER_IPC(featmgr_server, modify_feature, EFeatMgrSetFeatureAndData, "FeatMgr::SetFeatureAndData");
        REGISTER_IPC(featmgr_server, modify_feature, EFeatMgrSetFeatureData, "FeatMgr::SetFeatureData");
        REGISTER_IPC(featmgr_server, modify_feature, EFeatMgrDeleteFeature, "FeatMgr::DeleteFeature");
        REGISTER_IPC(featmgr_server, resource_stub, EFeatMgrSWIStart, "FeatMgr::SWIStart");
        REGISTER_IPC(featmgr_server, resource_stub, EFeatMgrSWIEnd, "FeatMgr::SWIEnd");
        REGISTER_IPC(featmgr_server, resource_stub, EFeatMgrResourceMark, "FeatMgr::ResourceMark");
        REGISTER_IPC(featmgr_server, resource_stub, EFeatMgrResourceCheck, "FeatMgr::ResourceCheck");
        REGISTER_IPC(featmgr_server, resource_stub, EFeatMgrResourceCount, "FeatMgr::ResourceCount");
        REGISTER_IPC(featmgr_server, resource_stub, EFeatMgrSetHeapFailure, "FeatMgr::SetHeapFailure");
    }

    enum feature_id : epoc::uid {
        feature_id_opengl_es_3d_api = 10,
        feature_id_svgt = 77,
        feature_id_side_volume_key = 207,
        feature_id_pen = 410,
        feature_id_vibra = 411,
        feature_id_korean = 180,
        feature_id_japanese = 1080,
        feature_id_thai = 1081,
        feature_id_chinese = 1096,
        feature_id_flash_lite_viewer = 1145,
        feature_id_pen_calibration = 1658,
        feature_id_tactile_feedback = 1718
    };

    void featmgr_server::do_feature_scanning(system *sys) {
        // TODO: There is a lot of features.
        // See in here: https://github.com/SymbianSource/oss.FCL.sf.os.deviceplatformrelease/blob/master/foundation_system/sf_config/config/inc/publicruntimeids.hrh

        // 1. We expose the guest 3D API and Flash feature flags when the emulator can service them.
        enable_features.push_back(feature_id_opengl_es_3d_api);
        enable_features.push_back(feature_id_flash_lite_viewer);
        enable_features.push_back(feature_id_side_volume_key);
        enable_features.push_back(feature_id_pen);
        enable_features.push_back(feature_id_vibra);
        enable_features.push_back(feature_id_pen_calibration);

        // 2. Are we welcoming SVG? Check for OpenVG, cause it should be there if this feature is available
        if (sys->get_io_system()->exist(u"z:\\sys\\bin\\libopenvg.dll")) {
            enable_features.push_back(feature_id_svgt);
        }

        // 3. Check for system language. User have responsibility to be honest :D
        // I like automatic detection, but it's not really easy thogh
        switch (sys->get_system_language()) {
        case language::zh: {
            enable_features.push_back(feature_id_chinese);
            break;
        }

        case language::jp: {
            enable_features.push_back(feature_id_japanese);
            break;
        }

        case language::ko: {
            enable_features.push_back(feature_id_korean);
            break;
        }

        case language::th: {
            enable_features.push_back(feature_id_thai);
            break;
        }

        default:
            break;
        }

        // 4: Add stuff you like here.
    }

    bool featmgr_server::load_featmgr_configs(io_system *io) {
        symfile cfg_file = io->open_file(u"Z:\\private\\102744CA\\featreg.cfg", READ_MODE | BIN_MODE);

        if (!cfg_file) {
            LOG_WARN(SERVICE_FEATMGR, "Feature registration config file not present!");
            return false;
        }

        featmgr_config_header header;
        cfg_file->read_file(&header, 1, sizeof(featmgr_config_header));

        // Check magic header
        if (strncmp(header.magic, "feat", 4) != 0) {
            return false;
        }

        {
            featmgr_config_entry temp_entry;

            for (uint32_t i = 0; i < header.num_entry; i++) {
                cfg_file->read_file(&temp_entry, 1, sizeof(featmgr_config_entry));

                if (temp_entry.info > 0) {
                    enable_features.push_back(temp_entry.uid);
                }
            }
        }

        {
            featmgr_config_range temp_range;

            for (uint32_t i = 0; i < header.num_range; i++) {
                cfg_file->read_file(&temp_range, 1, sizeof(featmgr_config_range));
                enable_feature_ranges.push_back(temp_range);
            }
        }

        return true;
    }

    void featmgr_server::ensure_features_loaded(system *sys) {
        if (!config_loaded) {
            const bool succ = load_featmgr_configs(sys->get_io_system());

            if (!succ) {
                LOG_INFO(SERVICE_FEATMGR, "Using scanned Feature Manager defaults");
            }

            do_feature_scanning(sys);
            std::sort(enable_features.begin(), enable_features.end());
            enable_features.erase(std::unique(enable_features.begin(), enable_features.end()), enable_features.end());

            config_loaded = true;
        }
    }

    bool featmgr_server::is_feature_supported(epoc::uid feature_id) const {
        if (std::binary_search(enable_features.begin(), enable_features.end(), feature_id)) {
            return true;
        }

        for (const auto &feature_range : enable_feature_ranges) {
            if (feature_range.low_uid <= feature_id && feature_id <= feature_range.high_uid) {
                return true;
            }
        }

        return false;
    }

    void featmgr_server::feature_supported(service::ipc_context &ctx) {
        ensure_features_loaded(ctx.sys);

        const std::optional<epoc::uid> feature_id = read_feature_uid(ctx, 0);

        if (!feature_id) {
            ctx.complete(epoc::error_argument);
            return;
        }

        const int result = is_feature_supported(*feature_id) ? 1 : 0;
        LOG_TRACE(SERVICE_FEATMGR, "FeatureSupported uid=0x{:X}, result={}", *feature_id, result);
        write_int_result(ctx, 1, result);
        ctx.complete(epoc::error_none);
    }

    void featmgr_server::features_supported(service::ipc_context &ctx) {
        ensure_features_loaded(ctx.sys);

        std::uint8_t *data = ctx.get_descriptor_argument_ptr(1);
        if (!data) {
            ctx.complete(epoc::error_argument);
            return;
        }

        const std::size_t data_size = ctx.get_argument_data_size(1);
        std::uint32_t supported_count = 0;

        const bool uid_request = request_matches_element_size(ctx, 0, sizeof(epoc::uid), data_size);
        const bool feature_entry_request = request_matches_element_size(ctx, 0, sizeof(feature_entry), data_size);

        if (feature_entry_request || (!uid_request && (data_size >= sizeof(feature_entry)) && (data_size % sizeof(feature_entry) == 0))) {
            const std::uint32_t entry_count = feature_count_from_request(ctx, 0, sizeof(feature_entry), data_size);
            auto *entries = reinterpret_cast<feature_entry *>(data);

            for (std::uint32_t i = 0; i < entry_count; i++) {
                const bool supported = is_feature_supported(entries[i].feature_id_);
                LOG_TRACE(SERVICE_FEATMGR, "FeaturesSupported entry uid=0x{:X}, result={}", entries[i].feature_id_, supported ? 1 : 0);
                entries[i].flags_ = supported ? (entries[i].flags_ | feature_flag_supported) : (entries[i].flags_ & ~feature_flag_supported);
                entries[i].data_ = 0;
                entries[i].reserved_ = 0;

                if (supported) {
                    supported_count++;
                }
            }
        } else if ((data_size >= sizeof(epoc::uid)) && (data_size % sizeof(epoc::uid) == 0)) {
            const std::uint32_t uid_count = feature_count_from_request(ctx, 0, sizeof(epoc::uid), data_size);
            auto *uids = reinterpret_cast<epoc::uid *>(data);

            for (std::uint32_t i = 0; i < uid_count; i++) {
                if (is_feature_supported(uids[i])) {
                    supported_count++;
                }
                LOG_TRACE(SERVICE_FEATMGR, "FeaturesSupported uid=0x{:X}, result={}", uids[i], is_feature_supported(uids[i]) ? 1 : 0);
            }
        } else {
            ctx.complete(epoc::error_argument);
            return;
        }

        write_int_result(ctx, 2, static_cast<std::int32_t>(supported_count));
        ctx.complete(epoc::error_none);
    }

    void featmgr_server::list_supported_features(service::ipc_context &ctx) {
        ensure_features_loaded(ctx.sys);

        const std::uint32_t byte_count = static_cast<std::uint32_t>(enable_features.size() * sizeof(epoc::uid));
        int write_error = 0;

        if (!ctx.write_data_to_descriptor_argument(0, reinterpret_cast<const std::uint8_t *>(enable_features.data()), byte_count, &write_error)) {
            ctx.complete(write_error == -2 ? epoc::error_overflow : epoc::error_argument);
            return;
        }

        ctx.complete(epoc::error_none);
    }

    void featmgr_server::number_of_supported_features(service::ipc_context &ctx) {
        ensure_features_loaded(ctx.sys);

        const std::int32_t count = static_cast<std::int32_t>(enable_features.size());
        write_int_result(ctx, 0, count);
        ctx.complete(count);
    }

    void featmgr_server::notify_stub(service::ipc_context &ctx) {
        if (ctx.msg->function == EFeatMgrReqNotify || ctx.msg->function == EFeatMgrReqNotifyUids) {
            write_int_result(ctx, 2, epoc::error_not_supported);
            ctx.complete(epoc::error_not_supported);
            return;
        }

        write_int_result(ctx, 0, epoc::error_none);
        ctx.complete(epoc::error_none);
    }

    void featmgr_server::modify_feature(service::ipc_context &ctx) {
        ensure_features_loaded(ctx.sys);

        std::optional<epoc::uid> feature_id;
        bool enable = true;

        switch (ctx.msg->function) {
        case EFeatMgrAddFeature: {
            std::optional<feature_entry> entry = ctx.get_argument_data_from_descriptor<feature_entry>(0, true);
            if (!entry) {
                ctx.complete(epoc::error_argument);
                return;
            }

            feature_id = entry->feature_id_;
            enable = (entry->flags_ & feature_flag_supported) != 0;
            break;
        }

        case EFeatMgrSetFeatureAndData: {
            feature_id = read_feature_uid(ctx, 0);
            enable = ctx.get_argument_value<std::uint32_t>(1).value_or(0) != 0;
            break;
        }

        case EFeatMgrDisableFeature:
        case EFeatMgrDeleteFeature:
            feature_id = read_feature_uid(ctx, 0);
            enable = false;
            break;

        case EFeatMgrSetFeatureData:
            feature_id = read_feature_uid(ctx, 0);
            write_int_result(ctx, 2, epoc::error_none);
            ctx.complete(epoc::error_none);
            return;

        case EFeatMgrEnableFeature:
        default:
            feature_id = read_feature_uid(ctx, 0);
            enable = true;
            break;
        }

        if (!feature_id) {
            ctx.complete(epoc::error_argument);
            return;
        }

        auto existing = std::lower_bound(enable_features.begin(), enable_features.end(), *feature_id);
        if (enable) {
            if (existing == enable_features.end() || *existing != *feature_id) {
                enable_features.insert(existing, *feature_id);
            }
        } else if (existing != enable_features.end() && *existing == *feature_id) {
            enable_features.erase(existing);
        }

        const int result_arg = (ctx.msg->function == EFeatMgrSetFeatureAndData) ? 3 : 1;
        write_int_result(ctx, result_arg, epoc::error_none);
        ctx.complete(epoc::error_none);
    }

    void featmgr_server::resource_stub(service::ipc_context &ctx) {
        if (ctx.msg->function == EFeatMgrResourceCount) {
            ctx.complete(0);
            return;
        }

        ctx.complete(epoc::error_none);
    }
}
