/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#include <ios/input_dialog.h>

#include <common/cvt.h>

#import <UIKit/UIKit.h>

namespace {
    eka2l1::drivers::ui::input_dialog_complete_callback input_callback;
    eka2l1::drivers::ui::yes_no_dialog_complete_callback question_callback;
    UIAlertController *input_alert;
    UIAlertController *question_alert;

    UIViewController *visible_controller(UIViewController *controller) {
        if (!controller) {
            return nil;
        }

        while (controller.presentedViewController) {
            controller = controller.presentedViewController;
        }

        if ([controller isKindOfClass:[UINavigationController class]]) {
            return visible_controller([(UINavigationController *)controller visibleViewController]);
        }

        if ([controller isKindOfClass:[UITabBarController class]]) {
            return visible_controller([(UITabBarController *)controller selectedViewController]);
        }

        return controller;
    }

    UIViewController *presenting_controller() {
        for (UIWindow *window in UIApplication.sharedApplication.windows) {
            if (window.isKeyWindow) {
                return visible_controller(window.rootViewController);
            }
        }

        return visible_controller(UIApplication.sharedApplication.windows.firstObject.rootViewController);
    }

    NSString *ns_from_u16(const std::u16string &text) {
        const std::string utf8 = eka2l1::common::ucs2_to_utf8(text);
        return [NSString stringWithUTF8String:utf8.c_str()];
    }
}

namespace eka2l1::drivers::ui {
    bool open_input_view(const std::u16string &initial_text, const int max_len,
        input_dialog_complete_callback complete_callback) {
        if (input_callback) {
            return false;
        }

        input_callback = complete_callback;
        NSString *initial = ns_from_u16(initial_text);
        dispatch_async(dispatch_get_main_queue(), ^{
            UIViewController *presenter = presenting_controller();
            if (!presenter || input_alert) {
                if (input_callback) {
                    input_callback(u"");
                    input_callback = nullptr;
                }
                return;
            }

            UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Enter Text"
                                                                           message:nil
                                                                    preferredStyle:UIAlertControllerStyleAlert];
            [alert addTextFieldWithConfigurationHandler:^(UITextField *textField) {
                textField.text = initial;
            }];

            [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                                      style:UIAlertActionStyleCancel
                                                    handler:^(__unused UIAlertAction *action) {
                                                        if (input_callback) {
                                                            input_callback(u"");
                                                            input_callback = nullptr;
                                                        }
                                                        input_alert = nil;
                                                    }]];

            [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                                      style:UIAlertActionStyleDefault
                                                    handler:^(__unused UIAlertAction *action) {
                                                        NSString *value = alert.textFields.firstObject.text ?: @"";
                                                        std::u16string result = eka2l1::common::utf8_to_ucs2([value UTF8String]);
                                                        if (max_len > 0 && result.size() > static_cast<std::size_t>(max_len)) {
                                                            result.resize(static_cast<std::size_t>(max_len));
                                                        }

                                                        if (input_callback) {
                                                            input_callback(result);
                                                            input_callback = nullptr;
                                                        }
                                                        input_alert = nil;
                                                    }]];

            input_alert = alert;
            [presenter presentViewController:alert
                                    animated:YES
                                  completion:^{
                                      [alert.textFields.firstObject becomeFirstResponder];
                                  }];
        });
        return true;
    }

    void close_input_view() {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (input_callback) {
                input_callback(u"");
                input_callback = nullptr;
            }

            [input_alert dismissViewControllerAnimated:YES completion:nil];
            input_alert = nil;
        });
    }

    void show_yes_no_dialog(const std::u16string &text, const std::u16string &button1_text,
        const std::u16string &button2_text, yes_no_dialog_complete_callback complete_callback) {
        if (question_callback) {
            return;
        }

        question_callback = complete_callback;
        NSString *message = ns_from_u16(text);
        NSString *yes = button1_text.empty() ? @"OK" : ns_from_u16(button1_text);
        NSString *no = button2_text.empty() ? @"Cancel" : ns_from_u16(button2_text);

        dispatch_async(dispatch_get_main_queue(), ^{
            UIViewController *presenter = presenting_controller();
            if (!presenter || question_alert) {
                if (question_callback) {
                    question_callback(0);
                    question_callback = nullptr;
                }
                return;
            }

            UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"EKA2L1"
                                                                           message:message
                                                                    preferredStyle:UIAlertControllerStyleAlert];
            [alert addAction:[UIAlertAction actionWithTitle:no
                                                      style:UIAlertActionStyleCancel
                                                    handler:^(__unused UIAlertAction *action) {
                                                        if (question_callback) {
                                                            question_callback(0);
                                                            question_callback = nullptr;
                                                        }
                                                        question_alert = nil;
                                                    }]];
            [alert addAction:[UIAlertAction actionWithTitle:yes
                                                      style:UIAlertActionStyleDefault
                                                    handler:^(__unused UIAlertAction *action) {
                                                        if (question_callback) {
                                                            question_callback(1);
                                                            question_callback = nullptr;
                                                        }
                                                        question_alert = nil;
                                                    }]];

            question_alert = alert;
            [presenter presentViewController:alert animated:YES completion:nil];
        });
    }
}

namespace eka2l1::ios {
    void finish_text_input(const std::u16string &text) {
        if (input_callback) {
            input_callback(text);
            input_callback = nullptr;
        }
    }

    void finish_question_dialog(int result) {
        if (question_callback) {
            question_callback(result);
            question_callback = nullptr;
        }
    }
}
