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

#include <drivers/camera/camera.h>
#include <drivers/camera/camera_collection.h>

#include <map>
#include <mutex>
#include <vector>

namespace eka2l1::drivers::camera {
    class instance_ios;

    class collection_ios final : public collection {
    private:
        friend class instance_ios;

        std::vector<void *> devices_;
        std::map<int, instance_ios *> current_reserved_;
        std::mutex reserve_lock_;

    public:
        explicit collection_ios();
        ~collection_ios() override;

        std::uint32_t count() const override;
        std::unique_ptr<instance> make_camera(const std::uint32_t camera_index) override;
    };

    class instance_ios final : public instance {
    private:
        collection_ios *collection_;
        int index_;
        void *device_;
        void *session_;
        void *photo_output_;
        void *video_output_;
        void *video_delegate_;
        void *capture_queue_;

        camera_capture_image_done_callback active_capture_img_callback_;
        camera_capture_image_done_callback active_frame_viewfinder_callback_;
        camera_wants_new_frame_callback wants_new_frame_callback_;
        std::mutex callback_lock_;

        std::uint32_t flash_mode_;
        std::uint32_t stub_exposure_;
        std::uint32_t stub_digital_zoom_;
        std::vector<eka2l1::vec2> output_sizes_;
        eka2l1::vec2 viewfinder_size_;
        frame_format viewfinder_format_;
        bool viewfinder_active_;

        bool ensure_authorized();
        bool ensure_session();
        bool ensure_photo_output();
        bool ensure_video_output(const eka2l1::vec2 &size, const frame_format format);
        void stop_session_if_idle();

    public:
        explicit instance_ios(collection_ios *collection, const int index, void *device);
        ~instance_ios() override;

        bool set_parameter(const parameter_key key, const std::uint32_t value) override;
        bool get_parameter(const parameter_key key, std::uint32_t &value) override;

        std::vector<frame_format> supported_frame_formats() override;
        std::vector<eka2l1::vec2> supported_output_image_sizes(const frame_format frame_format) override;

        bool reserve() override;
        void release() override;

        info get_info() override;

        void capture_image(const std::uint32_t resolution_index, const frame_format format,
            camera_capture_image_done_callback callback) override;
        void receive_viewfinder_feed(const eka2l1::vec2 &size, const frame_format format,
            camera_wants_new_frame_callback new_frame_needed_callback,
            camera_capture_image_done_callback new_frame_come_callback) override;
        void stop_viewfinder_feed() override;

        bool wants_viewfinder_frame();
        void deliver_viewfinder_frame(const void *bgra_data, int width, int height, int stride);
        void deliver_capture_data(const void *data, std::size_t size, const eka2l1::vec2 &size_pixels,
            const frame_format format, int error);
    };
}
