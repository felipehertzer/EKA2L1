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

#include "sensor_ios.h"

#include <common/log.h>
#include <common/time.h>

#import <CoreMotion/CoreMotion.h>

#include <algorithm>
#include <cstring>

namespace eka2l1::drivers {
    static constexpr double GRAVITY_MS2 = 9.80665;
    static constexpr std::int32_t ACCELEROMETER_SCALE_RANGE_MAX = 127;
    static constexpr std::int32_t ACCELEROMETER_SCALE_RANGE_MIN = -128;
    static const double ACCELEROMETER_MEASURE_RANGE_AVAILABLE[] = {
        19.62, 78.48
    };
    static const std::int32_t SAMPLING_RATE_AVAILABLE[] = {
        10, 40, 50
    };
    static constexpr std::int32_t ACCELEROMETER_MEASURE_RANGE_MAX_OPTION = sizeof(ACCELEROMETER_MEASURE_RANGE_AVAILABLE) / sizeof(double);
    static constexpr std::int32_t SAMPLING_RATE_MAX_OPTION = sizeof(SAMPLING_RATE_AVAILABLE) / sizeof(std::int32_t);

    static CMMotionManager *motion_manager_from(void *manager) {
        return reinterpret_cast<CMMotionManager *>(manager);
    }

    sensor_driver_ios::sensor_driver_ios()
        : motion_manager_(nullptr) {
        motion_manager_ = [[CMMotionManager alloc] init];

        if (accelerometer_available()) {
            sensor_info info;
            info.type_ = SENSOR_TYPE_ACCELEROMETER;
            info.quantity_ = SENSOR_DATA_QUANTITY_ACCELERATION;
            info.data_type_ = SENSOR_DATA_TYPE_ACCELOREMETER_AXIS;
            info.item_size_ = sizeof(sensor_accelerometer_axis_data);
            info.id_ = 1;
            info.name_ = "iOS Accelerometer";
            info.vendor_ = "Apple";
            infos_.push_back(info);
        }
    }

    sensor_driver_ios::~sensor_driver_ios() {
        stop_accelerometer();
#if !__has_feature(objc_arc)
        [motion_manager_from(motion_manager_) release];
#endif
        motion_manager_ = nullptr;
    }

    bool sensor_driver_ios::accelerometer_available() const {
        CMMotionManager *manager = motion_manager_from(motion_manager_);
        return manager && [manager isAccelerometerAvailable];
    }

    void sensor_driver_ios::start_accelerometer(sensor_accelerometer_ios *sensor, double interval) {
        CMMotionManager *manager = motion_manager_from(motion_manager_);
        if (!manager || ![manager isAccelerometerAvailable]) {
            return;
        }

        [manager setAccelerometerUpdateInterval:interval];
        [manager startAccelerometerUpdatesToQueue:[NSOperationQueue mainQueue]
                                      withHandler:^(CMAccelerometerData *data, NSError *error) {
                                          if (error || !data) {
                                              return;
                                          }

                                          sensor->push_sample(data.acceleration.x, data.acceleration.y, data.acceleration.z);
                                      }];
    }

    void sensor_driver_ios::stop_accelerometer() {
        CMMotionManager *manager = motion_manager_from(motion_manager_);
        if (manager && [manager isAccelerometerActive]) {
            [manager stopAccelerometerUpdates];
        }
    }

    std::vector<sensor_info> sensor_driver_ios::queries_active_sensor(const sensor_info &search_info) {
        std::vector<sensor_info> results;
        for (const sensor_info &info : infos_) {
            if (search_info.data_type_ && (search_info.data_type_ != info.data_type_)) {
                continue;
            }
            if (search_info.quantity_ && (search_info.quantity_ != info.quantity_)) {
                continue;
            }
            if (search_info.type_ && (search_info.type_ != info.type_)) {
                continue;
            }

            results.push_back(info);
        }

        return results;
    }

    std::unique_ptr<sensor> sensor_driver_ios::new_sensor_controller(const std::uint32_t id) {
        if ((id == 0) || (id > infos_.size())) {
            return nullptr;
        }

        return std::make_unique<sensor_accelerometer_ios>(this, infos_[id - 1]);
    }

    bool sensor_driver_ios::pause() {
        stop_accelerometer();
        return true;
    }

    bool sensor_driver_ios::resume() {
        return true;
    }

    sensor_accelerometer_ios::sensor_accelerometer_ios(sensor_driver_ios *driver, const sensor_info &info)
        : sensor_accelerometer_null(info)
        , driver_(driver)
        , listening_(false)
        , desired_buffering_count_(1)
        , active_measure_range_(0)
        , active_sampling_rate_(1) {
    }

    sensor_accelerometer_ios::~sensor_accelerometer_ios() {
        cancel_data_listening();
    }

    bool sensor_accelerometer_ios::get_property(const sensor_property prop, const std::int32_t item_index,
        const std::int32_t array_index, sensor_property_data &data) {
        data.property_id_ = prop;

        switch (prop) {
        case SENSOR_PROPERTY_SAMPLE_RATE:
            if (array_index == SENSOR_PROPERTY_ARRAY) {
                data.set_as_array_status(sensor_property_data::DATA_TYPE_INT, SAMPLING_RATE_MAX_OPTION - 1,
                    active_sampling_rate_);
            } else if ((array_index >= 0) && (array_index < SAMPLING_RATE_MAX_OPTION)) {
                data.set_int(SAMPLING_RATE_AVAILABLE[array_index]);
                data.array_index_ = array_index;
            } else {
                return false;
            }
            return true;

        case SENSOR_PROPERTY_DATA_FORMAT:
            data.set_int(SENSOR_DATA_FORMAT_SCALED);
            return true;

        case SENSOR_PROPERTY_SCALED_RANGE:
            data.set_int_range(ACCELEROMETER_SCALE_RANGE_MIN, ACCELEROMETER_SCALE_RANGE_MAX);
            return true;

        case SENSOR_PROPERTY_CHANNEL_UNIT:
            data.set_int(SENSOR_UNIT_MS_PER_S2);
            return true;

        case SENSOR_PROPERTY_SCALE:
            data.set_int(0);
            return true;

        case SENSOR_PROPERTY_MEASURE_RANGE:
            if (array_index == SENSOR_PROPERTY_ARRAY) {
                data.set_as_array_status(sensor_property_data::DATA_TYPE_DOUBLE,
                    ACCELEROMETER_MEASURE_RANGE_MAX_OPTION - 1, active_measure_range_);
            } else if ((array_index >= 0) && (array_index < ACCELEROMETER_MEASURE_RANGE_MAX_OPTION)) {
                data.set_double_range(-ACCELEROMETER_MEASURE_RANGE_AVAILABLE[array_index],
                    ACCELEROMETER_MEASURE_RANGE_AVAILABLE[array_index]);
                data.array_index_ = array_index;
            } else {
                return false;
            }
            return true;

        default:
            return sensor_accelerometer_null::get_property(prop, item_index, array_index, data);
        }
    }

    bool sensor_accelerometer_ios::set_property(const sensor_property_data &data) {
        switch (data.property_id_) {
        case SENSOR_PROPERTY_SAMPLE_RATE:
            if (data.array_index_ != SENSOR_PROPERTY_ARRAY || data.int_value_ < 0 || data.int_value_ >= SAMPLING_RATE_MAX_OPTION) {
                return false;
            }

            active_sampling_rate_ = static_cast<std::uint32_t>(data.int_value_);
            if (listening_) {
                driver_->start_accelerometer(this, 1.0 / SAMPLING_RATE_AVAILABLE[active_sampling_rate_]);
            }
            return true;

        case SENSOR_PROPERTY_MEASURE_RANGE:
            if (data.array_index_ != SENSOR_PROPERTY_ARRAY || data.int_value_ < 0 || data.int_value_ >= ACCELEROMETER_MEASURE_RANGE_MAX_OPTION) {
                return false;
            }

            active_measure_range_ = static_cast<std::uint32_t>(data.int_value_);
            return true;

        default:
            return sensor_accelerometer_null::set_property(data);
        }
    }

    bool sensor_accelerometer_ios::listen_for_data(std::size_t desired_buffering_count,
        std::size_t max_buffering_count, std::size_t delay_us) {
        (void)delay_us;

        if (listening_) {
            return false;
        }

        if (desired_buffering_count > max_buffering_count) {
            return false;
        }

        if (desired_buffering_count == 0) {
            desired_buffering_count = std::max<std::size_t>(max_buffering_count, 1);
        }

        desired_buffering_count_ = desired_buffering_count;
        listening_ = true;
        driver_->start_accelerometer(this, 1.0 / SAMPLING_RATE_AVAILABLE[active_sampling_rate_]);
        return true;
    }

    bool sensor_accelerometer_ios::cancel_data_listening() {
        if (!listening_) {
            return false;
        }

        driver_->stop_accelerometer();
        listening_ = false;

        const std::lock_guard<std::mutex> guard(lock_);
        data_callback_ = nullptr;
        events_translated_.clear();
        return true;
    }

    void sensor_accelerometer_ios::receive_data(sensor_data_callback callback) {
        const std::lock_guard<std::mutex> guard(lock_);
        if (data_callback_) {
            return;
        }

        data_callback_ = callback;
        events_translated_.clear();
    }

    void sensor_accelerometer_ios::push_sample(double x_g, double y_g, double z_g) {
        sensor_data_callback callback;
        std::vector<std::uint8_t> translated;
        std::size_t packet_count = 0;

        {
            const std::lock_guard<std::mutex> guard(lock_);
            if (!data_callback_) {
                return;
            }

            sensor_accelerometer_axis_data axis_data;
            const double measure_range = ACCELEROMETER_MEASURE_RANGE_AVAILABLE[active_measure_range_];

            axis_data.timestamp_ = common::get_current_utc_time_in_microseconds_since_0ad();
            axis_data.axis_x_ = static_cast<std::int32_t>((x_g * GRAVITY_MS2) * ACCELEROMETER_SCALE_RANGE_MAX / measure_range);
            axis_data.axis_y_ = static_cast<std::int32_t>((y_g * GRAVITY_MS2) * ACCELEROMETER_SCALE_RANGE_MAX / measure_range);
            axis_data.axis_z_ = static_cast<std::int32_t>((z_g * GRAVITY_MS2) * ACCELEROMETER_SCALE_RANGE_MAX / measure_range);

            events_translated_.insert(events_translated_.end(), reinterpret_cast<std::uint8_t *>(&axis_data),
                reinterpret_cast<std::uint8_t *>(&axis_data + 1));

            packet_count = events_translated_.size() / sizeof(sensor_accelerometer_axis_data);
            if (packet_count < desired_buffering_count_) {
                return;
            }

            callback = data_callback_;
            data_callback_ = nullptr;
            translated.swap(events_translated_);
        }

        callback(translated, packet_count);
    }

    std::uint32_t sensor_accelerometer_ios::data_packet_size() const {
        return sizeof(sensor_accelerometer_axis_data);
    }
}
