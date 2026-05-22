/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#pragma once

#include <common/language.h>
#include <common/types.h>
#include <common/vecx.h>
#include <config/config.h>
#include <drivers/graphics/common.h>
#include <drivers/itc.h>
#include <drivers/ui/input_dialog.h>
#include <package/manager.h>
#include <services/applist/applist.h>
#include <services/drm/rights/rights.h>
#include <services/window/window.h>
#include <system/installation/common.h>

#include <cstdint>
#include <string>
#include <vector>

namespace eka2l1 {
    class fbs_server;
    class kernel_system;
    class rights_server;
    class system;
}

namespace eka2l1::ios {
    class launcher {
    public:
        explicit launcher(eka2l1::system *sys);

        std::vector<std::string> get_apps();
        void launch_app(std::uint32_t uid);
        package::installation_result install_app(std::string &path);
        void set_current_device(std::uint32_t id, bool temporary);
        std::vector<std::string> get_devices();
        std::vector<std::string> get_device_firwmare_codes();
        void set_device_name(std::uint32_t id, const char *name);
        void rescan_devices();
        std::uint32_t get_current_device() const;
        device_installation_error install_device(std::string &rpkg_path, std::string &rom_path, bool install_rpkg);
        bool does_rom_need_rpkg(const std::string &rom_path);
        std::vector<std::string> get_packages();
        void uninstall_package(std::uint32_t uid, std::int32_t ext_index);
        void mount_sd_card(std::string &path);
        void load_config();
        void set_language(std::uint32_t language_id);
        void set_rtos_level(std::uint32_t level);
        void update_app_setting(std::uint32_t uid);
        void draw(drivers::graphics_command_builder &builder, epoc::screen *scr,
            std::uint32_t width, std::uint32_t height);
        std::vector<std::string> get_language_ids();
        std::vector<std::string> get_language_names();
        void set_screen_params(std::uint32_t background_color, std::uint32_t scale_ratio,
            std::uint32_t scale_type, std::uint32_t gravity, const std::string &bg_img_path,
            float bg_img_opacity, bool keep_bg_aspect);
        bool open_input_view(const std::u16string &initial_text, const int max_len,
            drivers::ui::input_dialog_complete_callback complete_callback);
        void close_input_view();
        void on_finished_text_input(const std::string &text, bool force_close);
        bool open_question_dialog(const std::u16string &text, const std::u16string &button1_text,
            const std::u16string &button2_text, drivers::ui::yes_no_dialog_complete_callback complete_callback);
        void on_question_dialog_finished(int result);
        int install_ngage_game(const std::string &path);
        bool install_ng2_game_licenses(const std::string &content);
        std::vector<std::string> get_success_installed_license_games();
        std::vector<std::string> get_failed_installed_license_games();
        void set_current_mmc_id(const std::string &new_mmc_id);
        bool save_screenshot_to(const std::string &path);
        void retrieve_servers();

    private:
        eka2l1::system *sys_;
        config::state *conf_;
        eka2l1::kernel_system *kern_;
        applist_server *alserv_;
        window_server *winserv_;
        fbs_server *fbsserv_;
        rights_server *rightsserv_;
        std::vector<std::string> success_license_games_;
        std::vector<std::string> failed_license_games_;
        eka2l1::vecx<std::uint8_t, 3> background_color_;
        float scale_ratio_;
        std::uint32_t scale_type_;
        std::uint32_t gravity_;
        drivers::ui::input_dialog_complete_callback input_complete_callback_;
        drivers::ui::yes_no_dialog_complete_callback yes_no_complete_callback_;
        drivers::handle background_img_;
        std::string background_img_path_;
        std::string active_background_img_path_;
        float background_img_opacity_;
        int background_width_;
        int background_height_;
        bool keep_bg_aspect_;
        bool background_img_dirty_;
        std::vector<std::uint8_t> screenshot_buffer_;

        void set_language_to_property(language new_one);
        void set_language_current(language lang);
    };
}
