#include <common/applauncher.h>

#import <UIKit/UIKit.h>

namespace eka2l1::common {
    bool launch_browser(const std::string &url) {
        @autoreleasepool {
            NSString *url_string = [NSString stringWithUTF8String:url.c_str()];
            if (!url_string) {
                return false;
            }

            NSURL *target_url = [NSURL URLWithString:url_string];
            if (!target_url) {
                return false;
            }

            UIApplication *application = [UIApplication sharedApplication];
            if (![application canOpenURL:target_url]) {
                return false;
            }

            dispatch_async(dispatch_get_main_queue(), ^{
                [application openURL:target_url options:@{} completionHandler:nil];
            });

            return true;
        }
    }
}
