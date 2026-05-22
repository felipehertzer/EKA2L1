#include <services/drm/helper.h>

#include <common/log.h>
#include <system/epoc.h>
#include <utils/err.h>

#include <algorithm>
#include <optional>

namespace eka2l1 {
    namespace {
        enum drm_helper_opcode {
            drm_helper_register = 0,
            drm_helper_remove = 1,
            drm_helper_indicate_idle = 2,
            drm_helper_is_registered = 3
        };

        std::optional<std::string> get_uri(service::ipc_context *ctx) {
            if (ctx->get_argument_data_size(3) == 0) {
                return std::nullopt;
            }

            return ctx->get_argument_value<std::string>(3);
        }
    }

    drm_helper_server::drm_helper_server(eka2l1::system *sys)
        : service::typical_server(sys, "CDRMHelperServer") {
    }

    void drm_helper_server::connect(service::ipc_context &ctx) {
        create_session<drm_helper_session>(&ctx);
        ctx.complete(epoc::error_none);
    }

    void drm_helper_server::register_content(const std::string &uri, const std::uint8_t permission_type,
        const std::uint8_t registration_type, const std::uint8_t automated_type) {
        auto existing = std::find_if(automated_entries_.begin(), automated_entries_.end(),
            [&](const automated_entry &entry) {
                return (entry.uri == uri)
                    && (entry.permission_type == permission_type)
                    && (entry.registration_type == registration_type)
                    && (entry.automated_type == automated_type);
            });

        if (existing != automated_entries_.end()) {
            return;
        }

        automated_entries_.push_back({ uri, permission_type, registration_type, automated_type });
    }

    bool drm_helper_server::remove_content(const std::string &uri, const std::uint8_t permission_type,
        const std::uint8_t registration_type, const std::uint8_t automated_type) {
        auto existing = std::find_if(automated_entries_.begin(), automated_entries_.end(),
            [&](const automated_entry &entry) {
                return (entry.uri == uri)
                    && (entry.permission_type == permission_type)
                    && (entry.registration_type == registration_type)
                    && (entry.automated_type == automated_type);
            });

        if (existing == automated_entries_.end()) {
            return false;
        }

        automated_entries_.erase(existing);
        return true;
    }

    bool drm_helper_server::is_registered(const std::string &uri, const std::uint8_t registration_type,
        const std::uint8_t automated_type) const {
        return std::find_if(automated_entries_.begin(), automated_entries_.end(),
                   [&](const automated_entry &entry) {
                       return (entry.uri == uri)
                           && (entry.registration_type == registration_type)
                           && (entry.automated_type == automated_type);
                   })
            != automated_entries_.end();
    }

    drm_helper_session::drm_helper_session(service::typical_server *serv, const kernel::uid ss_id,
        epoc::version client_version)
        : service::typical_session(serv, ss_id, client_version) {
    }

    void drm_helper_session::fetch(service::ipc_context *ctx) {
        drm_helper_server *helper = server<drm_helper_server>();

        switch (ctx->msg->function) {
        case drm_helper_register: {
            auto uri = get_uri(ctx);
            if (!uri) {
                ctx->complete(epoc::error_argument);
                return;
            }

            helper->register_content(*uri,
                static_cast<std::uint8_t>(ctx->msg->args.args[0]),
                static_cast<std::uint8_t>(ctx->msg->args.args[1]),
                static_cast<std::uint8_t>(ctx->msg->args.args[2]));
            ctx->complete(epoc::error_none);
            break;
        }

        case drm_helper_remove: {
            auto uri = get_uri(ctx);
            if (!uri) {
                ctx->complete(epoc::error_argument);
                return;
            }

            if (static_cast<std::uint8_t>(ctx->msg->args.args[0]) == 3) {
                helper->remove_content(*uri, 1,
                    static_cast<std::uint8_t>(ctx->msg->args.args[1]),
                    static_cast<std::uint8_t>(ctx->msg->args.args[2]));
                helper->remove_content(*uri, 2,
                    static_cast<std::uint8_t>(ctx->msg->args.args[1]),
                    static_cast<std::uint8_t>(ctx->msg->args.args[2]));
            }

            if (!helper->remove_content(*uri,
                    static_cast<std::uint8_t>(ctx->msg->args.args[0]),
                    static_cast<std::uint8_t>(ctx->msg->args.args[1]),
                    static_cast<std::uint8_t>(ctx->msg->args.args[2]))) {
                ctx->complete(epoc::error_not_found);
                return;
            }

            ctx->complete(epoc::error_none);
            break;
        }

        case drm_helper_indicate_idle:
            ctx->complete(epoc::error_none);
            break;

        case drm_helper_is_registered: {
            auto uri = get_uri(ctx);
            if (!uri) {
                ctx->complete(epoc::error_argument);
                return;
            }

            const bool registered = helper->is_registered(*uri,
                static_cast<std::uint8_t>(ctx->msg->args.args[1]),
                static_cast<std::uint8_t>(ctx->msg->args.args[2]));
            ctx->complete(registered ? 1 : 0);
            break;
        }

        default:
            LOG_WARN(SERVICE_DRMSYS, "Unimplemented DRM helper opcode: 0x{:X}", ctx->msg->function);
            ctx->complete(epoc::error_not_supported);
            break;
        }
    }
}
