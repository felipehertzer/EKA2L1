/*
 * Copyright (c) 2019 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project
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

#pragma once

#include <common/configure.h>
#include <common/platform.h>

#define EKA2L1_HAS_VULKAN_BACKEND (BUILD_WITH_VULKAN && (EKA2L1_PLATFORM(WIN32) || EKA2L1_PLATFORM(ANDROID) || EKA2L1_PLATFORM(MACOS) || EKA2L1_PLATFORM(IOS) || EKA2L1_PLATFORM(UNIX)))

#if EKA2L1_HAS_VULKAN_BACKEND

#include <drivers/graphics/backend/graphics_driver_shared.h>
#include <drivers/graphics/backend/vulkan/shader_vulkan.h>
#include <drivers/graphics/input_desc.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

#if EKA2L1_PLATFORM(WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#elif EKA2L1_PLATFORM(ANDROID)
#define VK_USE_PLATFORM_ANDROID_KHR
#elif EKA2L1_PLATFORM(MACOS) || EKA2L1_PLATFORM(IOS)
#define VK_USE_PLATFORM_METAL_EXT
#elif EKA2L1_PLATFORM(UNIX)
#define VK_USE_PLATFORM_XCB_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif

#include <vulkan/vulkan.hpp>

namespace eka2l1::drivers {
    class vulkan_framebuffer;
    class vulkan_buffer;
    class vulkan_input_descriptors;
    class vulkan_shader_program;
    class vulkan_texture;

    static constexpr std::size_t VULKAN_BACKEND_MAX_VERTEX_BUFFER_SLOTS = 16;
    static constexpr std::size_t VULKAN_BACKEND_MAX_TEXTURE_SLOTS = 32;

    struct vulkan_stencil_face_state {
        condition_func compare = condition_func::always;
        std::int32_t reference = 0;
        std::uint32_t compare_mask = 0xFFFFFFFF;
        std::uint32_t write_mask = 0xFFFFFFFF;
        stencil_action stencil_fail = stencil_action::keep;
        stencil_action depth_fail = stencil_action::keep;
        stencil_action depth_pass = stencil_action::keep;
    };

    struct vulkan_draw_state {
        bool clipping_enabled = false;
        std::vector<eka2l1::rect> clip_rects;
        bool viewport_set = false;
        eka2l1::rect viewport;

        bool blend_enabled = false;
        std::uint8_t color_write_mask = 0xF;
        std::array<float, 4> blend_color = { 0.0f, 0.0f, 0.0f, 0.0f };
        blend_equation rgb_blend_equation = blend_equation::add;
        blend_equation alpha_blend_equation = blend_equation::add;
        blend_factor rgb_source_factor = blend_factor::one;
        blend_factor rgb_dest_factor = blend_factor::zero;
        blend_factor alpha_source_factor = blend_factor::one;
        blend_factor alpha_dest_factor = blend_factor::zero;

        bool depth_test_enabled = false;
        bool depth_write_enabled = true;
        condition_func depth_compare = condition_func::less;
        float depth_range_near = 0.0f;
        float depth_range_far = 1.0f;
        bool depth_bias_enabled = false;
        float depth_bias_constant = 0.0f;
        float depth_bias_slope = 0.0f;
        float depth_bias_clamp = 0.0f;

        bool stencil_test_enabled = false;
        vulkan_stencil_face_state front_stencil;
        vulkan_stencil_face_state back_stencil;

        bool cull_enabled = false;
        rendering_face cull_face = rendering_face::back;
        rendering_face_determine_rule front_face_rule = rendering_face_determine_rule::vertices_counter_clockwise;

        bool line_smooth_enabled = false;
        bool multisample_enabled = false;
        bool sample_alpha_to_coverage_enabled = false;
        bool sample_alpha_to_one_enabled = false;
        bool sample_coverage_enabled = false;
        bool dither_enabled = false;
        float line_width = 1.0f;
    };

    struct vulkan_blend_state_key {
        bool blend_enabled = false;
        std::uint8_t color_write_mask = 0xF;
        blend_equation rgb_blend_equation = blend_equation::add;
        blend_equation alpha_blend_equation = blend_equation::add;
        blend_factor rgb_source_factor = blend_factor::one;
        blend_factor rgb_dest_factor = blend_factor::zero;
        blend_factor alpha_source_factor = blend_factor::one;
        blend_factor alpha_dest_factor = blend_factor::zero;
    };

    struct vulkan_rectangle_draw {
        vulkan_framebuffer *target;
        eka2l1::rect rectangle;
        eka2l1::vecx<float, 4> color;
        vulkan_draw_state state;
    };

    struct vulkan_bitmap_draw {
        vulkan_framebuffer *target;
        vulkan_texture *texture;
        vulkan_texture *mask_texture;
        eka2l1::rect destination;
        eka2l1::rect source;
        eka2l1::vec2 origin;
        float rotation;
        std::uint32_t flags;
        eka2l1::vecx<float, 4> color;
        vulkan_draw_state state;
    };

    struct vulkan_blend_pipeline_cache_entry {
        vulkan_blend_state_key key;
        vk::UniquePipeline rectangle_pipeline;
        vk::UniquePipeline bitmap_pipeline;
        vk::UniquePipeline offscreen_rectangle_pipeline;
        vk::UniquePipeline offscreen_bitmap_pipeline;
        vk::UniquePipeline offscreen_no_depth_rectangle_pipeline;
        vk::UniquePipeline offscreen_no_depth_bitmap_pipeline;
    };

    struct vulkan_upscale_pipeline_cache_entry {
        std::string shader_name;
        vulkan_blend_state_key blend_key;
        bool offscreen = false;
        vk::UniqueShaderModule fragment_shader;
        vk::UniquePipeline pipeline;
    };

    struct vulkan_advanced_draw {
        vulkan_framebuffer *target = nullptr;
        vulkan_shader_program *program = nullptr;
        vulkan_input_descriptors *input_descriptors = nullptr;
        std::vector<input_descriptor> input_descriptor_snapshot;
        vulkan_shader_descriptor_state descriptor_state;
        std::array<vulkan_buffer *, VULKAN_BACKEND_MAX_VERTEX_BUFFER_SLOTS> vertex_buffers = {};
        std::array<vk::DeviceSize, VULKAN_BACKEND_MAX_VERTEX_BUFFER_SLOTS> vertex_buffer_offsets = {};
        std::array<vulkan_texture *, VULKAN_BACKEND_MAX_TEXTURE_SLOTS> texture_slots = {};
        vulkan_buffer *index_buffer = nullptr;
        vulkan_draw_state state;
        graphics_primitive_mode primitive_mode = graphics_primitive_mode::triangles;
        data_format index_type = data_format::word;
        std::int32_t first_vertex = 0;
        std::int32_t vertex_count = 0;
        std::int32_t instance_count = 1;
        std::int32_t index_count = 0;
        std::int32_t index_offset = 0;
        std::int32_t vertex_base = 0;
        bool indexed = false;
    };

    using vulkan_pending_draw = std::variant<vulkan_rectangle_draw, vulkan_bitmap_draw, vulkan_advanced_draw>;

    struct vulkan_advanced_pipeline_key {
        vulkan_shader_program *program = nullptr;
        std::vector<input_descriptor> input_descriptors;
        graphics_primitive_mode primitive_mode = graphics_primitive_mode::triangles;
        vulkan_blend_state_key blend_state;
        bool cull_enabled = false;
        rendering_face cull_face = rendering_face::back;
        rendering_face_determine_rule front_face_rule = rendering_face_determine_rule::vertices_counter_clockwise;
        bool depth_test_enabled = false;
        bool depth_write_enabled = true;
        condition_func depth_compare = condition_func::less;
        bool depth_bias_enabled = false;
        bool stencil_test_enabled = false;
        vulkan_stencil_face_state front_stencil;
        vulkan_stencil_face_state back_stencil;
        bool offscreen = false;
    };

    struct vulkan_advanced_pipeline_cache_entry {
        vulkan_advanced_pipeline_key key;
        vk::UniquePipelineLayout pipeline_layout;
        vk::UniquePipeline pipeline;
    };

    struct vulkan_debug_options {
        bool enabled = false;
        bool trace_draws = false;
        bool capture_present = false;
        bool capture_offscreen = false;
        std::uint32_t present_frame_limit = 0;
        std::uint32_t offscreen_capture_limit = 0;
        std::uint32_t present_frame_skip = 0;
        std::uint32_t present_frame_interval = 1;
        std::uint32_t offscreen_capture_skip = 0;
        std::uint32_t offscreen_capture_interval = 1;
        std::string output_directory;
    };

    struct vulkan_pending_texture_upload {
        vulkan_texture *texture = nullptr;
        vk::Buffer staging_buffer;
        eka2l1::vec3 offset;
        eka2l1::vec3 size;
        vk::ImageLayout old_layout = vk::ImageLayout::eUndefined;
    };

    class vulkan_graphics_driver : public shared_graphics_driver {
        vk::UniqueInstance inst_;
        vk::UniqueDebugReportCallbackEXT reporter_;
        vk::UniqueDevice dvc_;
        vk::PhysicalDevice phys_dvc_;
        vk::PhysicalDeviceFeatures physical_device_features_;
        vk::PhysicalDeviceProperties physical_device_properties_;

        vk::UniqueSurfaceKHR surface_;
        vk::UniqueSwapchainKHR swapchain_;
        std::vector<vk::Image> swapchain_images_;
        std::vector<vk::UniqueImageView> swapchain_image_views_;
        std::vector<bool> swapchain_image_initialized_;
        std::vector<vk::UniqueFramebuffer> swapchain_framebuffers_;
        vk::UniqueImage swapchain_depth_stencil_image_;
        vk::UniqueDeviceMemory swapchain_depth_stencil_memory_;
        vk::UniqueImageView swapchain_depth_stencil_image_view_;
        vk::Format swapchain_format_;
        vk::Format depth_stencil_format_;
        vk::Extent2D swapchain_extent_;
        vk::Queue graphics_queue_;
        std::uint32_t graphics_queue_family_index_;
        vk::UniqueCommandPool command_pool_;
        vk::UniqueCommandBuffer command_buffer_;
        vk::UniqueSemaphore image_available_semaphore_;
        vk::UniqueSemaphore render_finished_semaphore_;
        vk::UniqueFence frame_fence_;
        vk::ClearColorValue clear_color_;
        float clear_depth_;
        std::uint32_t clear_stencil_;
        vk::UniqueRenderPass render_pass_;
        vk::UniqueShaderModule rectangle_vertex_shader_;
        vk::UniqueShaderModule rectangle_fragment_shader_;
        vk::UniquePipelineLayout rectangle_pipeline_layout_;
        vk::UniquePipeline rectangle_pipeline_;
        vk::UniqueDescriptorSetLayout bitmap_descriptor_set_layout_;
        vk::UniqueDescriptorPool bitmap_descriptor_pool_;
        vk::UniqueShaderModule bitmap_vertex_shader_;
        vk::UniqueShaderModule bitmap_fragment_shader_;
        vk::UniquePipelineLayout bitmap_pipeline_layout_;
        vk::UniquePipeline bitmap_pipeline_;
        vk::UniqueRenderPass offscreen_render_pass_;
        vk::UniqueRenderPass offscreen_no_depth_render_pass_;
        vk::UniquePipeline offscreen_rectangle_pipeline_;
        vk::UniquePipeline offscreen_bitmap_pipeline_;
        vk::UniquePipeline offscreen_no_depth_rectangle_pipeline_;
        vk::UniquePipeline offscreen_no_depth_bitmap_pipeline_;
        vulkan_blend_state_key default_blend_key_;
        std::vector<vulkan_blend_pipeline_cache_entry> blend_pipeline_cache_;
        std::vector<vulkan_upscale_pipeline_cache_entry> upscale_pipeline_cache_;
        std::vector<vulkan_advanced_pipeline_cache_entry> advanced_pipeline_cache_;
        std::vector<vulkan_shader_descriptor_snapshot> present_descriptor_snapshots_;

        window_system_info wsi_;
        eka2l1::request_queue<command_list> list_queue_;
        std::atomic_bool should_stop_;
        std::string active_upscale_shader_;
        std::vector<vulkan_pending_texture_upload> pending_texture_uploads_;
        std::vector<vulkan_pending_draw> pending_draws_;
        vulkan_framebuffer *active_framebuffer_;
        vulkan_framebuffer *bound_read_framebuffer_;
        vulkan_framebuffer *bound_draw_framebuffer_;
        vulkan_draw_state draw_state_;
        vulkan_draw_state backup_draw_state_;
        std::array<vulkan_buffer *, VULKAN_BACKEND_MAX_VERTEX_BUFFER_SLOTS> vertex_buffers_;
        std::array<vk::DeviceSize, VULKAN_BACKEND_MAX_VERTEX_BUFFER_SLOTS> vertex_buffer_offsets_;
        std::array<vulkan_texture *, VULKAN_BACKEND_MAX_TEXTURE_SLOTS> texture_slots_;
        vulkan_buffer *index_buffer_;
        vulkan_input_descriptors *active_input_descriptors_;
        vulkan_shader_program *active_shader_program_;
        float point_size_;
        pen_style line_style_;
        vulkan_debug_options debug_options_;
        std::uint64_t debug_frame_index_;
        std::uint64_t debug_present_frame_seen_;
        std::uint64_t debug_offscreen_capture_index_;
        std::uint64_t debug_offscreen_target_seen_;
        bool debug_swapchain_capture_supported_;

        bool initialized_;
        bool renderer_supported_;

        void configure_debug_options();
        bool create_debug_readback_buffer(const vk::DeviceSize size, vk::UniqueBuffer &buffer, vk::UniqueDeviceMemory &memory);
        bool debug_capture_texture(vulkan_texture *texture, const std::string &label, const std::uint64_t index);
        bool debug_record_swapchain_capture(vk::CommandBuffer command_buffer, vk::Image image, vk::Buffer readback_buffer,
            const vk::Extent2D &extent);
        bool debug_write_swapchain_capture(vk::DeviceMemory readback_memory, const vk::Extent2D &extent,
            const std::string &label, const std::uint64_t index);
        void debug_log_pending_draws(vulkan_framebuffer *target, const char *stage) const;
        bool create_instance();
        bool create_debug_callback();
        bool create_device();
        bool create_surface();
        bool create_swapchain();
        bool create_command_resources();
        bool create_sync_objects();
        bool create_descriptor_resources();
        bool create_render_resources();
        vk::UniquePipeline create_2d_pipeline(vk::RenderPass render_pass, vk::PipelineLayout pipeline_layout,
            vk::ShaderModule vertex_shader, vk::ShaderModule fragment_shader, const vulkan_blend_state_key &blend_state);
        vulkan_blend_pipeline_cache_entry *get_or_create_blend_pipeline(const vulkan_draw_state &draw_state);
        vk::Pipeline get_or_create_upscale_pipeline(const std::string &shader_name, const vulkan_draw_state &draw_state, bool offscreen);
        vulkan_advanced_pipeline_cache_entry *get_or_create_advanced_pipeline(const vulkan_advanced_draw &draw, const bool offscreen);
        bool present_clear_frame();
        bool has_pending_framebuffer_draws(vulkan_framebuffer *target) const;
        bool can_use_no_depth_offscreen(vulkan_framebuffer *target) const;
        void record_pending_texture_uploads(vk::CommandBuffer command_buffer);
        void complete_pending_texture_uploads();
        bool render_pending_to_framebuffer(vulkan_framebuffer *target);
        bool flush_pending_framebuffer_draws();
        void erase_pending_framebuffer_draws(vulkan_framebuffer *target);
        void record_pending_draws(vk::CommandBuffer command_buffer, const vk::Extent2D &extent,
            vulkan_framebuffer *target, vk::Pipeline rectangle_pipeline, vk::Pipeline bitmap_pipeline,
            const bool offscreen, const bool offscreen_has_depth = true,
            std::vector<vulkan_shader_descriptor_snapshot> *descriptor_snapshots = nullptr);
        void destroy_swapchain_resources();
        void destroy_render_resources();
        void display(command &cmd);
        void bind_bitmap(command &cmd);
        void draw_rectangle(command &cmd);
        void draw_bitmap(command &cmd);
        void draw_line_segment(const eka2l1::point &start, const eka2l1::point &end);
        void draw_line(command &cmd);
        void draw_polygon(command &cmd);
        void set_point_size(command &cmd);
        void set_pen_style(command &cmd);
        void clip_rect(command &cmd);
        void clip_region(command &cmd);
        void set_feature(command &cmd);
        void set_viewport(command &cmd);
        void blend_formula(command &cmd);
        void set_color_mask(command &cmd);
        void set_depth_func(command &cmd);
        void set_depth_mask(command &cmd);
        void set_depth_bias(command &cmd);
        void set_depth_range(command &cmd);
        void set_line_width(command &cmd);
        void set_blend_colour(command &cmd);
        void set_stencil_pass_condition(command &cmd);
        void set_stencil_action(command &cmd);
        void set_stencil_mask(command &cmd);
        void set_front_face_rule(command &cmd);
        void set_cull_face(command &cmd);
        void bind_framebuffer(command &cmd);
        void set_texture_anisotrophy(command &cmd);
        void bind_texture(command &cmd);
        void bind_vertex_buffers(command &cmd);
        void bind_index_buffer(command &cmd);
        void bind_input_descriptor(command &cmd);
        void draw_array(command &cmd);
        void draw_indexed(command &cmd);
        void set_uniform(command &cmd);
        void set_texture_for_shader(command &cmd);
        void read_bitmap(command &cmd);
        void read_framebuffer(command &cmd);
        void dispatch(command &cmd) override;

    public:
        explicit vulkan_graphics_driver(const window_system_info &info);
        ~vulkan_graphics_driver() override;

        bool is_initialized() const {
            return initialized_;
        }

        bool is_renderer_supported() const {
            return renderer_supported_;
        }

        vk::Device device() const {
            return dvc_.get();
        }

        vk::PhysicalDevice physical_device() const {
            return phys_dvc_;
        }

        std::uint32_t find_memory_type(const std::uint32_t type_filter, const vk::MemoryPropertyFlags properties) const;
        bool submit_immediate(const std::function<void(vk::CommandBuffer)> &recorder);
        void queue_texture_upload(vulkan_texture *texture, vk::Buffer staging_buffer, const eka2l1::vec3 &offset,
            const eka2l1::vec3 &size, const vk::ImageLayout old_layout);
        bool flush_pending_texture_uploads();
        vk::DescriptorSet allocate_bitmap_descriptor_set();
        void update_bitmap_descriptor_set(vk::DescriptorSet descriptor_set, vk::ImageView image_view, vk::Sampler sampler);
        vk::RenderPass offscreen_render_pass() const {
            return offscreen_render_pass_.get();
        }

        void set_bound_framebuffer(vulkan_framebuffer *framebuffer, const framebuffer_bind_type type_bind);
        void clear_bound_framebuffer(vulkan_framebuffer *framebuffer);
        vulkan_framebuffer *bound_draw_framebuffer() const {
            return bound_draw_framebuffer_;
        }
        void set_active_shader_program(vulkan_shader_program *program) {
            active_shader_program_ = program;
        }

        void submit_command_list(command_list &cmd_list) override;
        void run() override;
        void abort() override;
        bool aborted() const override;
        void bind_swapchain_framebuf() override;
        void update_surface(void *surface) override;
        void update_surface_size(const eka2l1::vec2 &size) override;
        void wait_for(int *status) override;
        void set_upscale_shader(const std::string &name) override;
        std::string get_active_upscale_shader() const override;
        bool support_extension(const graphics_driver_extension ext) override;
        bool query_extension_value(const graphics_driver_extension_query query, void *data_ptr) override;
    };
}

#endif

#undef EKA2L1_HAS_VULKAN_BACKEND
