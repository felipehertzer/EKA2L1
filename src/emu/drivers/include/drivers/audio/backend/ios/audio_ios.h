/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <drivers/audio/audio.h>
#include <drivers/audio/stream.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

struct AudioQueueBuffer;
using AudioQueueBufferRef = AudioQueueBuffer *;
struct OpaqueAudioQueue;
using AudioQueueRef = OpaqueAudioQueue *;

namespace eka2l1::drivers {
    class ios_audio_driver final : public audio_driver {
    private:
        std::uint32_t native_rate_;

    public:
        explicit ios_audio_driver(const std::uint32_t initial_master_volume = 100,
            const player_type preferred_midi_backend = player_type_tsf);
        ~ios_audio_driver() override;

        std::unique_ptr<audio_output_stream> new_output_stream(const std::uint32_t sample_rate,
            const std::uint8_t channels, data_callback callback) override;
        std::unique_ptr<audio_input_stream> new_input_stream(const std::uint32_t sample_rate,
            const std::uint8_t channels, data_callback callback) override;
        std::uint32_t native_sample_rate() override;
    };

    class ios_audio_output_stream final : public audio_output_stream {
    private:
        AudioQueueRef queue_;
        data_callback callback_;
        std::vector<AudioQueueBufferRef> buffers_;
        std::atomic<bool> playing_;
        std::atomic<bool> pausing_;
        std::atomic<std::uint64_t> submitted_frames_;
        std::uint32_t frames_per_buffer_;
        std::uint32_t buffer_byte_size_;
        float volume_;

        static void audio_queue_callback(void *user_data, AudioQueueRef queue, AudioQueueBufferRef buffer);
        void fill_and_enqueue(AudioQueueBufferRef buffer);

    public:
        ios_audio_output_stream(audio_driver *driver, const std::uint32_t sample_rate,
            const std::uint8_t channels, data_callback callback);
        ~ios_audio_output_stream() override;

        bool start() override;
        bool stop() override;
        void pause() override;

        bool is_playing() override;
        bool is_pausing() override;

        bool set_volume(const float volume) override;
        float get_volume() const override;

        bool current_frame_position(std::uint64_t *pos) override;
    };

    class ios_audio_input_stream final : public audio_input_stream {
    private:
        AudioQueueRef queue_;
        data_callback callback_;
        std::vector<AudioQueueBufferRef> buffers_;
        std::atomic<bool> recording_;
        std::atomic<std::uint64_t> received_frames_;
        std::uint32_t frames_per_buffer_;
        std::uint32_t buffer_byte_size_;

    public:
        ios_audio_input_stream(audio_driver *driver, const std::uint32_t sample_rate,
            const std::uint8_t channels, data_callback callback);
        ~ios_audio_input_stream() override;

        bool start() override;
        bool stop() override;
        bool is_recording() override;
        bool current_frame_position(std::uint64_t *pos) override;

        void process_input_buffer(AudioQueueRef queue, AudioQueueBufferRef buffer);
    };
}
