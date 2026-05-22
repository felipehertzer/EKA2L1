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

#include <drivers/camera/backend/ios/camera_ios.h>

#include <common/log.h>

#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <UIKit/UIKit.h>

#include <algorithm>
#include <cstring>
#include <set>

namespace eka2l1::drivers::camera {
    static constexpr std::uint32_t IOS_SUPPORTED_FORMATS = FRAME_FORMAT_ARGB8888 | FRAME_FORMAT_JPEG | FRAME_FORMAT_RGB565 | FRAME_FORMAT_FBSBMP_COLOR64K | FRAME_FORMAT_FBSBMP_COLOR16M | FRAME_FORMAT_FBSBMP_COLOR16MU | FRAME_FORMAT_EXIF;

    static AVCaptureDevice *as_device(void *ptr) {
        return reinterpret_cast<AVCaptureDevice *>(ptr);
    }

    static AVCaptureSession *as_session(void *ptr) {
        return reinterpret_cast<AVCaptureSession *>(ptr);
    }

    static AVCapturePhotoOutput *as_photo_output(void *ptr) {
        return reinterpret_cast<AVCapturePhotoOutput *>(ptr);
    }

    static AVCaptureVideoDataOutput *as_video_output(void *ptr) {
        return reinterpret_cast<AVCaptureVideoDataOutput *>(ptr);
    }

    static dispatch_queue_t as_queue(void *ptr) {
        return reinterpret_cast<dispatch_queue_t>(ptr);
    }

    static std::vector<frame_format> supported_formats() {
        return {
            FRAME_FORMAT_ARGB8888,
            FRAME_FORMAT_JPEG,
            FRAME_FORMAT_RGB565,
            FRAME_FORMAT_FBSBMP_COLOR64K,
            FRAME_FORMAT_FBSBMP_COLOR16M,
            FRAME_FORMAT_FBSBMP_COLOR16MU,
            FRAME_FORMAT_EXIF
        };
    }

    static std::uint16_t pack_rgb565(std::uint8_t b, std::uint8_t g, std::uint8_t r) {
        return static_cast<std::uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
    }

    static std::vector<std::uint8_t> convert_bgra_to_format(const std::uint8_t *src, int src_width,
        int src_height, int src_stride, int dst_width, int dst_height, frame_format format) {
        if (!src || src_width <= 0 || src_height <= 0 || dst_width <= 0 || dst_height <= 0) {
            return {};
        }

        switch (format) {
        case FRAME_FORMAT_RGB565:
        case FRAME_FORMAT_FBSBMP_COLOR64K: {
            const int row_bytes = (format == FRAME_FORMAT_FBSBMP_COLOR64K)
                ? (((dst_width * 2) + 3) / 4 * 4)
                : (dst_width * 2);
            std::vector<std::uint8_t> output(static_cast<std::size_t>(row_bytes * dst_height));

            for (int y = 0; y < dst_height; y++) {
                const int sy = y * src_height / dst_height;
                for (int x = 0; x < dst_width; x++) {
                    const int sx = x * src_width / dst_width;
                    const std::uint8_t *pixel = src + sy * src_stride + sx * 4;
                    const std::uint16_t packed = pack_rgb565(pixel[0], pixel[1], pixel[2]);
                    output[y * row_bytes + x * 2] = static_cast<std::uint8_t>(packed & 0xFF);
                    output[y * row_bytes + x * 2 + 1] = static_cast<std::uint8_t>((packed >> 8) & 0xFF);
                }
            }

            return output;
        }

        case FRAME_FORMAT_FBSBMP_COLOR16M: {
            const int row_bytes = ((dst_width * 3) + 3) / 4 * 4;
            std::vector<std::uint8_t> output(static_cast<std::size_t>(row_bytes * dst_height));

            for (int y = 0; y < dst_height; y++) {
                const int sy = y * src_height / dst_height;
                for (int x = 0; x < dst_width; x++) {
                    const int sx = x * src_width / dst_width;
                    const std::uint8_t *pixel = src + sy * src_stride + sx * 4;
                    output[y * row_bytes + x * 3] = pixel[0];
                    output[y * row_bytes + x * 3 + 1] = pixel[1];
                    output[y * row_bytes + x * 3 + 2] = pixel[2];
                }
            }

            return output;
        }

        case FRAME_FORMAT_ARGB8888:
        case FRAME_FORMAT_FBSBMP_COLOR16MU:
        default: {
            const int row_bytes = dst_width * 4;
            std::vector<std::uint8_t> output(static_cast<std::size_t>(row_bytes * dst_height));

            for (int y = 0; y < dst_height; y++) {
                const int sy = y * src_height / dst_height;
                for (int x = 0; x < dst_width; x++) {
                    const int sx = x * src_width / dst_width;
                    const std::uint8_t *pixel = src + sy * src_stride + sx * 4;
                    std::memcpy(output.data() + y * row_bytes + x * 4, pixel, 4);
                }
            }

            return output;
        }
        }
    }

    static std::vector<std::uint8_t> bgra_from_image_data(NSData *data, const eka2l1::vec2 &target_size) {
        if (!data) {
            return {};
        }

        UIImage *image = [UIImage imageWithData:data];
        if (!image || !image.CGImage) {
            return {};
        }

        const int width = static_cast<int>(target_size.x);
        const int height = static_cast<int>(target_size.y);
        if (width <= 0 || height <= 0) {
            return {};
        }

        std::vector<std::uint8_t> output(static_cast<std::size_t>(width * height * 4));
        CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
        const CGBitmapInfo bitmap_info = static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little)
            | static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst);
        CGContextRef context = CGBitmapContextCreate(output.data(), width, height, 8, width * 4,
            color_space, bitmap_info);

        if (context) {
            CGContextSetInterpolationQuality(context, kCGInterpolationMedium);
            CGContextDrawImage(context, CGRectMake(0, 0, width, height), image.CGImage);
            CGContextRelease(context);
        } else {
            output.clear();
        }

        CGColorSpaceRelease(color_space);
        return output;
    }

    static std::vector<eka2l1::vec2> output_sizes_for_device(AVCaptureDevice *device) {
        std::set<std::pair<int, int>> unique_sizes;
        for (AVCaptureDeviceFormat *format in [device formats]) {
            CMVideoDimensions dims = CMVideoFormatDescriptionGetDimensions(format.formatDescription);
            if (dims.width > 0 && dims.height > 0) {
                unique_sizes.emplace(static_cast<int>(dims.width), static_cast<int>(dims.height));
            }
        }

        if (unique_sizes.empty()) {
            unique_sizes.emplace(640, 480);
            unique_sizes.emplace(1280, 720);
            unique_sizes.emplace(1920, 1080);
        }

        std::vector<eka2l1::vec2> result;
        result.reserve(unique_sizes.size());
        for (const auto &size : unique_sizes) {
            result.emplace_back(size.first, size.second);
        }

        std::sort(result.begin(), result.end(), [](const eka2l1::vec2 &lhs, const eka2l1::vec2 &rhs) {
            const std::int64_t lhs_area = static_cast<std::int64_t>(lhs.x) * lhs.y;
            const std::int64_t rhs_area = static_cast<std::int64_t>(rhs.x) * rhs.y;
            if (lhs_area == rhs_area) {
                return lhs.x < rhs.x;
            }

            return lhs_area < rhs_area;
        });
        return result;
    }
}

@interface EKA2L1CameraPhotoDelegate : NSObject <AVCapturePhotoCaptureDelegate>
@property (nonatomic, assign) eka2l1::drivers::camera::instance_ios *owner;
@property (nonatomic, assign) eka2l1::drivers::camera::frame_format format;
@property (nonatomic, assign) eka2l1::vec2 targetSize;
@end

@implementation EKA2L1CameraPhotoDelegate
- (void)photoOutput:(AVCapturePhotoOutput *)output didFinishProcessingPhoto:(AVCapturePhoto *)photo error:(NSError *)error {
    (void)output;
    if (error || !self.owner) {
        if (self.owner) {
            self.owner->deliver_capture_data(nullptr, 0, self.targetSize, self.format, -1);
        }
#if !__has_feature(objc_arc)
        [self release];
#endif
        return;
    }

    NSData *data = [photo fileDataRepresentation];
    self.owner->deliver_capture_data([data bytes], [data length], self.targetSize, self.format, data ? 0 : -1);
#if !__has_feature(objc_arc)
    [self release];
#endif
}
@end

@interface EKA2L1CameraVideoDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, assign) eka2l1::drivers::camera::instance_ios *owner;
@end

@implementation EKA2L1CameraVideoDelegate
- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection *)connection {
    (void)output;
    (void)connection;

    if (!self.owner || !self.owner->wants_viewfinder_frame()) {
        return;
    }

    CVImageBufferRef image_buffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!image_buffer) {
        return;
    }

    CVPixelBufferLockBaseAddress(image_buffer, kCVPixelBufferLock_ReadOnly);
    const int width = static_cast<int>(CVPixelBufferGetWidth(image_buffer));
    const int height = static_cast<int>(CVPixelBufferGetHeight(image_buffer));
    const int stride = static_cast<int>(CVPixelBufferGetBytesPerRow(image_buffer));
    const void *base = CVPixelBufferGetBaseAddress(image_buffer);

    self.owner->deliver_viewfinder_frame(base, width, height, stride);
    CVPixelBufferUnlockBaseAddress(image_buffer, kCVPixelBufferLock_ReadOnly);
}
@end

namespace eka2l1::drivers::camera {
    static AVCaptureDevice *default_device(AVCaptureDevicePosition position) {
        AVCaptureDeviceDiscoverySession *session = [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:@[ AVCaptureDeviceTypeBuiltInWideAngleCamera ]
                                  mediaType:AVMediaTypeVideo
                                   position:position];
        return [[session devices] firstObject];
    }

    collection_ios::collection_ios() {
        AVCaptureDevice *back = default_device(AVCaptureDevicePositionBack);
        if (back) {
#if !__has_feature(objc_arc)
            [back retain];
#endif
            devices_.push_back(back);
        }

        AVCaptureDevice *front = default_device(AVCaptureDevicePositionFront);
        if (front) {
#if !__has_feature(objc_arc)
            [front retain];
#endif
            devices_.push_back(front);
        }
    }

    collection_ios::~collection_ios() {
#if !__has_feature(objc_arc)
        for (void *device : devices_) {
            [as_device(device) release];
        }
#endif
    }

    std::uint32_t collection_ios::count() const {
        return static_cast<std::uint32_t>(devices_.size());
    }

    std::unique_ptr<instance> collection_ios::make_camera(const std::uint32_t camera_index) {
        if (camera_index >= devices_.size()) {
            return nullptr;
        }

        return std::make_unique<instance_ios>(this, static_cast<int>(camera_index), devices_[camera_index]);
    }

    instance_ios::instance_ios(collection_ios *collection, const int index, void *device)
        : collection_(collection)
        , index_(index)
        , device_(device)
        , session_(nullptr)
        , photo_output_(nullptr)
        , video_output_(nullptr)
        , video_delegate_(nullptr)
        , capture_queue_(nullptr)
        , active_capture_img_callback_(nullptr)
        , active_frame_viewfinder_callback_(nullptr)
        , wants_new_frame_callback_(nullptr)
        , flash_mode_(FLASH_MODE_OFF)
        , stub_exposure_(EXPOSURE_MODE_AUTO)
        , stub_digital_zoom_(1)
        , viewfinder_size_(0)
        , viewfinder_format_(FRAME_FORMAT_FBSBMP_COLOR16MU)
        , viewfinder_active_(false) {
        output_sizes_ = output_sizes_for_device(as_device(device_));

        capture_queue_ = dispatch_queue_create("com.eka2l1.camera.ios", DISPATCH_QUEUE_SERIAL);
    }

    instance_ios::~instance_ios() {
        release();

        stop_viewfinder_feed();
        AVCaptureSession *session = as_session(session_);
        if (session) {
            [session stopRunning];
        }

#if !__has_feature(objc_arc)
        [as_photo_output(photo_output_) release];
        [as_video_output(video_output_) release];
        [as_session(session_) release];
        [reinterpret_cast<EKA2L1CameraVideoDelegate *>(video_delegate_) release];
        dispatch_release(as_queue(capture_queue_));
#endif
    }

    bool instance_ios::ensure_authorized() {
        AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
        if (status == AVAuthorizationStatusAuthorized) {
            return true;
        }

        if (status != AVAuthorizationStatusNotDetermined) {
            return false;
        }

        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        __block BOOL granted = NO;
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL allowed) {
                                     granted = allowed;
                                     dispatch_semaphore_signal(semaphore);
                                 }];
        dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
#if !__has_feature(objc_arc)
        dispatch_release(semaphore);
#endif
        return granted;
    }

    bool instance_ios::ensure_session() {
        if (session_) {
            return true;
        }

        if (!ensure_authorized()) {
            LOG_ERROR(DRIVER_CAM, "iOS camera permission is not granted");
            return false;
        }

        NSError *error = nil;
        AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:as_device(device_) error:&error];
        if (!input || error) {
            LOG_ERROR(DRIVER_CAM, "Failed to create iOS camera input");
            return false;
        }

        AVCaptureSession *session = [[AVCaptureSession alloc] init];
        if ([session canSetSessionPreset:AVCaptureSessionPresetPhoto]) {
            [session setSessionPreset:AVCaptureSessionPresetPhoto];
        }

        if (![session canAddInput:input]) {
            LOG_ERROR(DRIVER_CAM, "Failed to add iOS camera input to session");
#if !__has_feature(objc_arc)
            [session release];
#endif
            return false;
        }

        [session addInput:input];
        session_ = session;
        return true;
    }

    bool instance_ios::ensure_photo_output() {
        if (photo_output_) {
            return true;
        }

        if (!ensure_session()) {
            return false;
        }

        AVCapturePhotoOutput *photo_output = [[AVCapturePhotoOutput alloc] init];
        AVCaptureSession *session = as_session(session_);
        if (![session canAddOutput:photo_output]) {
#if !__has_feature(objc_arc)
            [photo_output release];
#endif
            return false;
        }

        [session addOutput:photo_output];
        photo_output_ = photo_output;
        return true;
    }

    bool instance_ios::ensure_video_output(const eka2l1::vec2 &size, const frame_format format) {
        (void)format;
        if (video_output_) {
            return true;
        }

        if (!ensure_session()) {
            return false;
        }

        AVCaptureSession *session = as_session(session_);
        if ((size.x <= 640) && (size.y <= 480) && [session canSetSessionPreset:AVCaptureSessionPreset640x480]) {
            [session setSessionPreset:AVCaptureSessionPreset640x480];
        } else if ((size.x <= 1280) && (size.y <= 720) && [session canSetSessionPreset:AVCaptureSessionPreset1280x720]) {
            [session setSessionPreset:AVCaptureSessionPreset1280x720];
        }

        AVCaptureVideoDataOutput *video_output = [[AVCaptureVideoDataOutput alloc] init];
        [video_output setAlwaysDiscardsLateVideoFrames:YES];
        [video_output setVideoSettings:@{
            (NSString *)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
        }];

        EKA2L1CameraVideoDelegate *delegate = [[EKA2L1CameraVideoDelegate alloc] init];
        delegate.owner = this;
        [video_output setSampleBufferDelegate:delegate queue:as_queue(capture_queue_)];

        if (![session canAddOutput:video_output]) {
#if !__has_feature(objc_arc)
            [delegate release];
            [video_output release];
#endif
            return false;
        }

        [session addOutput:video_output];
        video_output_ = video_output;
        video_delegate_ = delegate;
        return true;
    }

    void instance_ios::stop_session_if_idle() {
        if (session_ && !viewfinder_active_ && !active_capture_img_callback_) {
            [as_session(session_) stopRunning];
        }
    }

    bool instance_ios::set_parameter(const parameter_key key, const std::uint32_t value) {
        switch (key) {
        case PARAMETER_KEY_FLASH:
            if ((value != FLASH_MODE_OFF) && (value != FLASH_MODE_AUTO) && (value != FLASH_MODE_FORCED) && (value != FLASH_MODE_VIDEO_LIGHT)) {
                return false;
            }
            flash_mode_ = value;
            return true;

        case PARAMETER_KEY_EXPOSURE:
            stub_exposure_ = value;
            return true;

        case PARAMETER_KEY_DIGITAL_ZOOM:
            stub_digital_zoom_ = value;
            return true;

        default:
            return false;
        }
    }

    bool instance_ios::get_parameter(const parameter_key key, std::uint32_t &value) {
        switch (key) {
        case PARAMETER_KEY_FLASH:
            value = flash_mode_;
            return true;

        case PARAMETER_KEY_EXPOSURE:
            value = stub_exposure_;
            return true;

        case PARAMETER_KEY_DIGITAL_ZOOM:
            value = stub_digital_zoom_;
            return true;

        default:
            return false;
        }
    }

    std::vector<frame_format> instance_ios::supported_frame_formats() {
        return supported_formats();
    }

    std::vector<eka2l1::vec2> instance_ios::supported_output_image_sizes(const frame_format frame_format) {
        (void)frame_format;
        return output_sizes_;
    }

    bool instance_ios::reserve() {
        const std::lock_guard<std::mutex> guard(collection_->reserve_lock_);
        if (collection_->current_reserved_[index_] != nullptr) {
            return false;
        }

        collection_->current_reserved_[index_] = this;
        return true;
    }

    void instance_ios::release() {
        stop_viewfinder_feed();

        const std::lock_guard<std::mutex> guard(collection_->reserve_lock_);
        if (collection_->current_reserved_[index_] == this) {
            collection_->current_reserved_[index_] = nullptr;
        }
    }

    info instance_ios::get_info() {
        info result;
        result.camera_direction_ = ([as_device(device_) position] == AVCaptureDevicePositionFront)
            ? DIRECTION_FRONT
            : DIRECTION_BACK;
        result.num_image_sizes_supported_ = static_cast<std::int32_t>(output_sizes_.size());
        result.flash_modes_supported_ = FLASH_MODE_OFF | FLASH_MODE_AUTO | FLASH_MODE_FORCED | FLASH_MODE_VIDEO_LIGHT;
        result.options_supported_ = CAPTURE_OPTION_ALL;
        result.supported_image_formats_ = IOS_SUPPORTED_FORMATS;
        result.max_frame_per_buffer_supported_ = 1;
        result.max_buffers_supported_ = 1;
        return result;
    }

    void instance_ios::capture_image(const std::uint32_t resolution_index, const frame_format format,
        camera_capture_image_done_callback callback) {
        if (!callback || (resolution_index >= output_sizes_.size())) {
            if (callback) {
                callback(nullptr, 0, -1);
            }
            return;
        }

        {
            const std::lock_guard<std::mutex> guard(collection_->reserve_lock_);
            if (collection_->current_reserved_[index_] != this) {
                callback(nullptr, 0, -1);
                return;
            }
        }

        if (!ensure_photo_output()) {
            callback(nullptr, 0, -1);
            return;
        }

        {
            const std::lock_guard<std::mutex> guard(callback_lock_);
            if (active_capture_img_callback_) {
                callback(nullptr, 0, -1);
                return;
            }
            active_capture_img_callback_ = callback;
        }

        AVCapturePhotoSettings *settings = [AVCapturePhotoSettings photoSettings];
        if (flash_mode_ == FLASH_MODE_AUTO && [as_device(device_) hasFlash]) {
            [settings setFlashMode:AVCaptureFlashModeAuto];
        } else if (flash_mode_ == FLASH_MODE_FORCED && [as_device(device_) hasFlash]) {
            [settings setFlashMode:AVCaptureFlashModeOn];
        } else {
            [settings setFlashMode:AVCaptureFlashModeOff];
        }

        EKA2L1CameraPhotoDelegate *delegate = [[EKA2L1CameraPhotoDelegate alloc] init];
        delegate.owner = this;
        delegate.format = format;
        delegate.targetSize = output_sizes_[resolution_index];

        AVCaptureSession *session = as_session(session_);
        if (![session isRunning]) {
            [session startRunning];
        }

        [as_photo_output(photo_output_) capturePhotoWithSettings:settings delegate:delegate];
    }

    void instance_ios::receive_viewfinder_feed(const eka2l1::vec2 &size, const frame_format format,
        camera_wants_new_frame_callback new_frame_needed_callback,
        camera_capture_image_done_callback new_frame_come_callback) {
        if (!new_frame_needed_callback || !new_frame_come_callback || size.x <= 0 || size.y <= 0) {
            return;
        }

        {
            const std::lock_guard<std::mutex> guard(collection_->reserve_lock_);
            if (collection_->current_reserved_[index_] != this) {
                return;
            }
        }

        if (!ensure_video_output(size, format)) {
            new_frame_come_callback(nullptr, 0, -1);
            return;
        }

        {
            const std::lock_guard<std::mutex> guard(callback_lock_);
            active_frame_viewfinder_callback_ = new_frame_come_callback;
            wants_new_frame_callback_ = new_frame_needed_callback;
            viewfinder_size_ = size;
            viewfinder_format_ = format;
        }

        viewfinder_active_ = true;
        AVCaptureSession *session = as_session(session_);
        if (![session isRunning]) {
            [session startRunning];
        }
    }

    void instance_ios::stop_viewfinder_feed() {
        {
            const std::lock_guard<std::mutex> guard(callback_lock_);
            active_frame_viewfinder_callback_ = nullptr;
            wants_new_frame_callback_ = nullptr;
        }

        viewfinder_active_ = false;
        if (video_output_) {
            [as_video_output(video_output_) setSampleBufferDelegate:nil queue:nil];
            if (session_) {
                [as_session(session_) removeOutput:as_video_output(video_output_)];
            }
#if !__has_feature(objc_arc)
            [as_video_output(video_output_) release];
            [reinterpret_cast<EKA2L1CameraVideoDelegate *>(video_delegate_) release];
#endif
            video_output_ = nullptr;
            video_delegate_ = nullptr;
        }

        stop_session_if_idle();
    }

    bool instance_ios::wants_viewfinder_frame() {
        const std::lock_guard<std::mutex> guard(callback_lock_);
        return wants_new_frame_callback_ ? wants_new_frame_callback_() : false;
    }

    void instance_ios::deliver_viewfinder_frame(const void *bgra_data, int width, int height, int stride) {
        camera_capture_image_done_callback callback;
        eka2l1::vec2 target_size;
        frame_format format;

        {
            const std::lock_guard<std::mutex> guard(callback_lock_);
            callback = active_frame_viewfinder_callback_;
            target_size = viewfinder_size_;
            format = viewfinder_format_;
        }

        if (!callback) {
            return;
        }

        std::vector<std::uint8_t> converted = convert_bgra_to_format(
            reinterpret_cast<const std::uint8_t *>(bgra_data), width, height, stride,
            target_size.x, target_size.y, format);
        callback(converted.data(), converted.size(), converted.empty() ? -1 : 0);
    }

    void instance_ios::deliver_capture_data(const void *data, std::size_t size, const eka2l1::vec2 &size_pixels,
        const frame_format format, int error) {
        camera_capture_image_done_callback callback;
        {
            const std::lock_guard<std::mutex> guard(callback_lock_);
            callback = active_capture_img_callback_;
            active_capture_img_callback_ = nullptr;
        }

        if (!callback) {
            stop_session_if_idle();
            return;
        }

        if (error < 0 || !data || size == 0) {
            callback(nullptr, 0, -1);
            stop_session_if_idle();
            return;
        }

        if ((format == FRAME_FORMAT_JPEG) || (format == FRAME_FORMAT_EXIF)) {
            callback(data, size, 0);
            stop_session_if_idle();
            return;
        }

        NSData *jpeg_data = [NSData dataWithBytes:data length:size];
        std::vector<std::uint8_t> bgra = bgra_from_image_data(jpeg_data, size_pixels);
        std::vector<std::uint8_t> converted = convert_bgra_to_format(bgra.data(), size_pixels.x, size_pixels.y,
            size_pixels.x * 4, size_pixels.x, size_pixels.y, format);

        callback(converted.data(), converted.size(), converted.empty() ? -1 : 0);
        stop_session_if_idle();
    }
}
