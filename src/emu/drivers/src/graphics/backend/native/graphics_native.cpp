/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#include <drivers/graphics/backend/native/graphics_native.h>

#include <common/log.h>
#include <drivers/itc.h>

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace eka2l1::drivers {
    namespace {
        std::uint8_t expand_4(const std::uint16_t value) {
            return static_cast<std::uint8_t>((value << 4) | value);
        }

        std::uint8_t expand_5(const std::uint16_t value) {
            return static_cast<std::uint8_t>((value << 3) | (value >> 2));
        }

        std::uint8_t expand_6(const std::uint16_t value) {
            return static_cast<std::uint8_t>((value << 2) | (value >> 4));
        }

        int bytes_per_pixel(const texture_format format, const texture_data_type data_type) {
            if (data_type == texture_data_type::ushort_4_4_4_4 || data_type == texture_data_type::ushort_5_6_5 || data_type == texture_data_type::ushort_5_5_5_1) {
                return 2;
            }

            if (data_type == texture_data_type::uint_24_8) {
                return 4;
            }

            switch (format) {
            case texture_format::r:
            case texture_format::r8:
                return 1;
            case texture_format::rgb:
            case texture_format::bgr:
                return 3;
            case texture_format::rgba:
            case texture_format::bgra:
            case texture_format::rgba4:
            case texture_format::rgb5_a1:
            case texture_format::rgb565:
            default:
                return 4;
            }
        }

        native_texture *texture_from_drawable(drawable *drawable_obj) {
            if (!drawable_obj) {
                return nullptr;
            }

            if (drawable_obj->get_drawable_type() == DRAWABLE_TYPE_TEXTURE) {
                return reinterpret_cast<native_texture *>(drawable_obj);
            }

            return reinterpret_cast<native_renderbuffer *>(drawable_obj)->texture();
        }

        const native_texture *texture_from_drawable(const drawable *drawable_obj) {
            return texture_from_drawable(const_cast<drawable *>(drawable_obj));
        }

        void decode_source_pixel(const std::uint8_t *src, const texture_format format,
            const texture_data_type data_type, std::uint8_t rgba[4]) {
            rgba[0] = 0;
            rgba[1] = 0;
            rgba[2] = 0;
            rgba[3] = 255;

            if (!src) {
                return;
            }

            if (data_type == texture_data_type::ushort_4_4_4_4) {
                const std::uint16_t value = src[0] | (static_cast<std::uint16_t>(src[1]) << 8);
                rgba[0] = expand_4((value >> 12) & 0xF);
                rgba[1] = expand_4((value >> 8) & 0xF);
                rgba[2] = expand_4((value >> 4) & 0xF);
                rgba[3] = expand_4(value & 0xF);
                return;
            }

            if (data_type == texture_data_type::ushort_5_6_5) {
                const std::uint16_t value = src[0] | (static_cast<std::uint16_t>(src[1]) << 8);
                rgba[0] = expand_5((value >> 11) & 0x1F);
                rgba[1] = expand_6((value >> 5) & 0x3F);
                rgba[2] = expand_5(value & 0x1F);
                return;
            }

            if (data_type == texture_data_type::ushort_5_5_5_1) {
                const std::uint16_t value = src[0] | (static_cast<std::uint16_t>(src[1]) << 8);
                rgba[0] = expand_5((value >> 11) & 0x1F);
                rgba[1] = expand_5((value >> 6) & 0x1F);
                rgba[2] = expand_5((value >> 1) & 0x1F);
                rgba[3] = (value & 0x1) ? 255 : 0;
                return;
            }

            switch (format) {
            case texture_format::r:
            case texture_format::r8:
                rgba[0] = src[0];
                rgba[1] = src[0];
                rgba[2] = src[0];
                rgba[3] = src[0];
                break;
            case texture_format::rgb:
                rgba[0] = src[0];
                rgba[1] = src[1];
                rgba[2] = src[2];
                break;
            case texture_format::bgr:
                rgba[0] = src[2];
                rgba[1] = src[1];
                rgba[2] = src[0];
                break;
            case texture_format::bgra:
                rgba[0] = src[2];
                rgba[1] = src[1];
                rgba[2] = src[0];
                rgba[3] = src[3];
                break;
            case texture_format::rgba:
            default:
                rgba[0] = src[0];
                rgba[1] = src[1];
                rgba[2] = src[2];
                rgba[3] = src[3];
                break;
            }
        }

        std::uint8_t swizzle_channel(const std::uint8_t rgba[4], const channel_swizzle swizzle) {
            switch (swizzle) {
            case channel_swizzle::red:
                return rgba[0];
            case channel_swizzle::green:
                return rgba[1];
            case channel_swizzle::blue:
                return rgba[2];
            case channel_swizzle::alpha:
                return rgba[3];
            case channel_swizzle::zero:
                return 0;
            case channel_swizzle::one:
                return 255;
            }

            return 0;
        }

        void apply_swizzle(const native_texture &texture, const std::uint8_t src[4], std::uint8_t dst[4]) {
            const channel_swizzles &swizzle = texture.swizzle();
            dst[0] = swizzle_channel(src, swizzle[0]);
            dst[1] = swizzle_channel(src, swizzle[1]);
            dst[2] = swizzle_channel(src, swizzle[2]);
            dst[3] = swizzle_channel(src, swizzle[3]);
        }

        std::uint8_t clamp_u8(const float value) {
            return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f));
        }
    }

    native_texture::native_texture()
        : size_(0, 0, 0)
        , internal_format_(texture_format::none)
        , upload_format_(texture_format::none)
        , data_type_(texture_data_type::ubyte)
        , swizzle_({ channel_swizzle::red, channel_swizzle::green, channel_swizzle::blue, channel_swizzle::alpha })
        , min_filter_(filter_option::linear)
        , mag_filter_(filter_option::linear) {
    }

    bool native_texture::create(graphics_driver *driver, const int dim, const int miplvl, const vec3 &size,
        const texture_format internal_format, const texture_format format, const texture_data_type data_type,
        void *data, const std::size_t data_size, const std::size_t pixels_per_line, const std::uint32_t unpack_alignment) {
        (void)driver;
        (void)miplvl;
        (void)unpack_alignment;

        if (dim != 2 || size.x <= 0 || size.y <= 0) {
            return false;
        }

        size_ = size;
        internal_format_ = internal_format;
        upload_format_ = format;
        data_type_ = data_type;
        pixels_.assign(static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y) * 4, 0);

        if (data && data_size) {
            update_data(driver, 0, vec3(0, 0, 0), vec3(size.x, size.y, 1), pixels_per_line,
                format, data_type, data, data_size, unpack_alignment);
        }

        return true;
    }

    void native_texture::bind(graphics_driver *driver, const int binding) {
        (void)driver;
        (void)binding;
    }

    void native_texture::unbind(graphics_driver *driver) {
        (void)driver;
    }

    vec2 native_texture::get_size() const {
        return vec2(size_.x, size_.y);
    }

    texture_format native_texture::get_format() const {
        return internal_format_;
    }

    int native_texture::get_total_dimensions() const {
        return 2;
    }

    std::uint64_t native_texture::driver_handle() {
        return reinterpret_cast<std::uint64_t>(this);
    }

    void native_texture::set_filter_minmag(const bool min, const filter_option op) {
        if (min) {
            min_filter_ = op;
        } else {
            mag_filter_ = op;
        }
    }

    void native_texture::set_addressing_mode(const addressing_direction dir, const addressing_option op) {
        (void)dir;
        (void)op;
    }

    void native_texture::set_channel_swizzle(channel_swizzles swizz) {
        swizzle_ = swizz;
    }

    void native_texture::generate_mips() {
    }

    void native_texture::set_max_mip_level(const std::uint32_t max_mip) {
        (void)max_mip;
    }

    void native_texture::update_data(graphics_driver *driver, const int mip_lvl, const vec3 &offset, const vec3 &size,
        const std::size_t pixels_per_line, const texture_format data_format, const texture_data_type data_type,
        const void *data, const std::size_t data_size, const std::uint32_t unpack_alignment) {
        (void)driver;
        (void)mip_lvl;
        (void)unpack_alignment;

        if (!data || pixels_.empty()) {
            return;
        }

        const int source_bpp = bytes_per_pixel(data_format, data_type);
        const int source_stride_pixels = pixels_per_line ? static_cast<int>(pixels_per_line) : size.x;
        const std::uint8_t *source = reinterpret_cast<const std::uint8_t *>(data);

        for (int y = 0; y < size.y; y++) {
            const int dst_y = offset.y + y;
            if (dst_y < 0 || dst_y >= size_.y) {
                continue;
            }

            for (int x = 0; x < size.x; x++) {
                const int dst_x = offset.x + x;
                if (dst_x < 0 || dst_x >= size_.x) {
                    continue;
                }

                const std::size_t source_offset = (static_cast<std::size_t>(y) * source_stride_pixels + x) * source_bpp;
                if (source_offset + source_bpp > data_size) {
                    continue;
                }

                std::uint8_t rgba[4];
                decode_source_pixel(source + source_offset, data_format, data_type, rgba);
                const std::size_t dst_offset = (static_cast<std::size_t>(dst_y) * size_.x + dst_x) * 4;
                std::copy(rgba, rgba + 4, pixels_.begin() + static_cast<std::ptrdiff_t>(dst_offset));
            }
        }
    }

    texture_data_type native_texture::get_data_type() const {
        return data_type_;
    }

    bool native_texture::read_data(const texture_format dest_format, const texture_data_type dest_type,
        const point &pos, const object_size &size, std::uint8_t *buffer_ptr) const {
        if (!buffer_ptr || pixels_.empty()) {
            return false;
        }

        const int dest_bpp = bytes_per_pixel(dest_format, dest_type);
        for (int y = 0; y < size.y; y++) {
            const int src_y = pos.y + y;
            if (src_y < 0 || src_y >= size_.y) {
                continue;
            }

            for (int x = 0; x < size.x; x++) {
                const int src_x = pos.x + x;
                if (src_x < 0 || src_x >= size_.x) {
                    continue;
                }

                const std::uint8_t *rgba = pixels_.data() + (static_cast<std::size_t>(src_y) * size_.x + src_x) * 4;
                std::uint8_t *dst = buffer_ptr + (static_cast<std::size_t>(y) * size.x + x) * dest_bpp;

                if (dest_type == texture_data_type::ushort_5_6_5) {
                    const std::uint16_t value = static_cast<std::uint16_t>(((rgba[0] >> 3) << 11) | ((rgba[1] >> 2) << 5) | (rgba[2] >> 3));
                    dst[0] = static_cast<std::uint8_t>(value & 0xFF);
                    dst[1] = static_cast<std::uint8_t>(value >> 8);
                } else if (dest_type == texture_data_type::ushort_4_4_4_4) {
                    const std::uint16_t value = static_cast<std::uint16_t>(((rgba[0] >> 4) << 12) | ((rgba[1] >> 4) << 8) | ((rgba[2] >> 4) << 4) | (rgba[3] >> 4));
                    dst[0] = static_cast<std::uint8_t>(value & 0xFF);
                    dst[1] = static_cast<std::uint8_t>(value >> 8);
                } else {
                    switch (dest_format) {
                    case texture_format::r:
                    case texture_format::r8:
                        dst[0] = rgba[0];
                        break;
                    case texture_format::rgb:
                        dst[0] = rgba[0];
                        dst[1] = rgba[1];
                        dst[2] = rgba[2];
                        break;
                    case texture_format::bgr:
                        dst[0] = rgba[2];
                        dst[1] = rgba[1];
                        dst[2] = rgba[0];
                        break;
                    case texture_format::bgra:
                        dst[0] = rgba[2];
                        dst[1] = rgba[1];
                        dst[2] = rgba[0];
                        dst[3] = rgba[3];
                        break;
                    case texture_format::rgba:
                    default:
                        dst[0] = rgba[0];
                        dst[1] = rgba[1];
                        dst[2] = rgba[2];
                        dst[3] = rgba[3];
                        break;
                    }
                }
            }
        }

        return true;
    }

    const std::vector<std::uint8_t> &native_texture::pixels() const {
        return pixels_;
    }

    std::vector<std::uint8_t> &native_texture::pixels() {
        return pixels_;
    }

    const channel_swizzles &native_texture::swizzle() const {
        return swizzle_;
    }

    bool native_renderbuffer::create(graphics_driver *driver, const vec2 &size, const texture_format format) {
        return texture_.create(driver, 2, 0, vec3(size.x, size.y, 1), format, texture_format::rgba,
            texture_data_type::ubyte, nullptr, 0);
    }

    void native_renderbuffer::bind(graphics_driver *driver, const int binding) {
        texture_.bind(driver, binding);
    }

    void native_renderbuffer::unbind(graphics_driver *driver) {
        texture_.unbind(driver);
    }

    vec2 native_renderbuffer::get_size() const {
        return texture_.get_size();
    }

    texture_format native_renderbuffer::get_format() const {
        return texture_.get_format();
    }

    std::uint64_t native_renderbuffer::driver_handle() {
        return texture_.driver_handle();
    }

    native_texture *native_renderbuffer::texture() {
        return &texture_;
    }

    native_framebuffer::native_framebuffer(const std::vector<drawable *> &color_buffer_list,
        drawable *depth_buffer, drawable *stencil_buffer)
        : framebuffer(color_buffer_list, depth_buffer, stencil_buffer)
        , bound_driver_(nullptr)
        , draw_attachment_(0)
        , read_attachment_(0) {
    }

    void native_framebuffer::bind(graphics_driver *driver, const framebuffer_bind_type type_bind) {
        bound_driver_ = reinterpret_cast<native_graphics_driver *>(driver);
        if (bound_driver_) {
            bound_driver_->set_bound_framebuffer(this, type_bind);
        }
    }

    void native_framebuffer::unbind(graphics_driver *driver) {
        native_graphics_driver *target_driver = driver ? reinterpret_cast<native_graphics_driver *>(driver) : bound_driver_;
        if (target_driver) {
            target_driver->set_bound_framebuffer(nullptr, framebuffer_bind_read_draw);
        }
        bound_driver_ = nullptr;
    }

    bool native_framebuffer::set_draw_buffer(const std::int32_t attachment_id) {
        if (!is_attachment_id_valid(attachment_id)) {
            return false;
        }

        draw_attachment_ = attachment_id;
        return true;
    }

    bool native_framebuffer::set_read_buffer(const std::int32_t attachment_id) {
        if (!is_attachment_id_valid(attachment_id)) {
            return false;
        }

        read_attachment_ = attachment_id;
        return true;
    }

    bool native_framebuffer::set_depth_stencil_buffer(drawable *depth, drawable *stencil,
        const int depth_face_index, const int stencil_face_index) {
        (void)depth_face_index;
        (void)stencil_face_index;
        depth_buffer = depth;
        stencil_buffer = stencil;
        return true;
    }

    std::int32_t native_framebuffer::set_color_buffer(drawable *tex, const int face_index, const std::int32_t position) {
        (void)face_index;
        if (position >= 0) {
            if (static_cast<std::size_t>(position) >= color_buffers.size()) {
                color_buffers.resize(static_cast<std::size_t>(position) + 1, nullptr);
            }

            color_buffers[static_cast<std::size_t>(position)] = tex;
            return position;
        }

        color_buffers.push_back(tex);
        return static_cast<std::int32_t>(color_buffers.size() - 1);
    }

    bool native_framebuffer::blit(const rect &source_rect, const rect &dest_rect, const std::uint32_t flags,
        const filter_option copy_filter) {
        (void)flags;
        (void)copy_filter;
        return bound_driver_ && bound_driver_->blit_framebuffer(this, source_rect, dest_rect);
    }

    bool native_framebuffer::remove_color_buffer(const std::int32_t position) {
        if (!is_attachment_id_valid(position)) {
            return false;
        }

        color_buffers[static_cast<std::size_t>(position)] = nullptr;
        return true;
    }

    bool native_framebuffer::read(const texture_format type, const texture_data_type dest_format,
        const point &pos, const object_size &size, std::uint8_t *buffer_ptr) {
        const native_texture *texture = read_texture();
        return texture && texture->read_data(type, dest_format, pos, size, buffer_ptr);
    }

    native_texture *native_framebuffer::draw_texture() const {
        if (!is_attachment_id_valid(draw_attachment_)) {
            return nullptr;
        }

        return texture_from_drawable(color_buffers[static_cast<std::size_t>(draw_attachment_)]);
    }

    native_texture *native_framebuffer::read_texture() const {
        if (!is_attachment_id_valid(read_attachment_)) {
            return nullptr;
        }

        return texture_from_drawable(color_buffers[static_cast<std::size_t>(read_attachment_)]);
    }

    void native_buffer::bind(graphics_driver *driver) {
        (void)driver;
    }

    void native_buffer::unbind(graphics_driver *driver) {
        (void)driver;
    }

    bool native_buffer::create(graphics_driver *driver, const void *data, const std::size_t initial_size,
        const buffer_upload_hint use_hint) {
        (void)driver;
        (void)use_hint;
        data_.assign(initial_size, 0);
        if (data && initial_size) {
            std::memcpy(data_.data(), data, initial_size);
        }
        return true;
    }

    void native_buffer::update_data(graphics_driver *driver, const void *data, const std::size_t offset,
        const std::size_t size) {
        (void)driver;
        if (!data || !size) {
            return;
        }

        if (offset + size > data_.size()) {
            data_.resize(offset + size);
        }

        std::memcpy(data_.data() + offset, data, size);
    }

    bool native_shader_module::create(graphics_driver *driver, const char *data, const std::size_t size,
        const shader_module_type type, std::string *compile_log) {
        (void)driver;
        if (data && size) {
            source_.assign(data, data + size);
        }
        type_ = type;
        if (compile_log) {
            compile_log->clear();
        }
        return true;
    }

    bool native_shader_program::create(graphics_driver *driver, shader_module *vertex_module,
        shader_module *fragment_module, std::string *link_log) {
        (void)driver;
        (void)vertex_module;
        (void)fragment_module;
        if (link_log) {
            link_log->clear();
        }
        return true;
    }

    bool native_shader_program::use(graphics_driver *driver) {
        (void)driver;
        return true;
    }

    std::optional<int> native_shader_program::get_uniform_location(const std::string &name) {
        (void)name;
        return std::nullopt;
    }

    std::optional<int> native_shader_program::get_attrib_location(const std::string &name) {
        (void)name;
        return std::nullopt;
    }

    bool native_input_descriptors::modify(graphics_driver *drv, input_descriptor *descs, const int count) {
        (void)drv;
        descriptors_.clear();
        if (descs && count > 0) {
            descriptors_.assign(descs, descs + count);
        }
        return true;
    }

    native_graphics_driver::native_graphics_driver(const window_system_info &info)
        : shared_graphics_driver(graphic_api::native)
        , wsi_(info)
        , should_stop_(false)
        , initialized_(true)
        , sdl_renderer_(nullptr)
        , sdl_texture_(nullptr)
        , surface_size_(static_cast<int>(info.surface_width), static_cast<int>(info.surface_height))
        , bound_read_framebuffer_(nullptr)
        , bound_draw_framebuffer_(nullptr)
        , clip_rect_(point(0, 0), object_size(0, 0))
        , clipping_enabled_(false)
        , active_upscale_shader_("Default") {
        list_queue_.max_pending_count_ = 128;
        if (surface_size_.x <= 0 || surface_size_.y <= 0) {
            surface_size_ = vec2(960, 544);
        }
        swapchain_size = surface_size_;
        current_fb_width = surface_size_.x;
        current_fb_height = surface_size_.y;
        recreate_surface();
    }

    native_graphics_driver::~native_graphics_driver() {
        abort();
        if (sdl_texture_) {
            SDL_DestroyTexture(reinterpret_cast<SDL_Texture *>(sdl_texture_));
            sdl_texture_ = nullptr;
        }
        if (sdl_renderer_) {
            SDL_DestroyRenderer(reinterpret_cast<SDL_Renderer *>(sdl_renderer_));
            sdl_renderer_ = nullptr;
        }
    }

    bool native_graphics_driver::is_initialized() const {
        return initialized_;
    }

    bool native_graphics_driver::recreate_surface() {
        if (surface_size_.x <= 0 || surface_size_.y <= 0) {
            return false;
        }

        surface_pixels_.assign(static_cast<std::size_t>(surface_size_.x) * surface_size_.y * 4, 0);

        if (!wsi_.render_surface) {
            return true;
        }

        if (!sdl_renderer_) {
            SDL_Renderer *renderer = SDL_CreateRenderer(reinterpret_cast<SDL_Window *>(wsi_.render_surface), -1,
                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (!renderer) {
                renderer = SDL_CreateRenderer(reinterpret_cast<SDL_Window *>(wsi_.render_surface), -1, SDL_RENDERER_SOFTWARE);
            }
            sdl_renderer_ = renderer;
        }

        if (sdl_texture_) {
            SDL_DestroyTexture(reinterpret_cast<SDL_Texture *>(sdl_texture_));
            sdl_texture_ = nullptr;
        }

        if (sdl_renderer_) {
            sdl_texture_ = SDL_CreateTexture(reinterpret_cast<SDL_Renderer *>(sdl_renderer_), SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_STREAMING, surface_size_.x, surface_size_.y);
        }

        return true;
    }

    void native_graphics_driver::run() {
        while (!should_stop_) {
            std::optional<command_list> list = list_queue_.pop();
            if (!list) {
                break;
            }

            for (std::size_t i = 0; i < list->size_; i++) {
                dispatch(list->base_[i]);
            }

            delete[] list->base_;
        }
    }

    void native_graphics_driver::abort() {
        should_stop_ = true;
        list_queue_.abort();
    }

    bool native_graphics_driver::aborted() const {
        return should_stop_;
    }

    bool native_graphics_driver::is_stricted() const {
        return false;
    }

    void native_graphics_driver::update_surface(void *surface) {
        wsi_.render_surface = surface;
        if (sdl_texture_) {
            SDL_DestroyTexture(reinterpret_cast<SDL_Texture *>(sdl_texture_));
            sdl_texture_ = nullptr;
        }
        if (sdl_renderer_) {
            SDL_DestroyRenderer(reinterpret_cast<SDL_Renderer *>(sdl_renderer_));
            sdl_renderer_ = nullptr;
        }
        recreate_surface();
    }

    void native_graphics_driver::update_surface_size(const vec2 &size) {
        if (size.x <= 0 || size.y <= 0) {
            return;
        }

        surface_size_ = size;
        swapchain_size = size;
        current_fb_width = size.x;
        current_fb_height = size.y;
        recreate_surface();
    }

    void native_graphics_driver::submit_command_list(command_list &cmd_list) {
        if ((cmd_list.size_ == 0) || !cmd_list.base_ || should_stop_) {
            delete[] cmd_list.base_;
            return;
        }

        list_queue_.push(cmd_list);
    }

    void native_graphics_driver::set_upscale_shader(const std::string &name) {
        active_upscale_shader_ = name.empty() ? "Default" : name;
    }

    std::string native_graphics_driver::get_active_upscale_shader() const {
        return active_upscale_shader_;
    }

    bool native_graphics_driver::support_extension(const graphics_driver_extension ext) {
        return ext == graphics_driver_extension_float_precision_qualifier;
    }

    bool native_graphics_driver::query_extension_value(const graphics_driver_extension_query query, void *data_ptr) {
        (void)query;
        (void)data_ptr;
        return false;
    }

    void native_graphics_driver::bind_swapchain_framebuf() {
        bound_read_framebuffer_ = nullptr;
        bound_draw_framebuffer_ = nullptr;
        current_fb_width = surface_size_.x;
        current_fb_height = surface_size_.y;
    }

    void native_graphics_driver::set_bound_framebuffer(native_framebuffer *framebuffer, const framebuffer_bind_type type_bind) {
        if ((type_bind & framebuffer_bind_read) != 0) {
            bound_read_framebuffer_ = framebuffer;
        }
        if ((type_bind & framebuffer_bind_draw) != 0) {
            bound_draw_framebuffer_ = framebuffer;
        }
    }

    bool native_graphics_driver::current_surface(surface_view &surface) {
        if (bound_draw_framebuffer_) {
            native_texture *texture = bound_draw_framebuffer_->draw_texture();
            if (!texture) {
                return false;
            }
            surface.pixels = texture->pixels().data();
            surface.width = texture->get_size().x;
            surface.height = texture->get_size().y;
            return surface.pixels != nullptr;
        }

        surface.pixels = surface_pixels_.data();
        surface.width = surface_size_.x;
        surface.height = surface_size_.y;
        return surface.pixels != nullptr;
    }

    bool native_graphics_driver::pixel_allowed(const int x, const int y) const {
        if (!clipping_enabled_) {
            return true;
        }

        return x >= clip_rect_.top.x && y >= clip_rect_.top.y && x < (clip_rect_.top.x + clip_rect_.size.x) && y < (clip_rect_.top.y + clip_rect_.size.y);
    }

    void native_graphics_driver::put_pixel(surface_view &surface, const int x, const int y, const std::uint8_t rgba[4]) {
        if (!surface.pixels || x < 0 || y < 0 || x >= surface.width || y >= surface.height || !pixel_allowed(x, y)) {
            return;
        }

        std::uint8_t *dst = surface.pixels + (static_cast<std::size_t>(y) * surface.width + x) * 4;
        const std::uint32_t alpha = rgba[3];
        if (alpha == 255) {
            std::copy(rgba, rgba + 4, dst);
            return;
        }
        if (alpha == 0) {
            return;
        }

        const std::uint32_t inverse_alpha = 255 - alpha;
        dst[0] = static_cast<std::uint8_t>((rgba[0] * alpha + dst[0] * inverse_alpha) / 255);
        dst[1] = static_cast<std::uint8_t>((rgba[1] * alpha + dst[1] * inverse_alpha) / 255);
        dst[2] = static_cast<std::uint8_t>((rgba[2] * alpha + dst[2] * inverse_alpha) / 255);
        dst[3] = 255;
    }

    void native_graphics_driver::clear(command &cmd) {
        surface_view surface;
        if (!current_surface(surface)) {
            finish(cmd.status_, -1);
            return;
        }

        float clear_values[6];
        unpack_to_two_floats(cmd.data_[0], clear_values[0], clear_values[1]);
        unpack_to_two_floats(cmd.data_[1], clear_values[2], clear_values[3]);
        unpack_to_two_floats(cmd.data_[2], clear_values[4], clear_values[5]);

        const std::uint8_t clear_bits = static_cast<std::uint8_t>(cmd.data_[3]);
        if ((clear_bits & draw_buffer_bit_color_buffer) == 0) {
            finish(cmd.status_, 0);
            return;
        }

        const std::uint8_t rgba[4] = {
            clamp_u8(clear_values[0]),
            clamp_u8(clear_values[1]),
            clamp_u8(clear_values[2]),
            clamp_u8(clear_values[3])
        };

        for (int y = 0; y < surface.height; y++) {
            for (int x = 0; x < surface.width; x++) {
                put_pixel(surface, x, y, rgba);
            }
        }

        finish(cmd.status_, 0);
    }

    void native_graphics_driver::display(command &cmd) {
        if (disp_hook_) {
            disp_hook_();
        }

        SDL_Renderer *renderer = reinterpret_cast<SDL_Renderer *>(sdl_renderer_);
        SDL_Texture *texture = reinterpret_cast<SDL_Texture *>(sdl_texture_);
        if (renderer && texture && !surface_pixels_.empty()) {
            SDL_UpdateTexture(texture, nullptr, surface_pixels_.data(), surface_size_.x * 4);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        }

        finish(cmd.status_, 0);
    }

    void native_graphics_driver::draw_rectangle(command &cmd) {
        surface_view surface;
        if (!current_surface(surface)) {
            return;
        }

        rect target_rect;
        unpack_u64_to_2u32(cmd.data_[0], target_rect.top.x, target_rect.top.y);
        unpack_u64_to_2u32(cmd.data_[1], target_rect.size.x, target_rect.size.y);

        const std::uint8_t rgba[4] = {
            clamp_u8(brush_color[0]),
            clamp_u8(brush_color[1]),
            clamp_u8(brush_color[2]),
            clamp_u8(brush_color[3])
        };

        for (int y = target_rect.top.y; y < target_rect.top.y + target_rect.size.y; y++) {
            for (int x = target_rect.top.x; x < target_rect.top.x + target_rect.size.x; x++) {
                put_pixel(surface, x, y, rgba);
            }
        }
    }

    void native_graphics_driver::draw_bitmap(command &cmd) {
        bitmap *source_bitmap = get_bitmap(static_cast<drivers::handle>(cmd.data_[0]));
        if (!source_bitmap || !source_bitmap->tex) {
            return;
        }

        native_texture *source_texture = reinterpret_cast<native_texture *>(source_bitmap->tex.get());
        native_texture *mask_texture = nullptr;
        if (cmd.data_[1]) {
            bitmap *mask_bitmap = get_bitmap(static_cast<drivers::handle>(cmd.data_[1]));
            if (mask_bitmap && mask_bitmap->tex) {
                mask_texture = reinterpret_cast<native_texture *>(mask_bitmap->tex.get());
            }
        }

        surface_view surface;
        if (!current_surface(surface)) {
            return;
        }

        rect dest_rect;
        rect source_rect;
        unpack_u64_to_2u32(cmd.data_[2], dest_rect.top.x, dest_rect.top.y);
        unpack_u64_to_2u32(cmd.data_[3], dest_rect.size.x, dest_rect.size.y);
        unpack_u64_to_2u32(cmd.data_[4], source_rect.top.x, source_rect.top.y);
        unpack_u64_to_2u32(cmd.data_[5], source_rect.size.x, source_rect.size.y);
        const std::uint32_t flags = static_cast<std::uint32_t>(cmd.data_[7] >> 32);

        if (dest_rect.size.x <= 0 || dest_rect.size.y <= 0 || source_rect.size.x <= 0 || source_rect.size.y <= 0) {
            return;
        }

        const vec2 source_size = source_texture->get_size();
        const std::vector<std::uint8_t> &source_pixels = source_texture->pixels();
        const std::vector<std::uint8_t> *mask_pixels = mask_texture ? &mask_texture->pixels() : nullptr;

        for (int y = 0; y < dest_rect.size.y; y++) {
            int src_y = source_rect.top.y + (y * source_rect.size.y / dest_rect.size.y);
            if ((flags & bitmap_draw_flag_flip) != 0) {
                src_y = source_rect.top.y + source_rect.size.y - 1 - (y * source_rect.size.y / dest_rect.size.y);
            }
            if (src_y < 0 || src_y >= source_size.y) {
                continue;
            }

            for (int x = 0; x < dest_rect.size.x; x++) {
                const int src_x = source_rect.top.x + (x * source_rect.size.x / dest_rect.size.x);
                if (src_x < 0 || src_x >= source_size.x) {
                    continue;
                }

                const std::uint8_t *src_rgba = source_pixels.data() + (static_cast<std::size_t>(src_y) * source_size.x + src_x) * 4;
                std::uint8_t rgba[4];
                apply_swizzle(*source_texture, src_rgba, rgba);

                if (mask_pixels && mask_texture) {
                    const vec2 mask_size = mask_texture->get_size();
                    if (src_x < mask_size.x && src_y < mask_size.y) {
                        const std::uint8_t *mask = mask_pixels->data() + (static_cast<std::size_t>(src_y) * mask_size.x + src_x) * 4;
                        rgba[3] = (flags & bitmap_draw_flag_invert_mask) ? static_cast<std::uint8_t>(255 - mask[0]) : mask[0];
                    }
                }

                put_pixel(surface, dest_rect.top.x + x, dest_rect.top.y + y, rgba);
            }
        }
    }

    void native_graphics_driver::draw_line(command &cmd) {
        surface_view surface;
        if (!current_surface(surface)) {
            return;
        }

        point start;
        point end;
        unpack_u64_to_2u32(cmd.data_[0], start.x, start.y);
        unpack_u64_to_2u32(cmd.data_[1], end.x, end.y);

        const std::uint8_t rgba[4] = {
            clamp_u8(brush_color[0]),
            clamp_u8(brush_color[1]),
            clamp_u8(brush_color[2]),
            clamp_u8(brush_color[3])
        };

        int x0 = start.x;
        int y0 = start.y;
        const int x1 = end.x;
        const int y1 = end.y;
        const int dx = std::abs(x1 - x0);
        const int sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1 - y0);
        const int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;

        while (true) {
            put_pixel(surface, x0, y0, rgba);
            if (x0 == x1 && y0 == y1) {
                break;
            }
            const int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

    void native_graphics_driver::draw_polygon(command &cmd) {
        const std::size_t point_count = static_cast<std::size_t>(cmd.data_[0]);
        point *point_list = reinterpret_cast<point *>(cmd.data_[1]);
        if (!point_list || point_count < 2) {
            delete[] point_list;
            return;
        }

        for (std::size_t i = 0; i + 1 < point_count; i++) {
            command line_cmd;
            line_cmd.opcode_ = graphics_driver_draw_line;
            line_cmd.data_[0] = PACK_2U32_TO_U64(point_list[i].x, point_list[i].y);
            line_cmd.data_[1] = PACK_2U32_TO_U64(point_list[i + 1].x, point_list[i + 1].y);
            draw_line(line_cmd);
        }

        delete[] point_list;
    }

    void native_graphics_driver::clip_rect(command &cmd) {
        unpack_u64_to_2u32(cmd.data_[0], clip_rect_.top.x, clip_rect_.top.y);
        unpack_u64_to_2u32(cmd.data_[1], clip_rect_.size.x, clip_rect_.size.y);
    }

    void native_graphics_driver::set_feature(command &cmd) {
        graphics_feature feature = graphics_feature::clipping;
        bool enabled = false;
        unpack_u64_to_2u32(cmd.data_[0], feature, enabled);
        if (feature == graphics_feature::clipping) {
            clipping_enabled_ = enabled;
        }
    }

    void native_graphics_driver::read_framebuffer(command &cmd) {
        const drivers::handle handle = static_cast<drivers::handle>(cmd.data_[0]);
        const texture_format target_format = static_cast<texture_format>(static_cast<std::uint32_t>(cmd.data_[1]));
        const texture_data_type target_data_type = static_cast<texture_data_type>(static_cast<std::uint32_t>(cmd.data_[1] >> 32));

        point pos;
        object_size size;
        unpack_u64_to_2u32(cmd.data_[2], pos.x, pos.y);
        unpack_u64_to_2u32(cmd.data_[3], size.x, size.y);
        std::uint8_t *ptr = reinterpret_cast<std::uint8_t *>(cmd.data_[4]);
        if (!ptr) {
            finish(cmd.status_, 0);
            return;
        }

        if (handle != 0) {
            native_framebuffer *framebuffer = reinterpret_cast<native_framebuffer *>(get_graphics_object(handle));
            finish(cmd.status_, framebuffer && framebuffer->read(target_format, target_data_type, pos, size, ptr));
            return;
        }

        native_texture surface_texture;
        surface_texture.create(this, 2, 0, vec3(surface_size_.x, surface_size_.y, 1), texture_format::rgba,
            texture_format::rgba, texture_data_type::ubyte, surface_pixels_.data(), surface_pixels_.size());
        finish(cmd.status_, surface_texture.read_data(target_format, target_data_type, pos, size, ptr));
    }

    bool native_graphics_driver::blit_framebuffer(native_framebuffer *source, const rect &source_rect, const rect &dest_rect) {
        if (!source || !bound_draw_framebuffer_) {
            return false;
        }

        native_texture *source_texture = source->read_texture();
        native_texture *dest_texture = bound_draw_framebuffer_->draw_texture();
        if (!source_texture || !dest_texture) {
            return false;
        }

        surface_view surface;
        surface.pixels = dest_texture->pixels().data();
        surface.width = dest_texture->get_size().x;
        surface.height = dest_texture->get_size().y;

        const std::vector<std::uint8_t> &source_pixels = source_texture->pixels();
        const vec2 source_size = source_texture->get_size();

        for (int y = 0; y < dest_rect.size.y; y++) {
            const int src_y = source_rect.top.y + (y * source_rect.size.y / std::max(1, dest_rect.size.y));
            if (src_y < 0 || src_y >= source_size.y) {
                continue;
            }

            for (int x = 0; x < dest_rect.size.x; x++) {
                const int src_x = source_rect.top.x + (x * source_rect.size.x / std::max(1, dest_rect.size.x));
                if (src_x < 0 || src_x >= source_size.x) {
                    continue;
                }

                const std::uint8_t *rgba = source_pixels.data() + (static_cast<std::size_t>(src_y) * source_size.x + src_x) * 4;
                put_pixel(surface, dest_rect.top.x + x, dest_rect.top.y + y, rgba);
            }
        }

        return true;
    }

    void native_graphics_driver::dispatch(command &cmd) {
        switch (cmd.opcode_) {
        case graphics_driver_display:
            display(cmd);
            break;
        case graphics_driver_clear:
            clear(cmd);
            break;
        case graphics_driver_draw_rectangle:
            draw_rectangle(cmd);
            break;
        case graphics_driver_draw_bitmap:
            draw_bitmap(cmd);
            break;
        case graphics_driver_draw_line:
            draw_line(cmd);
            break;
        case graphics_driver_draw_polygon:
            draw_polygon(cmd);
            break;
        case graphics_driver_clip_rect:
        case graphics_driver_clip_bitmap_rect:
            clip_rect(cmd);
            break;
        case graphics_driver_clip_region: {
            const std::size_t rect_count = static_cast<std::size_t>(cmd.data_[0]);
            rect *rects = reinterpret_cast<rect *>(cmd.data_[1]);
            if (rects && rect_count) {
                clip_rect_ = rects[0];
                for (std::size_t i = 1; i < rect_count; i++) {
                    const int left = std::min(clip_rect_.top.x, rects[i].top.x);
                    const int top = std::min(clip_rect_.top.y, rects[i].top.y);
                    const int right = std::max(clip_rect_.top.x + clip_rect_.size.x, rects[i].top.x + rects[i].size.x);
                    const int bottom = std::max(clip_rect_.top.y + clip_rect_.size.y, rects[i].top.y + rects[i].size.y);
                    clip_rect_ = rect(point(left, top), object_size(right - left, bottom - top));
                }
            }
            delete[] rects;
            break;
        }
        case graphics_driver_set_feature:
            set_feature(cmd);
            break;
        case graphics_driver_set_viewport:
        case graphics_driver_set_bitmap_viewport: {
            rect viewport;
            unpack_u64_to_2u32(cmd.data_[0], viewport.top.x, viewport.top.y);
            unpack_u64_to_2u32(cmd.data_[1], viewport.size.x, viewport.size.y);
            set_viewport(viewport);
            break;
        }
        case graphics_driver_bind_framebuffer: {
            const drivers::handle handle = static_cast<drivers::handle>(cmd.data_[0]);
            if (!handle) {
                bind_swapchain_framebuf();
                break;
            }

            native_framebuffer *framebuffer = reinterpret_cast<native_framebuffer *>(get_graphics_object(handle));
            if (framebuffer) {
                framebuffer->bind(this, static_cast<framebuffer_bind_type>(cmd.data_[1]));
            }
            break;
        }
        case graphics_driver_read_framebuffer:
            read_framebuffer(cmd);
            break;
        case graphics_driver_set_uniform:
            delete[] reinterpret_cast<std::uint8_t *>(cmd.data_[1]);
            break;
        case graphics_driver_bind_vertex_buffers:
            delete[] reinterpret_cast<std::uint8_t *>(cmd.data_[0]);
            delete[] reinterpret_cast<std::uint8_t *>(cmd.data_[2]);
            break;
        case graphics_driver_draw_array:
        case graphics_driver_draw_indexed:
        case graphics_driver_bind_index_buffer:
        case graphics_driver_bind_input_descriptor:
        case graphics_driver_set_texture_for_shader:
        case graphics_driver_set_state:
        case graphics_driver_blend_formula:
        case graphics_driver_depth_pass_condition:
        case graphics_driver_depth_set_mask:
        case graphics_driver_stencil_pass_condition:
        case graphics_driver_stencil_set_action:
        case graphics_driver_stencil_set_mask:
        case graphics_driver_set_front_face_rule:
        case graphics_driver_cull_face:
        case graphics_driver_set_color_mask:
        case graphics_driver_set_depth_func:
        case graphics_driver_set_line_width:
        case graphics_driver_set_depth_bias:
        case graphics_driver_set_depth_range:
        case graphics_driver_set_texture_anisotrophy:
        case graphics_driver_set_blend_colour:
        case graphics_driver_backup_state:
        case graphics_driver_restore_state:
        case graphics_driver_set_point_size:
        case graphics_driver_set_pen_style:
            break;
        default:
            shared_graphics_driver::dispatch(cmd);
            break;
        }
    }
}
