/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#pragma once

#include <drivers/audio/audio.h>

#include <SDL.h>

#include <atomic>

namespace eka2l1::drivers {
    class sdl_audio_output_stream final : public audio_output_stream {
    public:
        explicit sdl_audio_output_stream(audio_driver *driver, const std::uint32_t sample_rate,
            const std::uint8_t channels, data_callback callback);
        ~sdl_audio_output_stream() override;

        bool start() override;
        bool stop() override;
        void pause() override;

        bool is_playing() override;
        bool is_pausing() override;

        bool set_volume(const float volume) override;
        float get_volume() const override;
        bool current_frame_position(std::uint64_t *pos) override;

    private:
        static void audio_callback(void *userdata, Uint8 *stream, int len);
        void fill_audio(Uint8 *stream, int len);

        SDL_AudioDeviceID device_;
        SDL_AudioSpec obtained_;
        data_callback callback_;
        std::atomic<bool> playing_;
        std::atomic<bool> pausing_;
        std::atomic<float> volume_;
        std::atomic<std::uint64_t> rendered_frames_;
    };

    class sdl_audio_driver final : public audio_driver {
    public:
        explicit sdl_audio_driver(const std::uint32_t initial_master_volume = 100,
            const player_type preferred_midi_backend = player_type_tsf);
        ~sdl_audio_driver() override;

        std::unique_ptr<audio_output_stream> new_output_stream(const std::uint32_t sample_rate,
            const std::uint8_t channels, data_callback callback) override;
        std::unique_ptr<audio_input_stream> new_input_stream(const std::uint32_t sample_rate,
            const std::uint8_t channels, data_callback callback) override;
        std::uint32_t native_sample_rate() override;
    };
}
