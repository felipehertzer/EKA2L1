/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#pragma once

#include <common/queue.h>
#include <drivers/graphics/backend/graphics_driver_shared.h>
#include <drivers/graphics/buffer.h>
#include <drivers/graphics/fb.h>
#include <drivers/graphics/input_desc.h>
#include <drivers/graphics/shader.h>
#include <drivers/graphics/texture.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eka2l1::drivers {
    class native_graphics_driver;

    class native_texture : public texture {
        vec3 size_;
        texture_format internal_format_;
        texture_format upload_format_;
        texture_data_type data_type_;
        channel_swizzles swizzle_;
        filter_option min_filter_;
        filter_option mag_filter_;
        std::vector<std::uint8_t> pixels_;

    public:
        native_texture();

        bool create(graphics_driver *driver, const int dim, const int miplvl, const vec3 &size,
            const texture_format internal_format, const texture_format format, const texture_data_type data_type,
            void *data, const std::size_t data_size, const std::size_t pixels_per_line = 0,
            const std::uint32_t unpack_alignment = 4) override;

        void bind(graphics_driver *driver, const int binding) override;
        void unbind(graphics_driver *driver) override;
        vec2 get_size() const override;
        texture_format get_format() const override;
        int get_total_dimensions() const override;
        std::uint64_t driver_handle() override;
        void set_filter_minmag(const bool min, const filter_option op) override;
        void set_addressing_mode(const addressing_direction dir, const addressing_option op) override;
        void set_channel_swizzle(channel_swizzles swizz) override;
        void generate_mips() override;
        void set_max_mip_level(const std::uint32_t max_mip) override;
        void update_data(graphics_driver *driver, const int mip_lvl, const vec3 &offset, const vec3 &size,
            const std::size_t byte_width, const texture_format data_format, const texture_data_type data_type,
            const void *data, const std::size_t data_size, const std::uint32_t unpack_alignment) override;
        texture_data_type get_data_type() const override;

        bool read_data(const texture_format dest_format, const texture_data_type dest_type,
            const point &pos, const object_size &size, std::uint8_t *buffer_ptr) const;
        const std::vector<std::uint8_t> &pixels() const;
        std::vector<std::uint8_t> &pixels();
        const channel_swizzles &swizzle() const;
    };

    class native_renderbuffer : public renderbuffer {
        native_texture texture_;

    public:
        bool create(graphics_driver *driver, const vec2 &size, const texture_format format) override;
        void bind(graphics_driver *driver, const int binding) override;
        void unbind(graphics_driver *driver) override;
        vec2 get_size() const override;
        texture_format get_format() const override;
        std::uint64_t driver_handle() override;
        native_texture *texture();
    };

    class native_framebuffer : public framebuffer {
        native_graphics_driver *bound_driver_;
        std::int32_t draw_attachment_;
        std::int32_t read_attachment_;

    public:
        explicit native_framebuffer(const std::vector<drawable *> &color_buffer_list,
            drawable *depth_buffer, drawable *stencil_buffer);

        void bind(graphics_driver *driver, const framebuffer_bind_type type_bind) override;
        void unbind(graphics_driver *driver) override;
        bool set_draw_buffer(const std::int32_t attachment_id) override;
        bool set_read_buffer(const std::int32_t attachment_id) override;
        bool set_depth_stencil_buffer(drawable *depth, drawable *stencil,
            const int depth_face_index, const int stencil_face_index) override;
        std::int32_t set_color_buffer(drawable *tex, const int face_index, const std::int32_t position = -1) override;
        bool blit(const rect &source_rect, const rect &dest_rect, const std::uint32_t flags,
            const filter_option copy_filter) override;
        bool remove_color_buffer(const std::int32_t position) override;
        bool read(const texture_format type, const texture_data_type dest_format,
            const point &pos, const object_size &size, std::uint8_t *buffer_ptr) override;

        native_texture *draw_texture() const;
        native_texture *read_texture() const;
    };

    class native_buffer : public buffer {
        std::vector<std::uint8_t> data_;

    public:
        void bind(graphics_driver *driver) override;
        void unbind(graphics_driver *driver) override;
        bool create(graphics_driver *driver, const void *data, const std::size_t initial_size,
            const buffer_upload_hint use_hint) override;
        void update_data(graphics_driver *driver, const void *data, const std::size_t offset,
            const std::size_t size) override;
    };

    class native_shader_module : public shader_module {
        std::string source_;
        shader_module_type type_;

    public:
        bool create(graphics_driver *driver, const char *data, const std::size_t size,
            const shader_module_type type, std::string *compile_log = nullptr) override;
    };

    class native_shader_program : public shader_program {
    public:
        bool create(graphics_driver *driver, shader_module *vertex_module, shader_module *fragment_module,
            std::string *link_log = nullptr) override;
        bool use(graphics_driver *driver) override;
        std::optional<int> get_uniform_location(const std::string &name) override;
        std::optional<int> get_attrib_location(const std::string &name) override;
    };

    class native_input_descriptors : public input_descriptors {
        std::vector<input_descriptor> descriptors_;

    public:
        bool modify(graphics_driver *drv, input_descriptor *descs, const int count) override;
    };

    class native_graphics_driver : public shared_graphics_driver {
        struct surface_view {
            std::uint8_t *pixels = nullptr;
            int width = 0;
            int height = 0;
        };

        request_queue<command_list> list_queue_;
        window_system_info wsi_;
        bool should_stop_;
        bool initialized_;
        void *sdl_renderer_;
        void *sdl_texture_;
        vec2 surface_size_;
        std::vector<std::uint8_t> surface_pixels_;
        native_framebuffer *bound_read_framebuffer_;
        native_framebuffer *bound_draw_framebuffer_;
        rect clip_rect_;
        bool clipping_enabled_;
        std::string active_upscale_shader_;

        bool recreate_surface();
        bool current_surface(surface_view &surface);
        bool pixel_allowed(const int x, const int y) const;
        void put_pixel(surface_view &surface, const int x, const int y, const std::uint8_t rgba[4]);
        void clear(command &cmd);
        void display(command &cmd);
        void draw_rectangle(command &cmd);
        void draw_bitmap(command &cmd);
        void draw_line(command &cmd);
        void draw_polygon(command &cmd);
        void clip_rect(command &cmd);
        void set_feature(command &cmd);
        void read_framebuffer(command &cmd);

    public:
        explicit native_graphics_driver(const window_system_info &info);
        ~native_graphics_driver() override;

        bool is_initialized() const;
        void run() override;
        void abort() override;
        bool aborted() const override;
        bool is_stricted() const override;
        void update_surface(void *surface) override;
        void update_surface_size(const vec2 &size) override;
        void submit_command_list(command_list &cmd_list) override;
        void set_upscale_shader(const std::string &name) override;
        std::string get_active_upscale_shader() const override;
        bool support_extension(const graphics_driver_extension ext) override;
        bool query_extension_value(const graphics_driver_extension_query query, void *data_ptr) override;
        void bind_swapchain_framebuf() override;
        void set_bound_framebuffer(native_framebuffer *framebuffer, const framebuffer_bind_type type_bind);
        bool blit_framebuffer(native_framebuffer *source, const rect &source_rect, const rect &dest_rect);
        void dispatch(command &cmd) override;
    };
}
