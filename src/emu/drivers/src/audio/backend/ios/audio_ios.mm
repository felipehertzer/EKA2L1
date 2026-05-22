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

#include <drivers/audio/backend/baeplat_impl.h>
#include <drivers/audio/backend/ios/audio_ios.h>

#include <common/log.h>

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <cstring>

namespace eka2l1::drivers {
    static bool configure_audio_session_for_recording() {
        @autoreleasepool {
            AVAudioSession *session = [AVAudioSession sharedInstance];
            NSError *error = nil;

            [session setCategory:AVAudioSessionCategoryPlayAndRecord
                     withOptions:(AVAudioSessionCategoryOptionDefaultToSpeaker | AVAudioSessionCategoryOptionAllowBluetoothHFP)
                           error:&error];
            if (error) {
                LOG_WARN(DRIVER_AUD, "Failed to set iOS recording audio session category: {}", [[error localizedDescription] UTF8String]);
                return false;
            }

            error = nil;
            [session setPreferredSampleRate:48000.0 error:&error];
            if (error) {
                LOG_WARN(DRIVER_AUD, "Failed to set iOS recording preferred sample rate: {}", [[error localizedDescription] UTF8String]);
            }

            error = nil;
            [session setActive:YES error:&error];
            if (error) {
                LOG_WARN(DRIVER_AUD, "Failed to activate iOS recording audio session: {}", [[error localizedDescription] UTF8String]);
                return false;
            }

            return true;
        }
    }

    static void audio_queue_input_callback(void *user_data, AudioQueueRef queue, AudioQueueBufferRef buffer,
        const AudioTimeStamp *start_time, UInt32 packet_count, const AudioStreamPacketDescription *packet_descriptions) {
        (void)start_time;
        (void)packet_count;
        (void)packet_descriptions;

        reinterpret_cast<ios_audio_input_stream *>(user_data)->process_input_buffer(queue, buffer);
    }

    ios_audio_driver::ios_audio_driver(const std::uint32_t initial_master_volume,
        const player_type preferred_midi_backend)
        : audio_driver(initial_master_volume, preferred_midi_backend)
        , native_rate_(48000) {
        @autoreleasepool {
            AVAudioSession *session = [AVAudioSession sharedInstance];
            NSError *error = nil;
            [session setCategory:AVAudioSessionCategoryPlayback error:&error];
            if (error) {
                LOG_WARN(DRIVER_AUD, "Failed to set iOS audio session category: {}", [[error localizedDescription] UTF8String]);
            }

            error = nil;
            [session setPreferredSampleRate:48000.0 error:&error];
            if (error) {
                LOG_WARN(DRIVER_AUD, "Failed to set iOS preferred sample rate: {}", [[error localizedDescription] UTF8String]);
            }

            error = nil;
            [session setActive:YES error:&error];
            if (error) {
                LOG_WARN(DRIVER_AUD, "Failed to activate iOS audio session: {}", [[error localizedDescription] UTF8String]);
            }

            if ([session sampleRate] > 0.0) {
                native_rate_ = static_cast<std::uint32_t>([session sampleRate]);
            }
        }
    }

    ios_audio_driver::~ios_audio_driver() {
        BAE_DriverDeactivated(this);
    }

    std::unique_ptr<audio_output_stream> ios_audio_driver::new_output_stream(const std::uint32_t sample_rate,
        const std::uint8_t channels, data_callback callback) {
        return std::make_unique<ios_audio_output_stream>(this, sample_rate, channels, callback);
    }

    std::unique_ptr<audio_input_stream> ios_audio_driver::new_input_stream(const std::uint32_t sample_rate,
        const std::uint8_t channels, data_callback callback) {
        return std::make_unique<ios_audio_input_stream>(this, sample_rate, channels, callback);
    }

    std::uint32_t ios_audio_driver::native_sample_rate() {
        return native_rate_;
    }

    ios_audio_output_stream::ios_audio_output_stream(audio_driver *driver, const std::uint32_t sample_rate,
        const std::uint8_t channels, data_callback callback)
        : audio_output_stream(driver, sample_rate, channels)
        , queue_(nullptr)
        , callback_(callback)
        , playing_(false)
        , pausing_(false)
        , submitted_frames_(0)
        , frames_per_buffer_(std::max<std::uint32_t>(256, sample_rate / 50))
        , buffer_byte_size_(frames_per_buffer_ * channels * sizeof(std::int16_t))
        , volume_(1.0f) {
        AudioStreamBasicDescription format = {};
        format.mSampleRate = sample_rate;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
        format.mBytesPerPacket = channels * sizeof(std::int16_t);
        format.mFramesPerPacket = 1;
        format.mBytesPerFrame = channels * sizeof(std::int16_t);
        format.mChannelsPerFrame = channels;
        format.mBitsPerChannel = 16;

        const OSStatus status = AudioQueueNewOutput(&format, audio_queue_callback, this, nullptr,
            kCFRunLoopCommonModes, 0, &queue_);
        if (status != noErr) {
            LOG_ERROR(DRIVER_AUD, "Failed to create iOS AudioQueue output stream: {}", static_cast<int>(status));
            queue_ = nullptr;
            return;
        }

        buffers_.reserve(3);
        for (int i = 0; i < 3; i++) {
            AudioQueueBufferRef buffer = nullptr;
            if (AudioQueueAllocateBuffer(queue_, buffer_byte_size_, &buffer) == noErr) {
                buffers_.push_back(buffer);
            }
        }

        set_volume(volume_);
    }

    ios_audio_output_stream::~ios_audio_output_stream() {
        stop();
        if (queue_) {
            AudioQueueDispose(queue_, true);
            queue_ = nullptr;
        }
    }

    void ios_audio_output_stream::audio_queue_callback(void *user_data, AudioQueueRef queue, AudioQueueBufferRef buffer) {
        (void)queue;
        reinterpret_cast<ios_audio_output_stream *>(user_data)->fill_and_enqueue(buffer);
    }

    void ios_audio_output_stream::fill_and_enqueue(AudioQueueBufferRef buffer) {
        auto *output = reinterpret_cast<std::int16_t *>(buffer->mAudioData);
        std::size_t frames_written = 0;

        if (pausing_ || driver_->suspending()) {
            std::memset(output, 0, buffer_byte_size_);
            frames_written = frames_per_buffer_;
        } else {
            frames_written = callback_(output, frames_per_buffer_);
            if (frames_written < frames_per_buffer_) {
                const std::size_t written_samples = frames_written * channels;
                const std::size_t remaining_samples = (frames_per_buffer_ - frames_written) * channels;
                std::memset(output + written_samples, 0, remaining_samples * sizeof(std::int16_t));
                frames_written = frames_per_buffer_;
            }
        }

        buffer->mAudioDataByteSize = buffer_byte_size_;
        submitted_frames_ += frames_written;

        if (playing_) {
            AudioQueueEnqueueBuffer(queue_, buffer, 0, nullptr);
        }
    }

    bool ios_audio_output_stream::start() {
        if (!queue_) {
            return false;
        }

        if (playing_) {
            pausing_ = false;
            return true;
        }

        submitted_frames_ = 0;
        pausing_ = false;
        playing_ = true;

        for (AudioQueueBufferRef buffer : buffers_) {
            fill_and_enqueue(buffer);
        }

        return AudioQueueStart(queue_, nullptr) == noErr;
    }

    bool ios_audio_output_stream::stop() {
        if (!queue_) {
            return false;
        }

        if (!playing_) {
            return true;
        }

        playing_ = false;
        pausing_ = false;
        submitted_frames_ = 0;
        AudioQueueStop(queue_, true);
        AudioQueueReset(queue_);
        return true;
    }

    void ios_audio_output_stream::pause() {
        pausing_ = true;
    }

    bool ios_audio_output_stream::is_playing() {
        return playing_;
    }

    bool ios_audio_output_stream::is_pausing() {
        return pausing_;
    }

    bool ios_audio_output_stream::set_volume(const float volume) {
        volume_ = volume;
        if (!queue_) {
            return false;
        }

        const float scaled_volume = volume * (static_cast<float>(driver_->master_volume()) / 100.0f);
        return AudioQueueSetParameter(queue_, kAudioQueueParam_Volume, scaled_volume) == noErr;
    }

    float ios_audio_output_stream::get_volume() const {
        return volume_;
    }

    bool ios_audio_output_stream::current_frame_position(std::uint64_t *pos) {
        if (!pos) {
            return false;
        }

        *pos = submitted_frames_;
        return true;
    }

    ios_audio_input_stream::ios_audio_input_stream(audio_driver *driver, const std::uint32_t sample_rate,
        const std::uint8_t channels, data_callback callback)
        : audio_input_stream(driver, sample_rate, channels)
        , queue_(nullptr)
        , callback_(callback)
        , buffers_()
        , recording_(false)
        , received_frames_(0)
        , frames_per_buffer_(std::max<std::uint32_t>(256, sample_rate / 50))
        , buffer_byte_size_(frames_per_buffer_ * channels * sizeof(std::int16_t)) {
        AudioStreamBasicDescription format = {};
        format.mSampleRate = sample_rate;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
        format.mBytesPerPacket = channels * sizeof(std::int16_t);
        format.mFramesPerPacket = 1;
        format.mBytesPerFrame = channels * sizeof(std::int16_t);
        format.mChannelsPerFrame = channels;
        format.mBitsPerChannel = 16;

        const OSStatus status = AudioQueueNewInput(&format, audio_queue_input_callback, this, nullptr,
            kCFRunLoopCommonModes, 0, &queue_);
        if (status != noErr) {
            LOG_ERROR(DRIVER_AUD, "Failed to create iOS AudioQueue input stream: {}", static_cast<int>(status));
            queue_ = nullptr;
            return;
        }

        buffers_.reserve(3);
        for (int i = 0; i < 3; i++) {
            AudioQueueBufferRef buffer = nullptr;
            if (AudioQueueAllocateBuffer(queue_, buffer_byte_size_, &buffer) == noErr) {
                buffers_.push_back(buffer);
            }
        }
    }

    ios_audio_input_stream::~ios_audio_input_stream() {
        stop();
        if (queue_) {
            AudioQueueDispose(queue_, true);
            queue_ = nullptr;
        }
    }

    bool ios_audio_input_stream::start() {
        if (!queue_) {
            return false;
        }

        if (buffers_.empty()) {
            return false;
        }

        if (recording_) {
            return true;
        }

        if (!configure_audio_session_for_recording()) {
            return false;
        }

        received_frames_ = 0;
        recording_ = true;

        for (AudioQueueBufferRef buffer : buffers_) {
            buffer->mAudioDataByteSize = buffer_byte_size_;
            AudioQueueEnqueueBuffer(queue_, buffer, 0, nullptr);
        }

        if (AudioQueueStart(queue_, nullptr) != noErr) {
            recording_ = false;
            return false;
        }

        return true;
    }

    bool ios_audio_input_stream::stop() {
        if (!queue_) {
            return false;
        }

        if (!recording_) {
            return true;
        }

        recording_ = false;
        AudioQueueStop(queue_, true);
        AudioQueueReset(queue_);
        return true;
    }

    bool ios_audio_input_stream::is_recording() {
        return recording_;
    }

    bool ios_audio_input_stream::current_frame_position(std::uint64_t *pos) {
        if (!pos) {
            return false;
        }

        *pos = received_frames_;
        return true;
    }

    void ios_audio_input_stream::process_input_buffer(AudioQueueRef queue, AudioQueueBufferRef buffer) {
        if (!recording_) {
            return;
        }

        const std::size_t frames_available = buffer->mAudioDataByteSize / (channels_ * sizeof(std::int16_t));
        if ((frames_available > 0) && callback_) {
            received_frames_ += callback_(reinterpret_cast<std::int16_t *>(buffer->mAudioData), frames_available);
        }

        if (recording_) {
            AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
        }
    }
}
