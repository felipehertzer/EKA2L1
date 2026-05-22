/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#include <ios/emu_window_ios.h>

namespace eka2l1::drivers {
    emu_window_ios::emu_window_ios()
        : layer_(nullptr)
        , userdata_(nullptr)
        , window_size_(0, 0)
        , fb_size_(0, 0)
        , mouse_pos_({ 0.0, 0.0 })
        , scale_(1.0f)
        , mouse_pressed_(false)
        , quit_(false) {
    }

    void emu_window_ios::surface_changed(void *layer, int width, int height, float scale) {
        layer_ = layer;
        scale_ = scale;
        fb_size_ = vec2(width, height);
        window_size_ = vec2(static_cast<int>(width / scale), static_cast<int>(height / scale));

        if (surface_change_hook) {
            surface_change_hook(layer_);
        }

        if (resize_hook) {
            resize_hook(userdata_, fb_size_);
        }
    }

    void emu_window_ios::update_mouse_state(int x, int y, bool pressed) {
        mouse_pos_ = vec2d({ static_cast<double>(x), static_cast<double>(y) });
        mouse_pressed_ = pressed;
    }

    window_system_info emu_window_ios::get_window_system_info() {
        window_system_info wsi;
        wsi.type = window_system_type::ios;
        wsi.render_surface = layer_;
        wsi.render_surface_scale = scale_;
        wsi.surface_width = fb_size_.x;
        wsi.surface_height = fb_size_.y;
        return wsi;
    }

    bool emu_window_ios::get_mouse_button_hold(const int mouse_btt) {
        return (mouse_btt == mouse_button_left) && mouse_pressed_;
    }

    void emu_window_ios::change_title(std::string new_title) {
    }

    void emu_window_ios::init(std::string title, vec2 size, const std::uint32_t flags) {
        window_size_ = size;
        fb_size_ = size;
    }

    void emu_window_ios::make_current() {
    }

    void emu_window_ios::done_current() {
    }

    void emu_window_ios::swap_buffer() {
    }

    void emu_window_ios::poll_events() {
    }

    void emu_window_ios::set_userdata(void *userdata) {
        userdata_ = userdata;
    }

    void *emu_window_ios::get_userdata() {
        return userdata_;
    }

    void emu_window_ios::set_fullscreen(const bool is_fullscreen) {
    }

    bool emu_window_ios::should_quit() {
        return quit_;
    }

    void emu_window_ios::shutdown() {
        quit_ = true;
    }

    vec2 emu_window_ios::window_size() {
        return window_size_;
    }

    vec2 emu_window_ios::window_fb_size() {
        return fb_size_;
    }

    vec2d emu_window_ios::get_mouse_pos() {
        return mouse_pos_;
    }

    bool emu_window_ios::set_cursor(cursor *cur) {
        return false;
    }

    void emu_window_ios::cursor_visiblity(const bool visi) {
    }

    bool emu_window_ios::cursor_visiblity() {
        return false;
    }
}
