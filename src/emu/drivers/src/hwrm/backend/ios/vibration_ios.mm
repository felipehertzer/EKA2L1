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

#include <drivers/hwrm/backend/ios/vibration_ios.h>

#import <AudioToolbox/AudioToolbox.h>
#import <UIKit/UIKit.h>

namespace eka2l1::drivers::hwrm {
    void vibrator_ios::vibrate(const std::uint32_t millisecs, const std::int16_t intensity) {
        (void)millisecs;

        dispatch_async(dispatch_get_main_queue(), ^{
            @autoreleasepool {
                if (@available(iOS 10.0, *)) {
                    UIImpactFeedbackStyle style = UIImpactFeedbackStyleMedium;
                    if (intensity <= -50) {
                        style = UIImpactFeedbackStyleLight;
                    } else if (intensity >= 50) {
                        style = UIImpactFeedbackStyleHeavy;
                    }

                    UIImpactFeedbackGenerator *generator = [[UIImpactFeedbackGenerator alloc] initWithStyle:style];
                    [generator prepare];
                    [generator impactOccurred];
#if !__has_feature(objc_arc)
                    [generator release];
#endif
                } else {
                    AudioServicesPlaySystemSound(kSystemSoundID_Vibrate);
                }
            }
        });
    }

    void vibrator_ios::stop_vibrate() {
        // Public iOS haptic APIs do not expose cancellation for a fired feedback event.
    }
}
