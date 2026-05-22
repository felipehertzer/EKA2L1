/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#include <vita/emu_window_vita.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace {
    static constexpr int VITA_WIDTH = 960;
    static constexpr int VITA_HEIGHT = 544;
    static constexpr int EKA2L1_KEY_CLEAR = 0x01;
    static constexpr int EKA2L1_KEY_DIAL = 10;
    static constexpr int EKA2L1_KEY_LEFT = 0x0E;
    static constexpr int EKA2L1_KEY_RIGHT = 0x0F;
    static constexpr int EKA2L1_KEY_UP = 0x10;
    static constexpr int EKA2L1_KEY_DOWN = 0x11;
    static constexpr int EKA2L1_KEY_POUND = 0x7F;
    static constexpr int EKA2L1_KEY_SOFT_LEFT = 0xA4;
    static constexpr int EKA2L1_KEY_SOFT_RIGHT = 0xA5;
    static constexpr int EKA2L1_KEY_FIRE = 0xA7;

    int key_for_controller_button(Uint8 button) {
        switch (button) {
        case SDL_CONTROLLER_BUTTON_A:
            return EKA2L1_KEY_FIRE;
        case SDL_CONTROLLER_BUTTON_B:
            return EKA2L1_KEY_CLEAR;
        case SDL_CONTROLLER_BUTTON_X:
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            return EKA2L1_KEY_SOFT_LEFT;
        case SDL_CONTROLLER_BUTTON_Y:
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            return EKA2L1_KEY_SOFT_RIGHT;
        case SDL_CONTROLLER_BUTTON_START:
            return EKA2L1_KEY_DIAL;
        case SDL_CONTROLLER_BUTTON_BACK:
            return EKA2L1_KEY_CLEAR;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            return EKA2L1_KEY_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            return EKA2L1_KEY_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            return EKA2L1_KEY_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            return EKA2L1_KEY_RIGHT;
        default:
            return -1;
        }
    }

    int key_for_keyboard(SDL_Keycode key) {
        switch (key) {
        case SDLK_UP:
            return EKA2L1_KEY_UP;
        case SDLK_DOWN:
            return EKA2L1_KEY_DOWN;
        case SDLK_LEFT:
            return EKA2L1_KEY_LEFT;
        case SDLK_RIGHT:
            return EKA2L1_KEY_RIGHT;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return EKA2L1_KEY_FIRE;
        case SDLK_BACKSPACE:
        case SDLK_ESCAPE:
            return EKA2L1_KEY_CLEAR;
        case SDLK_F1:
            return EKA2L1_KEY_SOFT_LEFT;
        case SDLK_F2:
            return EKA2L1_KEY_SOFT_RIGHT;
        case SDLK_F3:
            return EKA2L1_KEY_DIAL;
        case SDLK_HASH:
            return EKA2L1_KEY_POUND;
        default:
            break;
        }

        if (key >= SDLK_a && key <= SDLK_z) {
            return std::toupper(static_cast<unsigned char>('a' + (key - SDLK_a)));
        }

        if (key >= SDLK_0 && key <= SDLK_9) {
            return static_cast<int>('0' + (key - SDLK_0));
        }

        if (key == SDLK_ASTERISK) {
            return '*';
        }

        return -1;
    }
}

namespace eka2l1::drivers {
    emu_window_vita::emu_window_vita()
        : window_(nullptr)
        , controller_(nullptr)
        , userdata_(nullptr)
        , window_size_(VITA_WIDTH, VITA_HEIGHT)
        , fb_size_(VITA_WIDTH, VITA_HEIGHT)
        , mouse_pos_({ 0.0, 0.0 })
        , mouse_pressed_(false)
        , quit_(false) {
    }

    emu_window_vita::~emu_window_vita() {
        shutdown();
    }

    void emu_window_vita::open_first_controller() {
        if (controller_) {
            return;
        }

        const int joystick_count = SDL_NumJoysticks();
        for (int index = 0; index < joystick_count; index++) {
            if (SDL_IsGameController(index)) {
                controller_ = SDL_GameControllerOpen(index);
                if (controller_) {
                    return;
                }
            }
        }
    }

    void emu_window_vita::init(std::string title, vec2 size, const std::uint32_t flags) {
        SDL_SetMainReady();
        if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
            quit_ = true;
            return;
        }

        const int width = size.x > 0 ? size.x : VITA_WIDTH;
        const int height = size.y > 0 ? size.y : VITA_HEIGHT;
        Uint32 window_flags = SDL_WINDOW_SHOWN;
        if (flags & emu_window_flag_fullscreen) {
            window_flags |= SDL_WINDOW_FULLSCREEN;
        }

        window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            width, height, window_flags);
        if (!window_) {
            quit_ = true;
            return;
        }

        open_first_controller();
        update_size_from_window();
    }

    void emu_window_vita::update_size_from_window() {
        if (!window_) {
            return;
        }

        int window_width = 0;
        int window_height = 0;
        int drawable_width = 0;
        int drawable_height = 0;
        SDL_GetWindowSize(window_, &window_width, &window_height);
        SDL_GetWindowSize(window_, &drawable_width, &drawable_height);

        window_size_ = vec2(window_width, window_height);
        fb_size_ = vec2(drawable_width > 0 ? drawable_width : window_width,
            drawable_height > 0 ? drawable_height : window_height);
    }

    window_system_info emu_window_vita::get_window_system_info() {
        window_system_info wsi;
        wsi.type = window_system_type::vita;
        wsi.render_window = window_;
        wsi.render_surface = window_;
        wsi.surface_width = fb_size_.x;
        wsi.surface_height = fb_size_.y;
        return wsi;
    }

    void emu_window_vita::emit_touch(int x, int y, int pressure, int action, int pointer_id) {
        mouse_pos_ = vec2d({ static_cast<double>(x), static_cast<double>(y) });
        mouse_pressed_ = action != mouse_action_release;

        if (raw_mouse_event) {
            raw_mouse_event(userdata_, vec3(x, y, pressure), mouse_button_left, action, pointer_id);
        }
    }

    void emu_window_vita::emit_key(int key, bool pressed) {
        if (key < 0) {
            return;
        }

        if (pressed) {
            if (button_pressed) {
                button_pressed(userdata_, static_cast<std::uint32_t>(key));
            }
        } else if (button_released) {
            button_released(userdata_, static_cast<std::uint32_t>(key));
        }
    }

    void emu_window_vita::poll_events() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                quit_ = true;
                if (close_hook) {
                    close_hook(userdata_);
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
                open_first_controller();
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (controller_ && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller_)) == event.cdevice.which) {
                    SDL_GameControllerClose(controller_);
                    controller_ = nullptr;
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                emit_key(key_for_controller_button(event.cbutton.button), event.type == SDL_CONTROLLERBUTTONDOWN);
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                emit_key(key_for_keyboard(event.key.keysym.sym), event.type == SDL_KEYDOWN);
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    update_size_from_window();
                    if (resize_hook) {
                        resize_hook(userdata_, fb_size_);
                    }
                } else if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    quit_ = true;
                }
                break;
            case SDL_FINGERDOWN:
            case SDL_FINGERMOTION:
            case SDL_FINGERUP: {
                const int x = static_cast<int>(event.tfinger.x * static_cast<float>(fb_size_.x));
                const int y = static_cast<int>(event.tfinger.y * static_cast<float>(fb_size_.y));
                const int pressure = event.type == SDL_FINGERUP
                    ? 0
                    : std::max(1, static_cast<int>(event.tfinger.pressure * static_cast<float>(PRESSURE_MAX_NUM)));
                const int action = event.type == SDL_FINGERDOWN
                    ? mouse_action_press
                    : (event.type == SDL_FINGERUP ? mouse_action_release : mouse_action_repeat);
                const int pointer_id = static_cast<int>(std::llabs(event.tfinger.fingerId) % MAX_SYMBIAN_SUPPORTED_POINTERS);
                emit_touch(x, y, pressure, action, pointer_id);
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    emit_touch(event.button.x, event.button.y, event.type == SDL_MOUSEBUTTONUP ? 0 : PRESSURE_MAX_NUM,
                        event.type == SDL_MOUSEBUTTONUP ? mouse_action_release : mouse_action_press, 0);
                }
                break;
            }
            case SDL_MOUSEMOTION:
                if (mouse_pressed_) {
                    emit_touch(event.motion.x, event.motion.y, PRESSURE_MAX_NUM, mouse_action_repeat, 0);
                }
                break;
            default:
                break;
            }
        }
    }

    void emu_window_vita::shutdown() {
        if (controller_) {
            SDL_GameControllerClose(controller_);
            controller_ = nullptr;
        }

        if (window_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }

        quit_ = true;
    }

    bool emu_window_vita::get_mouse_button_hold(const int mouse_btt) {
        return (mouse_btt == mouse_button_left) && mouse_pressed_;
    }

    void emu_window_vita::change_title(std::string new_title) {
        if (window_) {
            SDL_SetWindowTitle(window_, new_title.c_str());
        }
    }

    void emu_window_vita::make_current() {
    }

    void emu_window_vita::done_current() {
    }

    void emu_window_vita::swap_buffer() {
    }

    void emu_window_vita::set_userdata(void *userdata) {
        userdata_ = userdata;
    }

    void *emu_window_vita::get_userdata() {
        return userdata_;
    }

    void emu_window_vita::set_fullscreen(const bool is_fullscreen) {
        if (window_) {
            SDL_SetWindowFullscreen(window_, is_fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
        }
    }

    bool emu_window_vita::should_quit() {
        return quit_;
    }

    vec2 emu_window_vita::window_size() {
        return window_size_;
    }

    vec2 emu_window_vita::window_fb_size() {
        return fb_size_;
    }

    vec2d emu_window_vita::get_mouse_pos() {
        return mouse_pos_;
    }

    bool emu_window_vita::set_cursor(cursor *cur) {
        (void)cur;
        return false;
    }

    void emu_window_vita::cursor_visiblity(const bool visi) {
        SDL_ShowCursor(visi ? SDL_ENABLE : SDL_DISABLE);
    }

    bool emu_window_vita::cursor_visiblity() {
        return SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE;
    }
}
