/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#include <drivers/audio/backend/sdl2/audio_sdl2.h>

#include <algorithm>
#include <cstring>

namespace eka2l1::drivers {
    sdl_audio_output_stream::sdl_audio_output_stream(audio_driver *driver, const std::uint32_t sample_rate,
        const std::uint8_t channels, data_callback callback)
        : audio_output_stream(driver, sample_rate, channels)
        , device_(0)
        , obtained_()
        , callback_(callback)
        , playing_(false)
        , pausing_(false)
        , volume_(1.0f)
        , rendered_frames_(0) {
        SDL_AudioSpec desired;
        SDL_zero(desired);
        desired.freq = static_cast<int>(sample_rate);
        desired.format = AUDIO_S16SYS;
        desired.channels = channels;
        desired.samples = 1024;
        desired.callback = &sdl_audio_output_stream::audio_callback;
        desired.userdata = this;

        device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained_, 0);
    }

    sdl_audio_output_stream::~sdl_audio_output_stream() {
        if (device_) {
            SDL_CloseAudioDevice(device_);
            device_ = 0;
        }
    }

    bool sdl_audio_output_stream::start() {
        if (!device_) {
            return false;
        }

        pausing_ = false;
        playing_ = true;
        SDL_PauseAudioDevice(device_, 0);
        return true;
    }

    bool sdl_audio_output_stream::stop() {
        if (!device_) {
            return false;
        }

        SDL_PauseAudioDevice(device_, 1);
        playing_ = false;
        pausing_ = false;
        rendered_frames_ = 0;
        return true;
    }

    void sdl_audio_output_stream::pause() {
        pausing_ = true;
    }

    bool sdl_audio_output_stream::is_playing() {
        return playing_;
    }

    bool sdl_audio_output_stream::is_pausing() {
        return pausing_;
    }

    bool sdl_audio_output_stream::set_volume(const float volume) {
        volume_ = std::max(0.0f, volume);
        return true;
    }

    float sdl_audio_output_stream::get_volume() const {
        return volume_;
    }

    bool sdl_audio_output_stream::current_frame_position(std::uint64_t *pos) {
        if (!pos) {
            return false;
        }

        *pos = rendered_frames_.load();
        return true;
    }

    void sdl_audio_output_stream::audio_callback(void *userdata, Uint8 *stream, int len) {
        reinterpret_cast<sdl_audio_output_stream *>(userdata)->fill_audio(stream, len);
    }

    void sdl_audio_output_stream::fill_audio(Uint8 *stream, int len) {
        const std::size_t bytes_per_frame = static_cast<std::size_t>(channels) * sizeof(std::int16_t);
        const std::size_t frame_count = bytes_per_frame ? static_cast<std::size_t>(len) / bytes_per_frame : 0;
        std::int16_t *samples = reinterpret_cast<std::int16_t *>(stream);

        if (!callback_ || pausing_ || driver_->suspending()) {
            std::memset(stream, 0, static_cast<std::size_t>(len));
            return;
        }

        const std::size_t rendered = callback_(samples, frame_count);
        if (rendered < frame_count) {
            std::memset(samples + rendered * channels, 0, (frame_count - rendered) * bytes_per_frame);
        }

        const float gain = volume_.load() * (static_cast<float>(driver_->master_volume()) / 100.0f);
        if (gain != 1.0f) {
            const std::size_t sample_count = frame_count * channels;
            for (std::size_t index = 0; index < sample_count; index++) {
                const float scaled = static_cast<float>(samples[index]) * gain;
                samples[index] = static_cast<std::int16_t>(std::clamp(scaled, -32768.0f, 32767.0f));
            }
        }

        rendered_frames_.fetch_add(rendered);
    }

    sdl_audio_driver::sdl_audio_driver(const std::uint32_t initial_master_volume,
        const player_type preferred_midi_backend)
        : audio_driver(initial_master_volume, preferred_midi_backend) {
        SDL_InitSubSystem(SDL_INIT_AUDIO);
    }

    sdl_audio_driver::~sdl_audio_driver() {
    }

    std::unique_ptr<audio_output_stream> sdl_audio_driver::new_output_stream(const std::uint32_t sample_rate,
        const std::uint8_t channels, data_callback callback) {
        return std::make_unique<sdl_audio_output_stream>(this, sample_rate, channels, callback);
    }

    std::unique_ptr<audio_input_stream> sdl_audio_driver::new_input_stream(const std::uint32_t sample_rate,
        const std::uint8_t channels, data_callback callback) {
        (void)sample_rate;
        (void)channels;
        (void)callback;
        return nullptr;
    }

    std::uint32_t sdl_audio_driver::native_sample_rate() {
        return 48000;
    }
}
