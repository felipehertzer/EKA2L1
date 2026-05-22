/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#include <vita/state.h>

#include <common/fileutils.h>
#include <common/log.h>
#include <common/path.h>
#include <common/version.h>
#include <dispatch/libraries/register.h>
#include <drivers/audio/audio.h>
#include <drivers/graphics/graphics.h>
#include <kernel/kernel.h>
#include <kernel/libmanager.h>
#include <package/manager.h>
#include <services/init.h>
#include <services/window/window.h>
#include <system/devices.h>

namespace {
    static constexpr const char *VITA_WORK_DIR = "ux0:data/eka2l1";

    void prepare_vita_storage() {
        eka2l1::common::create_directories(VITA_WORK_DIR);
        eka2l1::common::set_current_directory(VITA_WORK_DIR);

        eka2l1::common::create_directories("data/drives/c");
        eka2l1::common::create_directories("data/drives/d");
        eka2l1::common::create_directories("data/drives/e");
        eka2l1::common::create_directories("data/drives/z");
        eka2l1::common::create_directories("resources");
        eka2l1::common::create_directories("compat");
        eka2l1::common::create_directories("patch");
        eka2l1::common::create_directories("scripts");
    }
}

namespace eka2l1::vita {
    emulator::emulator()
        : symsys(nullptr)
        , graphics_driver(nullptr)
        , audio_driver(nullptr)
        , sensor_driver(nullptr)
        , launcher(nullptr)
        , app_settings(nullptr)
        , window(nullptr)
        , should_emu_quit(false)
        , should_emu_pause(false)
        , should_graphics_pause(false)
        , stage_two_inited(false)
        , winserv(nullptr)
        , present_status(0)
        , system_reset_cbh(0) {
    }

    void emulator::register_draw_callback() {
        if (!winserv || !launcher) {
            return;
        }

        eka2l1::epoc::screen *screens = winserv->get_screens();
        while (screens) {
            std::size_t change_handle = screens->add_screen_redraw_callback(this, [](void *userdata, eka2l1::epoc::screen *scr, const bool is_dsa) {
                (void)is_dsa;
                emulator *state_ptr = reinterpret_cast<emulator *>(userdata);
                if (!state_ptr->graphics_driver || !state_ptr->window || !state_ptr->launcher) {
                    return;
                }

                state_ptr->graphics_driver->wait_for(&state_ptr->present_status);

                drivers::graphics_command_builder builder;
                state_ptr->launcher->draw(builder, scr, state_ptr->window->window_fb_size().x,
                    state_ptr->window->window_fb_size().y);

                state_ptr->present_status = -100;
                builder.present(&state_ptr->present_status);

                drivers::command_list retrieved = builder.retrieve_command_list();
                state_ptr->graphics_driver->submit_command_list(retrieved);
            });

            screen_change_handles.push_back(change_handle);
            screens = screens->next;
        }
    }

    void emulator::on_system_reset(system *the_sys) {
        winserv = reinterpret_cast<eka2l1::window_server *>(the_sys->get_kernel_system()->get_by_name<eka2l1::service::server>(
            eka2l1::get_winserv_name_by_epocver(symsys->get_symbian_version_use())));

        if (launcher) {
            launcher->retrieve_servers();
        }

        if (stage_two_inited) {
            register_draw_callback();
            the_sys->initialize_user_parties();
        }
    }

    void emulator::stage_one() {
        prepare_vita_storage();
        log::setup_log(nullptr);

        conf.deserialize();
        conf.storage = "data";
        if (log::filterings) {
            log::filterings->parse_filter_string(conf.log_filter);
        }

        LOG_INFO(FRONTEND_CMDLINE, "EKA2L1 PS Vita frontend ({}-{})", GIT_BRANCH, GIT_COMMIT_HASH);

        app_settings = std::make_unique<config::app_settings>(&conf);

        system_create_components comp;
        comp.audio_ = nullptr;
        comp.graphics_ = nullptr;
        comp.conf_ = &conf;
        comp.settings_ = app_settings.get();

        symsys = std::make_unique<eka2l1::system>(comp);

        device_manager *dvcmngr = symsys->get_device_manager();
        if (dvcmngr->total() > 0) {
            symsys->startup();

            if (!symsys->set_device(conf.device)) {
                LOG_ERROR(FRONTEND_CMDLINE, "Failed to set configured device {}, falling back to device 0", conf.device);
                conf.device = 0;
                symsys->rescan_devices(drive_z);
                symsys->set_device(0);
            }

            symsys->mount(drive_c, drive_media::physical, eka2l1::add_path(conf.storage, "/drives/c/"), io_attrib_internal);
            symsys->mount(drive_d, drive_media::physical, eka2l1::add_path(conf.storage, "/drives/d/"), io_attrib_internal);
            symsys->mount(drive_e, drive_media::physical, eka2l1::add_path(conf.storage, "/drives/e/"), io_attrib_removeable);

            on_system_reset(symsys.get());
        }

        system_reset_cbh = symsys->add_system_reset_callback([this](system *the_sys) {
            on_system_reset(the_sys);
        });

        launcher = std::make_unique<eka2l1::ios::launcher>(symsys.get());
        stage_two_inited = false;
    }

    bool emulator::stage_two() {
        if (stage_two_inited) {
            return true;
        }

        device_manager *dvcmngr = symsys->get_device_manager();
        device *dvc = dvcmngr->get_current();

        if (!dvc) {
            LOG_ERROR(FRONTEND_CMDLINE, "No current device is available. Copy an EKA2L1 data folder to ux0:data/eka2l1/data.");
            return false;
        }

        LOG_INFO(FRONTEND_CMDLINE, "Device being used: {} ({})", dvc->model, dvc->firmware_code);

        symsys->mount(drive_z, drive_media::rom,
            eka2l1::add_path(conf.storage, "/drives/z/"), io_attrib_internal | io_attrib_write_protected);

        drivers::player_type player_be = drivers::player_type_tsf;
        if (conf.midi_backend == config::MIDI_BACKEND_MINIBAE) {
            player_be = drivers::player_type_minibae;
        }

        audio_driver = drivers::make_audio_driver(drivers::audio_driver_backend::cubeb,
            conf.audio_master_volume, player_be);

        if (audio_driver) {
            audio_driver->set_bank_path(drivers::MIDI_BANK_TYPE_HSB, conf.hsb_bank_path);
            audio_driver->set_bank_path(drivers::MIDI_BANK_TYPE_SF2, conf.sf2_bank_path);
        }

        symsys->set_audio_driver(audio_driver.get());

        sensor_driver = drivers::sensor_driver::instantiate();
        symsys->set_sensor_driver(sensor_driver.get());
        symsys->initialize_user_parties();

        manager::packages *pkgmngr = symsys->get_packages();
        pkgmngr->load_registries();
        pkgmngr->migrate_legacy_registries();

        register_draw_callback();

        stage_two_inited = true;
        return true;
    }
}
