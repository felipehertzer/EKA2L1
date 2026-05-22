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

#include "../null/sensor_null.h"

#include <mutex>
#include <vector>

namespace eka2l1::drivers {
    class sensor_driver_ios;

    class sensor_accelerometer_ios final : public sensor_accelerometer_null {
    private:
        sensor_driver_ios *driver_;
        bool listening_;
        std::size_t desired_buffering_count_;
        std::uint32_t active_measure_range_;
        std::uint32_t active_sampling_rate_;
        std::vector<std::uint8_t> events_translated_;
        sensor_data_callback data_callback_;
        std::mutex lock_;

    public:
        explicit sensor_accelerometer_ios(sensor_driver_ios *driver, const sensor_info &info);
        ~sensor_accelerometer_ios() override;

        void push_sample(double x_g, double y_g, double z_g);

        bool get_property(const sensor_property prop, const std::int32_t item_index,
            const std::int32_t array_index, sensor_property_data &data) override;
        bool set_property(const sensor_property_data &data) override;
        bool listen_for_data(std::size_t desired_buffering_count, std::size_t max_buffering_count, std::size_t delay_us) override;
        bool cancel_data_listening() override;
        void receive_data(sensor_data_callback callback) override;
        std::uint32_t data_packet_size() const override;
    };

    class sensor_driver_ios final : public sensor_driver {
    private:
        friend class sensor_accelerometer_ios;

        void *motion_manager_;
        std::vector<sensor_info> infos_;

        void start_accelerometer(sensor_accelerometer_ios *sensor, double interval);
        void stop_accelerometer();
        bool accelerometer_available() const;

    public:
        explicit sensor_driver_ios();
        ~sensor_driver_ios();

        std::vector<sensor_info> queries_active_sensor(const sensor_info &search_info) override;
        std::unique_ptr<sensor> new_sensor_controller(const std::uint32_t id) override;

        bool pause() override;
        bool resume() override;
    };
}
