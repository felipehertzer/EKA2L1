/*
 * Copyright (c) 2022 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#if EKA2L1_ENABLE_FFMPEG
#include <drivers/video/backend/ffmpeg/video_player_ffmpeg.h>
#endif
#include <drivers/video/video.h>

namespace eka2l1::drivers {
#if !EKA2L1_ENABLE_FFMPEG
    class video_player_null final : public video_player {
    public:
        bool open_file(const std::string &path) override {
            return false;
        }

        bool open_custom_io(common::ro_stream &stream) override {
            return false;
        }

        void close() override {
        }

        void play(const std::uint64_t *us_range) override {
            if (play_complete_callback_) {
                play_complete_callback_(play_complete_callback_userdata_, -1);
            }
        }

        void pause() override {
        }

        void stop() override {
        }

        std::uint32_t max_volume() const override {
            return 0;
        }

        std::uint32_t volume() const override {
            return 0;
        }

        bool set_volume(const std::uint32_t volume) override {
            return false;
        }

        std::uint64_t duration() const override {
            return 0;
        }

        std::uint64_t position() const override {
            return 0;
        }

        void set_position(const std::uint64_t pos) override {
        }

        eka2l1::vec2 get_video_size() const override {
            return { 0, 0 };
        }

        void set_fps(const float fps) override {
        }

        float get_fps() const override {
            return 0.0f;
        }

        std::uint32_t audio_bitrate() const override {
            return 0;
        }

        std::uint32_t video_bitrate() const override {
            return 0;
        }
    };
#endif

    video_player_instance new_best_video_player(audio_driver *drv) {
#if EKA2L1_ENABLE_FFMPEG
        return std::make_unique<video_player_ffmpeg>(drv);
#else
        return std::make_unique<video_player_null>();
#endif
    }
}
