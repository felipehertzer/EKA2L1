/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <common/sync.h>
#include <config/app_settings.h>
#include <config/config.h>
#include <drivers/audio/audio.h>
#include <drivers/graphics/emu_window.h>
#include <drivers/graphics/graphics.h>
#include <drivers/sensor/sensor.h>
#include <ios/emu_window_ios.h>
#include <ios/launcher.h>
#include <services/window/window.h>
#include <system/epoc.h>

namespace eka2l1::ios {
    struct emulator {
        std::unique_ptr<system> symsys;
        std::unique_ptr<drivers::graphics_driver> graphics_driver;
        std::unique_ptr<drivers::audio_driver> audio_driver;
        std::unique_ptr<drivers::sensor_driver> sensor_driver;
        std::unique_ptr<launcher> launcher;
        std::unique_ptr<config::app_settings> app_settings;
        std::unique_ptr<drivers::emu_window_ios> window;

        std::atomic<bool> should_emu_quit;
        std::atomic<bool> should_emu_pause;
        std::atomic<bool> should_graphics_pause;
        std::atomic<bool> stage_two_inited;

        common::semaphore pause_sema;
        common::semaphore pause_graphics_sema;
        common::event graphics_init_done;

        config::state conf;
        window_server *winserv;
        int present_status;
        std::vector<std::size_t> screen_change_handles;
        std::size_t system_reset_cbh;
        std::mutex input_mutex;

        explicit emulator();

        void stage_one();
        bool stage_two();
        void on_system_reset(system *the_sys);
        void register_draw_callback();
    };
}
