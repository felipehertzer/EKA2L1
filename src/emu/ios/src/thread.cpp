/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#include <ios/thread.h>

#include <common/log.h>
#include <common/thread.h>
#include <drivers/graphics/graphics.h>
#include <ios/state.h>

namespace {
    std::unique_ptr<std::thread> os_thread_obj;
    std::unique_ptr<std::thread> graphics_thread_obj;
}

namespace eka2l1::ios {
    static constexpr const char *os_thread_name = "Symbian OS thread";
    static constexpr const char *graphics_driver_thread_name = "Graphics thread";

    static int graphics_driver_thread_initialization(emulator &state) {
        eka2l1::common::set_thread_name(graphics_driver_thread_name);
        eka2l1::common::set_thread_priority(eka2l1::common::thread_priority_high);

        state.window = std::make_unique<drivers::emu_window_ios>();
        state.window->init("EKA2L1", eka2l1::vec2(0, 0), drivers::emu_window_flag_maximum_size);
        state.window->set_userdata(&state);

        state.graphics_driver = drivers::create_graphics_driver(drivers::graphic_api::vulkan,
            state.window->get_window_system_info());
        if (!state.graphics_driver) {
            return -1;
        }

        state.symsys->set_graphics_driver(state.graphics_driver.get());

        drivers::emu_window_ios *window = state.window.get();
        window->surface_change_hook = [&state](void *new_surface) {
            if (state.graphics_driver) {
                state.graphics_driver->update_surface(new_surface);
                state.graphics_driver->update_surface_size(state.window->window_fb_size());
            }
        };

        state.graphics_driver->set_display_hook([window, &state]() {
            window->poll_events();

            if (state.should_graphics_pause) {
                state.graphics_driver->update_surface(nullptr);
                state.pause_graphics_sema.wait();
            }
        });

        state.graphics_init_done.set();
        return 0;
    }

    static void graphics_driver_thread(emulator &state) {
        int result = graphics_driver_thread_initialization(state);
        if (result != 0) {
            LOG_ERROR(FRONTEND_CMDLINE, "Graphics driver initialization failed with code {}", result);
            state.graphics_init_done.set();
            return;
        }

        state.graphics_driver->run();
        state.graphics_driver.reset();
    }

    static void os_thread(emulator &state) {
        eka2l1::common::set_thread_name(os_thread_name);
        eka2l1::common::set_thread_priority(eka2l1::common::thread_priority_high);

        while (!state.should_emu_quit) {
            state.symsys->loop();

            if (state.should_emu_pause) {
                state.symsys->pause();
                state.pause_sema.wait();
                state.symsys->unpause();
            }
        }

        state.symsys.reset();
    }

    bool emulator_entry(emulator &state) {
        state.stage_one();
        const bool result = state.stage_two();

        if (result) {
            os_thread_obj = std::make_unique<std::thread>(os_thread, std::ref(state));
        }

        state.graphics_init_done.reset();
        graphics_thread_obj = std::make_unique<std::thread>(graphics_driver_thread, std::ref(state));
        state.graphics_init_done.wait();

        return result;
    }

    void start_threads(emulator &state) {
        state.should_emu_pause = false;
        state.should_graphics_pause = false;
        state.pause_graphics_sema.notify();
        state.pause_sema.notify();

        if (state.sensor_driver) {
            state.sensor_driver->resume();
        }

        if (state.audio_driver) {
            state.audio_driver->resume();
        }
    }

    void pause_threads(emulator &state) {
        state.should_emu_pause = true;
        state.should_graphics_pause = true;

        if (state.sensor_driver) {
            state.sensor_driver->pause();
        }

        if (state.audio_driver) {
            state.audio_driver->suspend();
        }
    }

    void stop_threads(emulator &state) {
        state.should_emu_quit = true;

        if (state.graphics_driver) {
            state.graphics_driver->abort();
        }

        state.pause_graphics_sema.notify();
        state.pause_sema.notify();

        if (os_thread_obj && os_thread_obj->joinable()) {
            os_thread_obj->join();
        }

        if (graphics_thread_obj && graphics_thread_obj->joinable()) {
            graphics_thread_obj->join();
        }
    }

    void surface_changed(emulator &state, void *layer, int width, int height, float scale) {
        if (!state.window) {
            return;
        }

        state.window->surface_changed(layer, width, height, scale);
    }

    void press_key(emulator &state, int key, int key_state) {
        if (!state.winserv) {
            return;
        }

        eka2l1::drivers::input_event evt;
        evt.type_ = eka2l1::drivers::input_event_type::key_raw;
        evt.key_.state_ = static_cast<eka2l1::drivers::key_state>(key_state);
        evt.key_.code_ = key;
        state.winserv->queue_input_from_driver(evt);
    }

    void touch_screen(emulator &state, int x, int y, int z, int action, int pointer_id) {
        if (!state.winserv) {
            return;
        }

        eka2l1::drivers::input_event evt;
        evt.type_ = eka2l1::drivers::input_event_type::touch;
        evt.mouse_.pos_x_ = x;
        evt.mouse_.pos_y_ = y;
        evt.mouse_.pos_z_ = z;
        evt.mouse_.mouse_id = static_cast<std::uint32_t>(pointer_id);
        evt.mouse_.button_ = eka2l1::drivers::mouse_button::mouse_button_left;
        evt.mouse_.action_ = static_cast<eka2l1::drivers::mouse_action>(action);
        evt.mouse_.raw_screen_pos_ = false;
        state.winserv->queue_input_from_driver(evt);
    }
}
