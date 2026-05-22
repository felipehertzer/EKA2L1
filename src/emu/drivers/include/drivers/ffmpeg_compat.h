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

#include <cerrno>
#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

namespace eka2l1::drivers::ffmpeg_compat {
#if LIBAVUTIL_VERSION_MAJOR >= 58
    using channel_layout = AVChannelLayout;

    inline int channel_count(const AVCodecContext *context) {
        return context->ch_layout.nb_channels;
    }

    inline int channel_count(const AVCodecParameters *parameters) {
        return parameters->ch_layout.nb_channels;
    }

    inline int channel_count(const AVFrame *frame) {
        return frame->ch_layout.nb_channels;
    }

    inline int channel_count(const channel_layout *layout) {
        return layout ? layout->nb_channels : 0;
    }

    inline void set_default_channel_layout(AVCodecContext *context, const int channels) {
        av_channel_layout_uninit(&context->ch_layout);
        av_channel_layout_default(&context->ch_layout, channels);
    }

    inline bool channel_layout_is_valid(const channel_layout *layout) {
        return layout && (layout->nb_channels > 0);
    }

    inline const channel_layout *next_channel_layout(const channel_layout *layout) {
        return layout + 1;
    }

    inline const channel_layout *first_supported_channel_layout(const AVCodec *codec) {
#if LIBAVCODEC_VERSION_MAJOR >= 62
        const void *configs = nullptr;
        int config_count = 0;
        if ((avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_CHANNEL_LAYOUT, 0,
                 &configs, &config_count)
                >= 0)
            && configs && (config_count > 0)) {
            return static_cast<const channel_layout *>(configs);
        }
#endif

        return codec->ch_layouts;
    }

    inline int configure_swr(SwrContext **context, const int out_channels,
        const AVSampleFormat out_format, const int out_rate, const channel_layout *in_layout,
        const int in_channels, const AVSampleFormat in_format, const int in_rate) {
        channel_layout out_layout = {};
        channel_layout input_layout = {};

        av_channel_layout_default(&out_layout, out_channels);
        if (channel_layout_is_valid(in_layout)) {
            av_channel_layout_copy(&input_layout, in_layout);
        } else {
            av_channel_layout_default(&input_layout, in_channels);
        }

        const int result = swr_alloc_set_opts2(context, &out_layout, out_format, out_rate,
            &input_layout, in_format, in_rate, 0, nullptr);

        av_channel_layout_uninit(&input_layout);
        av_channel_layout_uninit(&out_layout);
        return result;
    }

    inline int configure_swr_from_frame(SwrContext **context, const int out_channels,
        const AVSampleFormat out_format, const int out_rate, const AVFrame *frame) {
        return configure_swr(context, out_channels, out_format, out_rate, &frame->ch_layout,
            channel_count(frame), static_cast<AVSampleFormat>(frame->format), frame->sample_rate);
    }

    inline int configure_swr_from_codec_parameters(SwrContext **context, const int out_channels,
        const AVSampleFormat out_format, const int out_rate, const AVCodecParameters *parameters) {
        return configure_swr(context, out_channels, out_format, out_rate, &parameters->ch_layout,
            channel_count(parameters), static_cast<AVSampleFormat>(parameters->format), parameters->sample_rate);
    }
#else
    using channel_layout = std::uint64_t;

    inline std::uint64_t default_channel_layout(const int channels) {
        return (channels == 1) ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    }

    inline int channel_count(const AVCodecContext *context) {
        return context->channels;
    }

    inline int channel_count(const AVCodecParameters *parameters) {
        return parameters->channels;
    }

    inline int channel_count(const AVFrame *frame) {
        return frame->channels;
    }

    inline int channel_count(const channel_layout *layout) {
        return layout ? av_get_channel_layout_nb_channels(*layout) : 0;
    }

    inline void set_default_channel_layout(AVCodecContext *context, const int channels) {
        context->channels = channels;
        context->channel_layout = default_channel_layout(channels);
    }

    inline bool channel_layout_is_valid(const channel_layout *layout) {
        return layout && (*layout != 0);
    }

    inline const channel_layout *next_channel_layout(const channel_layout *layout) {
        return layout + 1;
    }

    inline const channel_layout *first_supported_channel_layout(const AVCodec *codec) {
        return codec->channel_layouts;
    }

    inline int configure_swr(SwrContext **context, const int out_channels,
        const AVSampleFormat out_format, const int out_rate, const channel_layout in_layout,
        const int in_channels, const AVSampleFormat in_format, const int in_rate) {
        const std::uint64_t input_layout = in_layout ? in_layout : default_channel_layout(in_channels);
        *context = swr_alloc_set_opts(nullptr, default_channel_layout(out_channels), out_format, out_rate,
            input_layout, in_format, in_rate, 0, nullptr);

        return *context ? 0 : AVERROR(ENOMEM);
    }

    inline int configure_swr_from_frame(SwrContext **context, const int out_channels,
        const AVSampleFormat out_format, const int out_rate, const AVFrame *frame) {
        return configure_swr(context, out_channels, out_format, out_rate, frame->channel_layout,
            channel_count(frame), static_cast<AVSampleFormat>(frame->format), frame->sample_rate);
    }

    inline int configure_swr_from_codec_parameters(SwrContext **context, const int out_channels,
        const AVSampleFormat out_format, const int out_rate, const AVCodecParameters *parameters) {
        return configure_swr(context, out_channels, out_format, out_rate, parameters->channel_layout,
            channel_count(parameters), static_cast<AVSampleFormat>(parameters->format), parameters->sample_rate);
    }
#endif
}
