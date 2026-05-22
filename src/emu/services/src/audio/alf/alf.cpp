/*
 * Copyright (c) 2022 EKA2L1 Team
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

#include <services/audio/alf/alf.h>
#include <system/epoc.h>

#include <common/log.h>
#include <utils/err.h>

#include <cstdint>
#include <optional>

namespace eka2l1 {
    namespace {
        enum alfred_server_ipc {
            EAlfEnvSetRefreshMode = 0,
            EAlfEnvSetMaxFrameRate,
            EAlfEnvContinueRefresh,
            EAlfEnvPauseRefresh,
            EAlfEnvRefreshCallBack,
            EAlfEnvSetIdleThreshold,
            EAlfEnvRenderer,
            EAlfGetPointerEvent,
            EAlfCancelPtrEvents,
            ESetFullScreenDrawing,
            EAlfGetSystemEvent,
            EAlfCancelSystemEvents,
            EAlfNotifyAppVisibility,
            EAlfDisplaySubSessionOpen,
            EAlfControlGroupSubSessionOpen,
            EAlfTransformationSubSessionOpen,
            EAlfCreateSubSession,
            EAlfCloseSubSession,
            EAlfDoSubSessionCmd,
            EAlfDoAsyncSubSessionCmd,
            EAlfSubSCancelAsynchRequest,
            EAlfSetWgParent,
            EAlfSBufAddObserver,
            EAlfSBufRemoveObserver,
            EAlfSBufRequestEvent,
            EAlfSBufRequestNextBuffer,
            EAlfDisplaySubSessionOpen2,
            EAlfDoSubSessionBatchCmd,
            EAlfConfigureBatchCmd,
            EAlfNotifyTextureInfo,
            EAlfCancelNotifyTextureInfo,
            EAlfSBufRequestBufferDraw,

            EAlfCntrlCreate = 150,

            EAlfVisualCreate = 200,
            EAlfQtCommandBuffer = 450,
            EAlfLCTTextVisualCreate = 500,
            EAlfLineVisualCreate = 600,
            EAlfImageVisualCreate = 700,
            EAlfMeshVisualCreate = 800,
            EAlfCanvasVisualCreate = 900,
            EAlfLayoutCreate = 1000,
            EAlfAnchorLayoutCreate = 1100,
            EAlfLCTAnchorLayoutCreate = 1200,
            EAlfGridLayoutCreate = 1300,
            EAlfLCTGridLayoutCreate = 1400,
            EAlfDeckLayoutCreate = 1500,
            EAlfFlowLayoutCreate = 1600,
            EAlfCurvePathLayoutCreate = 1700,
            EAlfViewportLayoutCreate = 1800,
            EAlfRosterShow = 2000,
            EAlfTextureCreateAnimated = 2100,
            EAlfTextureStopAnimation,
            EAlfTextureStartAnimation,
            EAlfTextureCreate,
            EAlfTextureLoad,
            EAlfTextureUnload,
            EAlfTextureBlur,
            EAlfTextureHasContent,
            EAlfTextureDelete,
            EAlfTextureRelease,
            EAlfTextureRestore,
            EAlfTextureNotifySkinChanged,
            EAlfTextureUpdateOwnerId,
            EAlfTextureSetAutoSizeParams,
            EAlfTextureCleanAnimation,
            EAlfDisplaySetClearBackground = 2200,
            EAlfControlGroupAppend = 2300,
            EAlfBrushSetOpacity = 2500,
            EAlfImageBrushCreate = 2600,
            EAlfShadowBorderBrushCreate = 2700,
            EAlfDropShadowBrushCreate = 2800,
            EAlfGradientBrushCreate = 2900,
            EAlfFrameBrushCreate = 3000,
            EAlfTransformationLoadIdentity = 3300,
            EAlfMappingFunctionAverageCreate = 3500,
            EAlfCurvePathCreate = 3700,
            EAlfPlatformTextStyleCreate = 3900,
            EAlfKnownVisualOpcodeEnd = 4100
        };

        constexpr int EHuiRendererBitgdi = 1;

        struct alf_implementation_information {
            std::int32_t implementation_uid;
            std::int32_t implementation_id;
            epoc::version version;
        };

        struct alf_subsession : epoc::ref_count_object {
            std::int32_t implementation_uid;
            std::int32_t implementation_id;

            explicit alf_subsession(const alf_implementation_information &implementation)
                : implementation_uid(implementation.implementation_uid)
                , implementation_id(implementation.implementation_id) {
            }
        };

        constexpr int EAlfDecodSLoadPlugin = 20000;
        constexpr int EAlfDecodSUnloadPlugin = 20001;
        constexpr int EAlfDecodSSendSynch = 20002;
        constexpr int EAlfDecodSSendAsynch = 20003;
        constexpr int EAlfDecodSCancelAsynch = 20004;
        constexpr int EAlfDecodSPrepareFrame = 20005;
        constexpr int EAlfBridgerAsyncronousData = 20006;
        constexpr int EAlfBridgerSendChunk = 20007;
        constexpr int EAlfBridgerSendSyncData = 20008;
        constexpr int EAlfBridgerRequestDataBlock = 20009;
        constexpr int EDsNotifyNativeWindowData = 20010;
        constexpr int EAlfBridgerBlindSend = 20011;
        constexpr int EAlfSetScreenRotation = 20012;
        constexpr int EAlfGetNativeWindowHandles = 20013;
        constexpr int EAlfSynchronize = 20014;
        constexpr int EAlfPostDataToCompositionClient = 20015;
        constexpr int EAlfPostDataToCompositionTarget = 20016;
        constexpr int EAlfGetListOfWGsHavingInactiveSurfaces = 20017;
        constexpr int EAlfQueueRequestBGSessions = 20018;
        constexpr int EAlfGetNumberOfActiveEffects = 20019;
        constexpr int EAlfRequestSignal = 20020;
        constexpr int EAlfCompleteSignal = 20021;
        constexpr int EAlfSetDistractionWindow = 20022;
        constexpr int EAlfVolunteerForGoomTarget = 20023;
        constexpr int EAlfExcludeFromGoomTargets = 20024;
        constexpr int KAlfFlushDrawCommands = 20025;
        constexpr int EAlfSuppressVisibilityChanges = 20026;

        constexpr int KAlfCompOpCreateSource = 15000;
        constexpr int KAlfCompOpCreateToken = 15001;
        constexpr int KAlfCompOpBindSourceToToken = 15002;
        constexpr int KAlfCompOpEnableAlpha = 15003;
        constexpr int KAlfCompOpSetOpacity = 15004;
        constexpr int KAlfCompOpSetRotation = 15005;
        constexpr int KAlfCompOpSetZOrder = 15006;
        constexpr int KAlfCompOpSetExtent = 15007;
        constexpr int KAlfCompOpEnableKb = 15008;
        constexpr int KAlfCompOpRequestEvent = 15009;
        constexpr int KAlfCompOpCancelEventRequest = 15010;
        constexpr int KAlfComOpSetBackgroundAnim = 15011;
        constexpr int KAlfCompOpSessionClosed = 15012;
        constexpr int KAlfCompOpSetSRect = 15013;

        constexpr int KAlfCompositionFrameReady = 16000;
        constexpr int KAlfCompositionLowOnGraphicsMemory = 16001;
        constexpr int KAlfCompositionTargetHidden = 16002;
        constexpr int KAlfCompositionTargetCreated = 16003;
        constexpr int KAlfCompositionWServReady = 16004;
        constexpr int KAlfCompositionWServScreenNumber = 16005;
        constexpr int KAlfCompositionSourceScreenNumber = 16006;
        constexpr int KAlfCompositionGoodOnGraphicsMemory = 16007;
        constexpr int KAlfCompositionTargetVisible = 16008;
        constexpr int KAlfCompositionTargetHiddenBGAnim = 16009;
        constexpr int KAlfCompositionTargetVisibleBGAnim = 16010;
        constexpr int KAlfCompositionLayoutSwitchComplete = 16011;
        constexpr int KAlfCompositionFlushDrawCommands = 16012;

        struct alf_native_window_data {
            std::int32_t screen_number;
            std::int32_t alf_window_group_id;
            std::uint32_t alf_window_handle;
        };

        int next_alf_composition_handle = 1;

        int allocate_alf_composition_handle() {
            return next_alf_composition_handle++;
        }
    }

    void alf_app_session::pending_alf_event::request(service::ipc_context *ctx) {
        if (context_) {
            context_->complete(epoc::error_in_use);
        }

        context_ = ctx->move_to_new();
    }

    void alf_app_session::pending_alf_event::cancel() {
        if (context_) {
            context_->complete(epoc::error_cancel);
            context_.reset();
        }
    }

    alf_app_server::alf_app_server(eka2l1::system *sys, const std::uint32_t server_differentiator)
        : app_ui_based_server(sys, server_differentiator, alf_streamer_server::app_uid) {
    }

    void alf_app_server::connect(service::ipc_context &ctx) {
        const std::optional<epoc::version> client_version = get_version(&ctx);
        if (client_version && (client_version->u32 == alf_streamer_server::service_uid)) {
            create_session<alf_app_session>(&ctx);
            typical_server::connect(ctx);
            return;
        }

        app_ui_based_server::connect(ctx);
    }

    alf_app_session::alf_app_session(service::typical_server *serv, const kernel::uid ss_id, epoc::version client_version)
        : app_ui_based_session(serv, ss_id, client_version)
        , refresh_mode_(1)
        , idle_threshold_(0)
        , parent_window_group_id_(0)
        , texture_owner_process_id_(0)
        , visible_(false) {
    }

    alf_app_session::~alf_app_session() {
        pointer_event_.cancel();
        system_event_.cancel();
        texture_info_event_.cancel();
        screen_buffer_event_.cancel();
    }

    bool alf_app_session::write_int_result(service::ipc_context *ctx, const int argument_index, const int value) {
        int error_code = 0;
        if (ctx->write_data_to_descriptor_argument(argument_index, value, &error_code)) {
            return true;
        }

        if ((error_code == -1) && ctx->write_arg(argument_index, static_cast<std::uint32_t>(value))) {
            return true;
        }

        return false;
    }

    void alf_app_session::handle_create_subsession(service::ipc_context *ctx) {
        std::optional<alf_implementation_information> implementation =
            ctx->get_argument_data_from_descriptor<alf_implementation_information>(0, true);

        if (!implementation) {
            implementation = alf_implementation_information{};
        }

        alf_subsession *subsession = make_new<alf_subsession>(*implementation);
        if (!subsession) {
            ctx->complete(epoc::error_no_memory);
            return;
        }

        if (!ctx->write_handle_argument(3, subsession->id)) {
            server<service::typical_server>()->remove(subsession);
            ctx->complete(epoc::error_bad_descriptor);
            return;
        }

        LOG_TRACE(SERVICE_UI, "Created ALF subsession handle 0x{:X}, implementation=0x{:X}:0x{:X}",
            subsession->id, implementation->implementation_uid, implementation->implementation_id);
        ctx->complete(epoc::error_none);
    }

    void alf_app_session::handle_close_subsession(service::ipc_context *ctx) {
        const std::uint32_t handle = ctx->msg->args.args[3];
        alf_subsession *subsession = server<service::typical_server>()->get<alf_subsession>(handle);
        if (!subsession) {
            ctx->complete(epoc::error_bad_handle);
            return;
        }

        server<service::typical_server>()->remove(subsession);
        ctx->complete(epoc::error_none);
    }

    void alf_app_session::handle_subsession_command(service::ipc_context *ctx) {
        const int opcode = ctx->msg->function;
        const std::uint32_t handle = ctx->msg->args.args[3];

        if (!server<service::typical_server>()->get<alf_subsession>(handle)) {
            ctx->complete(epoc::error_bad_handle);
            return;
        }

        if (opcode == EAlfSubSCancelAsynchRequest) {
            ctx->complete(epoc::error_none);
            return;
        }

        LOG_TRACE(SERVICE_UI, "ALF subsession command 0x{:X} acknowledged for handle 0x{:X}",
            ctx->msg->args.args[0], handle);
        ctx->complete(epoc::error_none);
    }

    void alf_app_session::handle_texture_command(service::ipc_context *ctx) {
        switch (ctx->msg->function) {
        case EAlfTextureUpdateOwnerId:
            texture_owner_process_id_ = static_cast<int>(ctx->msg->own_thr->owning_process()->unique_id());
            ctx->complete(epoc::error_none);
            return;

        case EAlfTextureCreateAnimated:
        case EAlfTextureCreate:
        case EAlfTextureLoad: {
            if (texture_owner_process_id_ == 0) {
                ctx->complete(epoc::error_not_ready);
                return;
            }

            const int texture_handle = static_cast<int>(allocate_alf_composition_handle());
            if (!write_int_result(ctx, 0, texture_handle)) {
                ctx->complete(epoc::error_bad_descriptor);
                return;
            }

            ctx->complete(epoc::error_none);
            return;
        }

        case EAlfTextureHasContent:
            if (!write_int_result(ctx, 0, 0)) {
                ctx->complete(epoc::error_bad_descriptor);
                return;
            }

            ctx->complete(epoc::error_none);
            return;

        case EAlfTextureStopAnimation:
        case EAlfTextureStartAnimation:
        case EAlfTextureUnload:
        case EAlfTextureBlur:
        case EAlfTextureDelete:
        case EAlfTextureRelease:
        case EAlfTextureRestore:
        case EAlfTextureNotifySkinChanged:
        case EAlfTextureSetAutoSizeParams:
        case EAlfTextureCleanAnimation:
            ctx->complete(epoc::error_none);
            return;

        default:
            break;
        }
    }

    void alf_app_session::fetch(service::ipc_context *ctx) {
        switch (ctx->msg->function) {
        case EAlfEnvSetRefreshMode:
            refresh_mode_ = ctx->msg->args.args[0];
            ctx->complete(epoc::error_none);
            return;

        case EAlfEnvSetMaxFrameRate:
        case EAlfEnvContinueRefresh:
        case EAlfEnvPauseRefresh:
        case EAlfEnvRefreshCallBack:
            ctx->complete(epoc::error_none);
            return;

        case EAlfEnvSetIdleThreshold:
            idle_threshold_ = ctx->msg->args.args[0];
            ctx->complete(epoc::error_none);
            return;

        case EAlfEnvRenderer:
            if (!write_int_result(ctx, 0, EHuiRendererBitgdi)) {
                ctx->complete(epoc::error_bad_descriptor);
                return;
            }

            ctx->complete(epoc::error_none);
            return;

        case EAlfGetPointerEvent:
            pointer_event_.request(ctx);
            return;

        case EAlfCancelPtrEvents:
            pointer_event_.cancel();
            ctx->complete(epoc::error_none);
            return;

        case EAlfGetSystemEvent:
            system_event_.request(ctx);
            return;

        case EAlfCancelSystemEvents:
            system_event_.cancel();
            ctx->complete(epoc::error_none);
            return;

        case EAlfNotifyAppVisibility:
            visible_ = ctx->msg->args.args[0] != 0;
            ctx->complete(epoc::error_none);
            return;

        case EAlfCreateSubSession:
            handle_create_subsession(ctx);
            return;

        case EAlfCloseSubSession:
            handle_close_subsession(ctx);
            return;

        case EAlfDoSubSessionCmd:
        case EAlfDoAsyncSubSessionCmd:
        case EAlfSubSCancelAsynchRequest:
            handle_subsession_command(ctx);
            return;

        case EAlfSetWgParent:
            parent_window_group_id_ = ctx->msg->args.args[0];
            ctx->complete(epoc::error_none);
            return;

        case EAlfNotifyTextureInfo:
            texture_info_event_.request(ctx);
            return;

        case EAlfCancelNotifyTextureInfo:
            texture_info_event_.cancel();
            ctx->complete(epoc::error_none);
            return;

        case EAlfSBufRequestEvent:
            screen_buffer_event_.request(ctx);
            return;

        case EAlfSBufRemoveObserver:
        case EAlfSBufRequestBufferDraw:
            screen_buffer_event_.cancel();
            ctx->complete(epoc::error_none);
            return;

        default:
            break;
        }

        if ((ctx->msg->function >= EAlfTextureCreateAnimated) && (ctx->msg->function <= EAlfTextureCleanAnimation)) {
            handle_texture_command(ctx);
            return;
        }

        if ((ctx->msg->function >= EAlfCntrlCreate) && (ctx->msg->function < EAlfKnownVisualOpcodeEnd)) {
            LOG_TRACE(SERVICE_UI, "ALF app opcode 0x{:X} acknowledged without a native Hui renderer", ctx->msg->function);
            ctx->complete(epoc::error_none);
            return;
        }

        LOG_WARN(SERVICE_UI, "Unimplemented ALF app session opcode 0x{:X}", ctx->msg->function);
        ctx->complete(epoc::error_not_supported);
    }

    alf_streamer_server::alf_streamer_server(eka2l1::system *sys)
        : service::typical_server(sys, server_name) {
    }

    void alf_streamer_server::connect(service::ipc_context &context) {
        create_session<alf_streamer_session>(&context);
        context.complete(epoc::error_none);
    }

    alf_streamer_session::alf_streamer_session(service::typical_server *serv, const kernel::uid ss_id,
        epoc::version client_version)
        : service::typical_session(serv, ss_id, client_version)
        , screen_number_(0)
        , composition_source_handle_(0) {
    }

    void alf_streamer_session::complete_native_window_handles(service::ipc_context *ctx) {
        const alf_native_window_data data = {
            screen_number_,
            0,
            0
        };

        int error_code = epoc::error_none;
        if (!ctx->write_data_to_descriptor_argument(0, data, &error_code)) {
            ctx->complete(error_code);
            return;
        }

        ctx->complete(epoc::error_none);
    }

    void alf_streamer_session::handle_composition_op(service::ipc_context *ctx, const int opcode) {
        switch (opcode) {
        case KAlfCompOpCreateSource:
            if (!composition_source_handle_) {
                composition_source_handle_ = allocate_alf_composition_handle();
            }

            ctx->complete(composition_source_handle_);
            break;

        case KAlfCompOpCreateToken:
            ctx->complete(allocate_alf_composition_handle());
            break;

        case KAlfCompositionWServScreenNumber:
            screen_number_ = ctx->msg->args.args[0];
            ctx->complete(epoc::error_none);
            break;

        case KAlfCompositionSourceScreenNumber: {
            auto data = ctx->get_argument_data_from_descriptor<std::int32_t>(0, true);
            if (!data) {
                ctx->complete(epoc::error_argument);
                return;
            }

            screen_number_ = *data;
            ctx->complete(epoc::error_none);
            break;
        }

        case KAlfCompOpRequestEvent:
            ctx->complete(epoc::error_not_found);
            break;

        case KAlfCompOpCancelEventRequest:
            ctx->complete(epoc::error_none);
            break;

        case KAlfCompOpBindSourceToToken:
        case KAlfCompOpEnableAlpha:
        case KAlfCompOpSetOpacity:
        case KAlfCompOpSetRotation:
        case KAlfCompOpSetZOrder:
        case KAlfCompOpSetExtent:
        case KAlfCompOpEnableKb:
        case KAlfComOpSetBackgroundAnim:
        case KAlfCompOpSessionClosed:
        case KAlfCompOpSetSRect:
        case KAlfCompositionFrameReady:
        case KAlfCompositionLowOnGraphicsMemory:
        case KAlfCompositionTargetHidden:
        case KAlfCompositionTargetCreated:
        case KAlfCompositionWServReady:
        case KAlfCompositionGoodOnGraphicsMemory:
        case KAlfCompositionTargetVisible:
        case KAlfCompositionTargetHiddenBGAnim:
        case KAlfCompositionTargetVisibleBGAnim:
        case KAlfCompositionLayoutSwitchComplete:
        case KAlfCompositionFlushDrawCommands:
            ctx->complete(epoc::error_none);
            break;

        default:
            LOG_TRACE(SERVICE_UI, "ALF composition opcode 0x{:X} accepted without a host composition target", opcode);
            ctx->complete(epoc::error_none);
            break;
        }
    }

    void alf_streamer_session::fetch(service::ipc_context *ctx) {
        const int opcode = ctx->msg->function;

        if ((opcode >= KAlfCompOpCreateSource) && (opcode < EAlfDecodSLoadPlugin)) {
            handle_composition_op(ctx, opcode);
            return;
        }

        switch (opcode) {
        case EAlfBridgerBlindSend:
        case EAlfBridgerSendChunk:
        case EAlfBridgerSendSyncData:
        case EAlfBridgerRequestDataBlock:
        case EAlfBridgerAsyncronousData:
        case EDsNotifyNativeWindowData:
        case EAlfSynchronize:
        case EAlfSetScreenRotation:
        case EAlfPostDataToCompositionClient:
        case EAlfPostDataToCompositionTarget:
        case EAlfQueueRequestBGSessions:
        case EAlfCompleteSignal:
        case EAlfSetDistractionWindow:
        case EAlfVolunteerForGoomTarget:
        case EAlfExcludeFromGoomTargets:
        case KAlfFlushDrawCommands:
        case EAlfSuppressVisibilityChanges:
        case EAlfDecodSCancelAsynch:
        case EAlfDecodSPrepareFrame:
            ctx->complete(epoc::error_none);
            break;

        case EAlfGetNativeWindowHandles:
            complete_native_window_handles(ctx);
            break;

        case EAlfGetListOfWGsHavingInactiveSurfaces:
            ctx->complete(epoc::error_not_found);
            break;

        case EAlfGetNumberOfActiveEffects:
            ctx->complete(0);
            break;

        case EAlfRequestSignal:
            ctx->complete(epoc::error_not_found);
            break;

        case EAlfDecodSLoadPlugin:
        case EAlfDecodSUnloadPlugin:
        case EAlfDecodSSendSynch:
        case EAlfDecodSSendAsynch:
            ctx->complete(epoc::error_not_supported);
            break;

        default:
            LOG_TRACE(SERVICE_UI, "ALF streamer opcode 0x{:X} acknowledged", opcode);
            ctx->complete(epoc::error_none);
            break;
        }
    }
}
