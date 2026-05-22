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

#include "graphics_vulkan_macos.h"

#include <common/log.h>
#include <common/platform.h>

#if EKA2L1_PLATFORM(MACOS)
#import <AppKit/AppKit.h>
#else
#import <UIKit/UIKit.h>
#endif
#import <QuartzCore/CAMetalLayer.h>

namespace eka2l1::drivers {
    static NSString *const EKA2L1VulkanMetalLayerName = @"EKA2L1VulkanMetalLayer";

    void *get_or_create_vulkan_metal_layer(void *render_surface, const float scale, const std::uint32_t width, const std::uint32_t height) {
        if (!render_surface) {
            LOG_ERROR(DRIVER_GRAPHICS, "Render surface is not present!");
            return nullptr;
        }

#if EKA2L1_PLATFORM(MACOS)
        NSView *view = reinterpret_cast<NSView *>(render_surface);
#else
        id surface = reinterpret_cast<id>(render_surface);
        UIView *view = [surface isKindOfClass:[UIView class]] ? (UIView *)surface : nil;
#endif
        __block CAMetalLayer *metal_layer = nil;

        void (^configure_layer)(void) = ^{
#if EKA2L1_PLATFORM(MACOS)
            [view setWantsLayer:YES];

            CALayer *existing_layer = [view layer];
            if ([existing_layer isKindOfClass:[CAMetalLayer class]]) {
                metal_layer = (CAMetalLayer *)existing_layer;
            } else {
                metal_layer = [CAMetalLayer layer];
                [metal_layer setName:EKA2L1VulkanMetalLayerName];
                [view setLayer:metal_layer];
            }

            NSWindow *window = [view window];
            const CGFloat backing_scale = scale > 0.0f ? static_cast<CGFloat>(scale)
                                                       : (window ? [window backingScaleFactor] : 1.0);
            const NSRect bounds = [view bounds];
#else
            if ([surface isKindOfClass:[CAMetalLayer class]]) {
                metal_layer = (CAMetalLayer *)surface;
            } else if (view && [[view layer] isKindOfClass:[CAMetalLayer class]]) {
                metal_layer = (CAMetalLayer *)[view layer];
            } else {
                LOG_ERROR(DRIVER_GRAPHICS, "iOS Vulkan rendering requires a CAMetalLayer-backed view");
                return;
            }

            const CGFloat backing_scale = scale > 0.0f ? static_cast<CGFloat>(scale)
                                                       : (view ? [view contentScaleFactor] : [metal_layer contentsScale]);
            const CGRect bounds = view ? [view bounds] : [metal_layer bounds];
#endif
            const CGSize drawable_size = CGSizeMake(
                width ? static_cast<CGFloat>(width) : bounds.size.width * backing_scale,
                height ? static_cast<CGFloat>(height) : bounds.size.height * backing_scale);

            [metal_layer setContentsScale:backing_scale];
            [metal_layer setDrawableSize:drawable_size];
            [metal_layer setFrame:bounds];
            [metal_layer setOpaque:YES];
        };

        if ([NSThread isMainThread]) {
            configure_layer();
        } else {
            dispatch_sync(dispatch_get_main_queue(), configure_layer);
        }

        return metal_layer;
    }

    void destroy_vulkan_metal_layer(void *render_surface) {
        if (!render_surface) {
            return;
        }

#if EKA2L1_PLATFORM(MACOS)
        NSView *view = reinterpret_cast<NSView *>(render_surface);
        void (^destroy_layer)(void) = ^{
            CALayer *existing_layer = [view layer];
            if (![existing_layer isKindOfClass:[CAMetalLayer class]]) {
                return;
            }

            if (![[existing_layer name] isEqualToString:EKA2L1VulkanMetalLayerName]) {
                return;
            }

            [view setLayer:nil];
            [view setWantsLayer:NO];
        };

        if ([NSThread isMainThread]) {
            destroy_layer();
        } else {
            dispatch_async(dispatch_get_main_queue(), destroy_layer);
        }
#endif
    }
}
