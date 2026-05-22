/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#include <ios/launcher.h>

#include <common/algorithm.h>
#include <common/cvt.h>
#include <common/fileutils.h>
#include <common/language.h>
#include <common/log.h>
#include <common/path.h>
#include <config/app_settings.h>
#include <drivers/graphics/graphics.h>
#include <kernel/kernel.h>
#include <loader/rom.h>
#include <package/manager.h>
#include <services/drm/rights/rights.h>
#include <services/fbs/fbs.h>
#include <services/window/window.h>
#include <system/devices.h>
#include <system/epoc.h>
#include <system/installation/firmware.h>
#include <system/installation/rpkg.h>
#include <utils/apacmd.h>
#include <utils/locale.h>
#include <utils/system.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace eka2l1::ios {
    static constexpr std::uint32_t WARE_APP_UID_START = 0x10300000;

    static inline bool is_reg_entry_probably_system_app(const apa_app_registry &reg) {
        return ((reg.land_drive == drive_z) && (reg.mandatory_info.uid < WARE_APP_UID_START));
    }

    launcher::launcher(eka2l1::system *sys)
        : sys_(sys)
        , conf_(sys->get_config())
        , kern_(sys->get_kernel_system())
        , alserv_(nullptr)
        , winserv_(nullptr)
        , fbsserv_(nullptr)
        , rightsserv_(nullptr)
        , background_color_({ 0xD0, 0xD0, 0xD0 })
        , scale_ratio_(100.0f)
        , scale_type_(1)
        , gravity_(2)
        , input_complete_callback_(nullptr)
        , yes_no_complete_callback_(nullptr)
        , background_img_(0)
        , background_img_opacity_(0.0f)
        , background_width_(0)
        , background_height_(0)
        , keep_bg_aspect_(true)
        , background_img_dirty_(false) {
        retrieve_servers();
    }

    void launcher::retrieve_servers() {
        kern_ = sys_->get_kernel_system();

        if (!kern_) {
            return;
        }

        alserv_ = reinterpret_cast<eka2l1::applist_server *>(kern_->get_by_name<service::server>(
            get_app_list_server_name_by_epocver(kern_->get_epoc_version())));
        winserv_ = reinterpret_cast<eka2l1::window_server *>(kern_->get_by_name<service::server>(
            get_winserv_name_by_epocver(kern_->get_epoc_version())));
        fbsserv_ = reinterpret_cast<eka2l1::fbs_server *>(kern_->get_by_name<service::server>(
            epoc::get_fbs_server_name_by_epocver(kern_->get_epoc_version())));
        rightsserv_ = reinterpret_cast<eka2l1::rights_server *>(kern_->get_by_name<service::server>(
            eka2l1::RIGHTS_SERVER_NAME));
    }

    std::vector<std::string> launcher::get_apps() {
        std::vector<std::string> info;

        if (!alserv_) {
            return info;
        }

        for (auto &reg : alserv_->get_registerations()) {
            if (reg.caps.is_hidden) {
                continue;
            }

            if (conf_ && conf_->hide_system_apps && is_reg_entry_probably_system_app(reg)) {
                continue;
            }

            info.push_back(std::to_string(reg.mandatory_info.uid));
            info.push_back(common::ucs2_to_utf8(reg.mandatory_info.long_caption.to_std_string(nullptr)));
        }

        return info;
    }

    void launcher::launch_app(std::uint32_t uid) {
        if (!alserv_ || !kern_) {
            return;
        }

        apa_app_registry *reg = alserv_->get_registration(uid);
        if (!reg) {
            return;
        }

        epoc::apa::command_line cmdline;
        cmdline.launch_cmd_ = epoc::apa::command_create;

        kern_->lock();
        alserv_->launch_app(*reg, cmdline, nullptr, [](kernel::process *pr) {});
        kern_->unlock();
    }

    package::installation_result launcher::install_app(std::string &path) {
        drive_number install_drive = sys_->is_s80_device_active()
            ? drive_number::drive_d
            : drive_number::drive_e;

        return static_cast<package::installation_result>(
            sys_->install_package(common::utf8_to_ucs2(path), install_drive));
    }

    void launcher::set_language_to_property(language new_one) {
        if (!kern_) {
            return;
        }

        property_ptr lang_prop = kern_->get_prop(epoc::SYS_CATEGORY, epoc::LOCALE_LANG_KEY);
        if (!lang_prop) {
            return;
        }

        auto current_lang = lang_prop->get_pkg<epoc::locale_language>();
        if (!current_lang) {
            return;
        }

        current_lang->language = static_cast<epoc::language>(new_one);
        lang_prop->set<epoc::locale_language>(current_lang.value());
    }

    void launcher::set_language_current(language lang) {
        if (!conf_) {
            return;
        }

        conf_->language = static_cast<int>(lang);
        sys_->set_system_language(lang);
        set_language_to_property(lang);
    }

    void launcher::set_current_device(std::uint32_t id, bool temporary) {
        device_manager *dvc_mngr = sys_->get_device_manager();
        if (!dvc_mngr || id >= dvc_mngr->get_devices().size()) {
            return;
        }

        auto &devices = dvc_mngr->get_devices();
        if (conf_ && conf_->device != static_cast<int>(id) && std::find(devices[id].languages.begin(), devices[id].languages.end(), conf_->language) == devices[id].languages.end()) {
            set_language_current(static_cast<language>(devices[id].default_language_code));
        }

        if (temporary) {
            sys_->set_device(id);
            retrieve_servers();
            return;
        }

        conf_->device = static_cast<int>(id);
        conf_->serialize();
        dvc_mngr->set_current(id);
    }

    std::vector<std::string> launcher::get_devices() {
        std::vector<std::string> devices;
        device_manager *dvc_mngr = sys_->get_device_manager();

        if (!dvc_mngr) {
            return devices;
        }

        for (auto &device : dvc_mngr->get_devices()) {
            devices.push_back(device.model);
        }

        return devices;
    }

    std::vector<std::string> launcher::get_device_firwmare_codes() {
        std::vector<std::string> firmware_codes;
        device_manager *dvc_mngr = sys_->get_device_manager();

        if (!dvc_mngr) {
            return firmware_codes;
        }

        for (auto &device : dvc_mngr->get_devices()) {
            firmware_codes.push_back(device.firmware_code);
        }

        return firmware_codes;
    }

    void launcher::set_device_name(std::uint32_t id, const char *name) {
        device_manager *dvc_mngr = sys_->get_device_manager();
        if (!dvc_mngr) {
            return;
        }

        auto &devices = dvc_mngr->get_devices();
        if (id >= devices.size()) {
            return;
        }

        devices[id].model = name;
        dvc_mngr->save_devices();
    }

    void launcher::rescan_devices() {
        sys_->rescan_devices(drive_z);
    }

    std::uint32_t launcher::get_current_device() const {
        return conf_ ? static_cast<std::uint32_t>(conf_->device) : 0;
    }

    bool launcher::does_rom_need_rpkg(const std::string &rom_path) {
        return loader::should_install_requires_additional_rpkg(rom_path);
    }

    device_installation_error launcher::install_device(std::string &rpkg_path, std::string &rom_path, bool install_rpkg) {
        std::string firmware_code;
        device_manager *dvc_mngr = sys_->get_device_manager();
        if (!dvc_mngr || !conf_) {
            return device_installation_general_failure;
        }

        const std::string root_c_path = add_path(conf_->storage, "drives/c/");
        const std::string root_e_path = add_path(conf_->storage, "drives/e/");
        const std::string root_z_path = add_path(conf_->storage, "drives/z/");
        const std::string rom_resident_path = add_path(conf_->storage, "roms/");

        common::create_directories(rom_resident_path);

        bool need_add_rpkg = false;
        device_installation_error result = device_installation_none;

        if (install_rpkg) {
            if (loader::should_install_requires_additional_rpkg(rom_path)) {
                result = loader::install_rpkg(dvc_mngr, rpkg_path, root_z_path, firmware_code, nullptr, nullptr);
                need_add_rpkg = true;
            } else {
                result = loader::install_rom(dvc_mngr, rom_path, rom_resident_path, root_z_path, nullptr, nullptr);
            }
        } else {
            result = install_firmware(dvc_mngr, rom_path, root_c_path, root_e_path, root_z_path, rom_resident_path, [](const std::vector<std::string> &variants) -> int {
                    (void)variants;
                    return 0; }, nullptr, nullptr);
        }

        if (result != device_installation_none) {
            return result;
        }

        dvc_mngr->save_devices();

        if (need_add_rpkg) {
            const std::string rom_directory = add_path(conf_->storage, add_path("roms", firmware_code + "\\"));
            common::create_directories(rom_directory);
            common::copy_file(rom_path, add_path(rom_directory, "SYM.ROM"), true);
        }

        return device_installation_none;
    }

    std::vector<std::string> launcher::get_packages() {
        std::vector<std::string> info;
        manager::packages *manager = sys_->get_packages();
        if (!manager) {
            return info;
        }

        for (const auto &[pkg_uid, pkg] : *manager) {
            if (!pkg.is_removable) {
                continue;
            }

            info.push_back(std::to_string(pkg.uid));
            info.push_back(std::to_string(pkg.index));
            info.push_back(common::ucs2_to_utf8(pkg.package_name));
        }

        return info;
    }

    void launcher::uninstall_package(std::uint32_t uid, std::int32_t ext_index) {
        manager::packages *manager = sys_->get_packages();
        if (!manager) {
            return;
        }

        package::object *obj = manager->package(uid, ext_index);
        if (obj) {
            manager->uninstall_package(*obj);
        }
    }

    void launcher::mount_sd_card(std::string &path) {
        io_system *io = sys_->get_io_system();
        if (!io) {
            return;
        }

        io->unmount(drive_e);
        io->mount_physical_path(drive_e, drive_media::physical,
            io_attrib_removeable | io_attrib_write_protected, common::utf8_to_ucs2(path));
    }

    void launcher::load_config() {
        if (conf_) {
            conf_->deserialize();
        }
    }

    void launcher::set_language(std::uint32_t language_id) {
        set_language_current(static_cast<language>(language_id));
    }

    void launcher::set_rtos_level(std::uint32_t level) {
        if (kern_) {
            kern_->get_ntimer()->set_realtime_level(static_cast<realtime_level>(level));
        }
    }

    void launcher::update_app_setting(std::uint32_t uid) {
        if (kern_) {
            kern_->get_app_settings()->update_setting(uid);
        }
    }

    void launcher::draw(drivers::graphics_command_builder &builder, epoc::screen *scr,
        std::uint32_t window_width, std::uint32_t window_height) {
        eka2l1::rect viewport;
        eka2l1::rect src;
        eka2l1::rect dest;

        drivers::filter_option filter = (conf_ && conf_->nearest_neighbor_filtering)
            ? drivers::filter_option::nearest
            : drivers::filter_option::linear;

        eka2l1::vec2 swapchain_size(window_width, window_height);
        viewport.size = swapchain_size;

        builder.set_swapchain_size(swapchain_size);
        builder.backup_state();
        builder.bind_bitmap(0);
        builder.set_feature(drivers::graphics_feature::cull, false);
        builder.set_feature(drivers::graphics_feature::depth_test, false);
        builder.set_feature(drivers::graphics_feature::blend, false);
        builder.set_feature(drivers::graphics_feature::clipping, false);
        builder.set_feature(drivers::graphics_feature::stencil_test, false);
        builder.set_viewport(viewport);
        builder.clear({ background_color_[0] / 255.0f, background_color_[1] / 255.0f,
                          background_color_[2] / 255.0f, 1.0f, 0.0f, 0.0f },
            drivers::draw_buffer_bit_color_buffer);

        if (background_img_dirty_) {
            if (background_img_) {
                builder.destroy(background_img_);
                background_img_ = 0;
            }
            background_width_ = 0;
            background_height_ = 0;
            background_img_dirty_ = false;
        }

        if (!background_img_ && !background_img_path_.empty()) {
            int comp = 0;
            FILE *f = common::open_c_file(background_img_path_, "rb");
            if (!f) {
                LOG_ERROR(FRONTEND_UI, "Unable to load iOS background texture");
                background_img_path_.clear();
                active_background_img_path_.clear();
            } else {
                stbi_uc *data = stbi_load_from_file(f, &background_width_, &background_height_, &comp, STBI_rgb_alpha);
                fclose(f);
                background_img_path_.clear();

                if (data && background_width_ > 0 && background_height_ > 0) {
                    background_img_ = drivers::create_texture(sys_->get_graphics_driver(), 2, 0,
                        drivers::texture_format::rgba, drivers::texture_format::rgba,
                        drivers::texture_data_type::ubyte, data,
                        static_cast<std::size_t>(background_width_ * background_height_ * 4),
                        eka2l1::vec3(background_width_, background_height_, 0));

                    if (background_img_) {
                        builder.set_texture_filter(background_img_, true, drivers::filter_option::nearest);
                    } else {
                        LOG_ERROR(FRONTEND_UI, "Unable to create iOS background texture");
                        active_background_img_path_.clear();
                    }
                }

                if (data) {
                    stbi_image_free(data);
                }
            }
        }

        if (background_img_ != 0) {
            eka2l1::rect draw_image_rect;
            draw_image_rect.size = swapchain_size;

            if (keep_bg_aspect_ && background_width_ > 0 && background_height_ > 0) {
                const float bg_aspect_ratio = static_cast<float>(background_width_) / static_cast<float>(background_height_);
                const bool fit_width = swapchain_size.y * bg_aspect_ratio <= draw_image_rect.size.x;

                if (fit_width) {
                    draw_image_rect.size.x = swapchain_size.x;
                    draw_image_rect.size.y = static_cast<int>(swapchain_size.x / bg_aspect_ratio);
                    draw_image_rect.top.y = (swapchain_size.y - draw_image_rect.size.y) / 2;
                } else {
                    draw_image_rect.size.y = swapchain_size.y;
                    draw_image_rect.size.x = static_cast<int>(swapchain_size.y * bg_aspect_ratio);
                    draw_image_rect.top.x = (swapchain_size.x - draw_image_rect.size.x) / 2;
                }
            }

            builder.set_feature(drivers::graphics_feature::blend, true);
            builder.blend_formula(drivers::blend_equation::add, drivers::blend_equation::add,
                drivers::blend_factor::frag_out_alpha, drivers::blend_factor::one_minus_frag_out_alpha,
                drivers::blend_factor::one, drivers::blend_factor::one_minus_frag_out_alpha);
            builder.set_brush_color_detail(eka2l1::vec4(255, 255, 255,
                common::clamp<int>(0, 255, static_cast<int>(background_img_opacity_ * 255.0f))));
            builder.draw_bitmap(background_img_, 0, draw_image_rect, eka2l1::rect(), eka2l1::vec2(0, 0),
                0.0f, drivers::bitmap_draw_flag_use_brush);
            builder.set_feature(drivers::graphics_feature::blend, false);
        }

        if (scr) {
            auto &crr_mode = scr->current_mode();
            eka2l1::vec2 size = crr_mode.size;
            src.size = size;

            float width = 0;
            float height = 0;
            std::uint32_t x = 0;
            std::uint32_t y = 0;

            switch (scale_type_) {
            case 0:
                width = size.x;
                height = size.y;
                break;
            case 2:
                width = swapchain_size.x;
                height = swapchain_size.y;
                break;
            case 1:
            default:
                width = swapchain_size.x;
                height = size.y * swapchain_size.x / size.x;

                if (height > swapchain_size.y) {
                    height = swapchain_size.y;
                    width = size.x * swapchain_size.y / size.y;
                }
                break;
            }

            width = width * scale_ratio_ / 100;
            height = height * scale_ratio_ / 100;

            switch (gravity_) {
            case 0:
                x = 0;
                y = (swapchain_size.y - height) / 2;
                break;
            case 1:
                x = (swapchain_size.x - width) / 2;
                y = 0;
                break;
            case 3:
                x = swapchain_size.x - width;
                y = (swapchain_size.y - height) / 2;
                break;
            case 4:
                x = (swapchain_size.x - width) / 2;
                y = swapchain_size.y - height;
                break;
            case 2:
            default:
                x = (swapchain_size.x - width) / 2;
                y = (swapchain_size.y - height) / 2;
                break;
            }

            const float scale_x = width / static_cast<float>(size.x);
            const float scale_y = height / static_cast<float>(size.y);

            scr->set_native_scale_factor(sys_->get_graphics_driver(), scale_x, scale_y);
            scr->absolute_pos.x = static_cast<int>(x);
            scr->absolute_pos.y = static_cast<int>(y);

            dest.top = eka2l1::vec2(x, y);
            dest.size = eka2l1::vec2(width, height);
            drivers::advance_draw_pos_around_origin(dest, scr->ui_rotation);

            if (scr->ui_rotation % 180 != 0) {
                std::swap(dest.size.x, dest.size.y);
                std::swap(src.size.x, src.size.y);
            }

            src.size *= scr->display_scale_factor;

            std::uint32_t flags = 0;
            if (scr->flags_ & epoc::screen::FLAG_SCREEN_UPSCALE_FACTOR_LOCK) {
                flags |= drivers::bitmap_draw_flag_use_upscale_shader;
            }

            builder.set_texture_filter(scr->screen_texture, true, filter);
            builder.set_texture_filter(scr->screen_texture, false, filter);
            builder.draw_bitmap(scr->screen_texture, 0, dest, src, eka2l1::vec2(0, 0),
                static_cast<float>(scr->ui_rotation), flags);
        }

        builder.load_backup_state();
    }

    std::vector<std::string> launcher::get_language_ids() {
        std::vector<std::string> languages;
        device_manager *dvc_mngr = sys_->get_device_manager();
        if (!dvc_mngr || !conf_) {
            return languages;
        }

        auto &devices = dvc_mngr->get_devices();
        if (devices.empty() || conf_->device < 0 || static_cast<std::size_t>(conf_->device) >= devices.size()) {
            return languages;
        }

        for (int language_id : devices[conf_->device].languages) {
            languages.push_back(std::to_string(language_id));
        }

        return languages;
    }

    std::vector<std::string> launcher::get_language_names() {
        std::vector<std::string> languages;
        device_manager *dvc_mngr = sys_->get_device_manager();
        if (!dvc_mngr || !conf_) {
            return languages;
        }

        auto &devices = dvc_mngr->get_devices();
        if (devices.empty() || conf_->device < 0 || static_cast<std::size_t>(conf_->device) >= devices.size()) {
            return languages;
        }

        for (int language_id : devices[conf_->device].languages) {
            languages.push_back(common::get_language_name_by_code(language_id));
        }

        return languages;
    }

    void launcher::set_screen_params(std::uint32_t background_color, std::uint32_t scale_ratio,
        std::uint32_t scale_type, std::uint32_t gravity, const std::string &bg_img_path,
        float bg_img_opacity, bool keep_bg_aspect) {
        background_color_[0] = (background_color >> 16) & 0xFF;
        background_color_[1] = (background_color >> 8) & 0xFF;
        background_color_[2] = background_color & 0xFF;
        scale_ratio_ = static_cast<float>(scale_ratio);
        scale_type_ = scale_type;
        gravity_ = gravity;
        if (active_background_img_path_ != bg_img_path) {
            background_img_path_ = bg_img_path;
            active_background_img_path_ = bg_img_path;
            background_img_dirty_ = true;
        }
        background_img_opacity_ = bg_img_opacity;
        keep_bg_aspect_ = keep_bg_aspect;
    }

    bool launcher::open_input_view(const std::u16string &initial_text, const int max_len,
        drivers::ui::input_dialog_complete_callback complete_callback) {
        (void)initial_text;
        (void)max_len;
        if (input_complete_callback_) {
            return false;
        }

        input_complete_callback_ = complete_callback;
        return true;
    }

    void launcher::close_input_view() {
        if (input_complete_callback_) {
            input_complete_callback_(u"");
            input_complete_callback_ = nullptr;
        }
    }

    void launcher::on_finished_text_input(const std::string &text, bool force_close) {
        (void)force_close;
        if (input_complete_callback_) {
            input_complete_callback_(common::utf8_to_ucs2(text));
            input_complete_callback_ = nullptr;
        }
    }

    bool launcher::open_question_dialog(const std::u16string &text, const std::u16string &button1_text,
        const std::u16string &button2_text, drivers::ui::yes_no_dialog_complete_callback complete_callback) {
        (void)text;
        (void)button1_text;
        (void)button2_text;
        if (yes_no_complete_callback_) {
            return false;
        }

        yes_no_complete_callback_ = complete_callback;
        return true;
    }

    void launcher::on_question_dialog_finished(int result) {
        if (yes_no_complete_callback_) {
            yes_no_complete_callback_(result);
            yes_no_complete_callback_ = nullptr;
        }
    }

    int launcher::install_ngage_game(const std::string &path) {
        return static_cast<int>(sys_->install_ngage_game_card(path, nullptr, nullptr));
    }

    bool launcher::install_ng2_game_licenses(const std::string &content) {
        if (!rightsserv_) {
            return false;
        }

        return rightsserv_->import_ng2l(content, success_license_games_, failed_license_games_);
    }

    std::vector<std::string> launcher::get_success_installed_license_games() {
        return success_license_games_;
    }

    std::vector<std::string> launcher::get_failed_installed_license_games() {
        return failed_license_games_;
    }

    void launcher::set_current_mmc_id(const std::string &new_mmc_id) {
        if (conf_) {
            conf_->current_mmc_id = new_mmc_id;
        }
    }

    bool launcher::save_screenshot_to(const std::string &path) {
        if (!winserv_ || !sys_->get_graphics_driver()) {
            return false;
        }

        epoc::screen *scr = winserv_->get_current_focus_screen();
        if (!scr) {
            return false;
        }

        eka2l1::vec2 scr_size_scaled = scr->current_mode().size * scr->display_scale_factor;
        const std::size_t total_data_size = static_cast<std::size_t>(scr_size_scaled.x) * scr_size_scaled.y * 4;
        if (screenshot_buffer_.size() < total_data_size) {
            screenshot_buffer_.resize(total_data_size);
        }

        if (!drivers::read_bitmap(sys_->get_graphics_driver(), scr->screen_texture,
                eka2l1::point(0, 0), scr_size_scaled, 32, screenshot_buffer_.data())) {
            return false;
        }

        return stbi_write_png(path.c_str(), scr_size_scaled.x, scr_size_scaled.y, 4,
                   screenshot_buffer_.data(), scr_size_scaled.x * 4)
            != 0;
    }
}
