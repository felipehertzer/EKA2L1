/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#pragma once

#include <common/vecx.h>
#include <drivers/graphics/emu_window.h>

#include <SDL.h>

namespace eka2l1::drivers {
    class emu_window_vita final : public emu_window {
    public:
        explicit emu_window_vita();
        ~emu_window_vita() override;

        window_system_info get_window_system_info() override;
        bool get_mouse_button_hold(const int mouse_btt) override;
        void change_title(std::string new_title) override;
        void init(std::string title, vec2 size, const std::uint32_t flags) override;
        void make_current() override;
        void done_current() override;
        void swap_buffer() override;
        void poll_events() override;
        void set_userdata(void *userdata) override;
        void *get_userdata() override;
        void set_fullscreen(const bool is_fullscreen) override;
        bool should_quit() override;
        void shutdown() override;
        vec2 window_size() override;
        vec2 window_fb_size() override;
        vec2d get_mouse_pos() override;
        bool set_cursor(cursor *cur) override;
        void cursor_visiblity(const bool visi) override;
        bool cursor_visiblity() override;

    private:
        void update_size_from_window();
        void emit_touch(int x, int y, int pressure, int action, int pointer_id);
        void emit_key(int key, bool pressed);
        void open_first_controller();

        SDL_Window *window_;
        SDL_GameController *controller_;
        void *userdata_;
        vec2 window_size_;
        vec2 fb_size_;
        vec2d mouse_pos_;
        bool mouse_pressed_;
        bool quit_;
    };
}
