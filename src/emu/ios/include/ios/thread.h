/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#pragma once

namespace eka2l1::ios {
    struct emulator;

    bool emulator_entry(emulator &state);
    void start_threads(emulator &state);
    void pause_threads(emulator &state);
    void stop_threads(emulator &state);
    void surface_changed(emulator &state, void *layer, int width, int height, float scale);
    void press_key(emulator &state, int key, int key_state);
    void touch_screen(emulator &state, int x, int y, int z, int action, int pointer_id);
}
