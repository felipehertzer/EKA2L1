#if EKA2L1_BUILD_NATIVE_BACKEND
#include <drivers/graphics/backend/native/graphics_native.h>
#endif
#if EKA2L1_BUILD_VULKAN_BACKEND
#include <drivers/graphics/backend/vulkan/buffer_vulkan.h>
#endif
#include <drivers/graphics/buffer.h>
#include <drivers/graphics/graphics.h>

namespace eka2l1::drivers {
    std::unique_ptr<buffer> make_buffer(graphics_driver *driver) {
#if EKA2L1_BUILD_VULKAN_BACKEND
        if (driver && (driver->get_current_api() == graphic_api::vulkan)) {
            return std::make_unique<vulkan_buffer>();
        }
#endif

#if EKA2L1_BUILD_NATIVE_BACKEND
        if (driver && (driver->get_current_api() == graphic_api::native)) {
            return std::make_unique<native_buffer>();
        }
#endif

        return nullptr;
    }
}
