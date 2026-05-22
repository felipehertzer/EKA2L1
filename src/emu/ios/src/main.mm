/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#include <common/fileutils.h>
#include <ios/state.h>
#include <ios/thread.h>

#import <GameController/GameController.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace {
    static constexpr int EKA2L1_KEY_CLEAR = 0x01;
    static constexpr int EKA2L1_KEY_DIAL = 10;
    static constexpr int EKA2L1_KEY_LEFT = 0x0E;
    static constexpr int EKA2L1_KEY_RIGHT = 0x0F;
    static constexpr int EKA2L1_KEY_UP = 0x10;
    static constexpr int EKA2L1_KEY_DOWN = 0x11;
    static constexpr int EKA2L1_KEY_STAR = '*';
    static constexpr int EKA2L1_KEY_POUND = 0x7F;
    static constexpr int EKA2L1_KEY_SOFT_LEFT = 0xA4;
    static constexpr int EKA2L1_KEY_SOFT_RIGHT = 0xA5;
    static constexpr int EKA2L1_KEY_FIRE = 0xA7;

    std::unique_ptr<eka2l1::ios::emulator> g_state;

    NSString *const EKA2L1PrefScreenBackgroundColor = @"ScreenBackgroundColor";
    NSString *const EKA2L1PrefScreenScaleRatio = @"ScreenScaleRatio";
    NSString *const EKA2L1PrefScreenScaleType = @"ScreenScaleType";
    NSString *const EKA2L1PrefScreenGravity = @"ScreenGravity";
    NSString *const EKA2L1PrefBackgroundImagePath = @"BackgroundImagePath";
    NSString *const EKA2L1PrefBackgroundImageOpacity = @"BackgroundImageOpacity";
    NSString *const EKA2L1PrefBackgroundImageKeepAspect = @"BackgroundImageKeepAspect";
    NSString *const EKA2L1PrefKeypadVisible = @"VirtualKeypadVisible";
    NSString *const EKA2L1PrefKeypadAlpha = @"VirtualKeypadAlpha";
    NSString *const EKA2L1PrefKeypadFeedback = @"VirtualKeypadFeedback";
    NSString *const EKA2L1PrefTouchInput = @"TouchInput";
    NSString *const EKA2L1PrefMountedSdCardBookmark = @"MountedSdCardBookmark";
    NSString *const EKA2L1PrefMountedSdCardPath = @"MountedSdCardPath";

    NSString *documents_path() {
        NSArray<NSURL *> *urls = [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
                                                                        inDomains:NSUserDomainMask];
        return urls.firstObject.path;
    }

    uint32_t unsigned_default(NSString *key, uint32_t fallback) {
        NSNumber *value = [[NSUserDefaults standardUserDefaults] objectForKey:key];
        return value ? static_cast<uint32_t>(value.unsignedIntValue) : fallback;
    }

    float float_default(NSString *key, float fallback) {
        NSNumber *value = [[NSUserDefaults standardUserDefaults] objectForKey:key];
        return value ? value.floatValue : fallback;
    }

    BOOL bool_default(NSString *key, BOOL fallback) {
        NSNumber *value = [[NSUserDefaults standardUserDefaults] objectForKey:key];
        return value ? value.boolValue : fallback;
    }

    void sync_bundle_item(NSString *sourceName, NSString *destName) {
        NSFileManager *fm = [NSFileManager defaultManager];
        NSString *source = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:sourceName];
        NSString *dest = [documents_path() stringByAppendingPathComponent:destName];

        BOOL isDirectory = NO;
        if (![fm fileExistsAtPath:source isDirectory:&isDirectory]) {
            return;
        }

        if (!isDirectory) {
            [fm createDirectoryAtPath:dest.stringByDeletingLastPathComponent
                withIntermediateDirectories:YES
                                 attributes:nil
                                      error:nil];
            [fm removeItemAtPath:dest error:nil];
            NSError *error = nil;
            if (![fm copyItemAtPath:source toPath:dest error:&error]) {
                NSLog(@"Unable to copy %@ into documents: %@", sourceName, error);
            }
            return;
        }

        [fm createDirectoryAtPath:dest withIntermediateDirectories:YES attributes:nil error:nil];
        NSDirectoryEnumerator<NSString *> *enumerator = [fm enumeratorAtPath:source];
        for (NSString *relative in enumerator) {
            NSString *sourcePath = [source stringByAppendingPathComponent:relative];
            NSString *destPath = [dest stringByAppendingPathComponent:relative];

            BOOL entryIsDirectory = NO;
            if (![fm fileExistsAtPath:sourcePath isDirectory:&entryIsDirectory]) {
                continue;
            }

            if (entryIsDirectory) {
                [fm createDirectoryAtPath:destPath withIntermediateDirectories:YES attributes:nil error:nil];
                continue;
            }

            [fm createDirectoryAtPath:destPath.stringByDeletingLastPathComponent
                withIntermediateDirectories:YES
                                 attributes:nil
                                      error:nil];
            [fm removeItemAtPath:destPath error:nil];
            NSError *error = nil;
            if (![fm copyItemAtPath:sourcePath toPath:destPath error:&error]) {
                NSLog(@"Unable to copy %@ into documents: %@", sourcePath, error);
            }
        }
    }

    void sync_bundle_item(NSString *name) {
        sync_bundle_item(name, name);
    }

    void bootstrap_working_directory() {
        NSFileManager *fm = [NSFileManager defaultManager];
        NSString *docs = documents_path();

        [fm createDirectoryAtPath:docs withIntermediateDirectories:YES attributes:nil error:nil];
        sync_bundle_item(@"eka2l1_resources", @"resources");
        sync_bundle_item(@"compat");
        sync_bundle_item(@"patch");
        sync_bundle_item(@"scripts");

        eka2l1::common::set_current_directory([docs UTF8String]);
    }

    int key_for_press(UIPress *press) {
        if (@available(iOS 13.4, *)) {
            UIKey *key = press.key;
            if (!key) {
                return -1;
            }

            switch (key.keyCode) {
            case UIKeyboardHIDUsageKeyboardUpArrow:
                return EKA2L1_KEY_UP;
            case UIKeyboardHIDUsageKeyboardDownArrow:
                return EKA2L1_KEY_DOWN;
            case UIKeyboardHIDUsageKeyboardLeftArrow:
                return EKA2L1_KEY_LEFT;
            case UIKeyboardHIDUsageKeyboardRightArrow:
                return EKA2L1_KEY_RIGHT;
            case UIKeyboardHIDUsageKeyboardReturnOrEnter:
            case UIKeyboardHIDUsageKeypadEnter:
                return EKA2L1_KEY_FIRE;
            case UIKeyboardHIDUsageKeyboardDeleteOrBackspace:
                return EKA2L1_KEY_CLEAR;
            case UIKeyboardHIDUsageKeyboardTab:
                return 0x02;
            case UIKeyboardHIDUsageKeyboardEscape:
                return 0x04;
            case UIKeyboardHIDUsageKeyboardSpacebar:
                return 0x05;
            case UIKeyboardHIDUsageKeyboardDeleteForward:
                return 0x0D;
            case UIKeyboardHIDUsageKeyboardF1:
                return EKA2L1_KEY_SOFT_LEFT;
            case UIKeyboardHIDUsageKeyboardF2:
                return EKA2L1_KEY_SOFT_RIGHT;
            case UIKeyboardHIDUsageKeyboardF3:
                return EKA2L1_KEY_DIAL;
            case UIKeyboardHIDUsageKeyboardF4:
                return EKA2L1_KEY_CLEAR;
            default:
                break;
            }

            NSString *chars = key.charactersIgnoringModifiers;
            if (chars.length != 1) {
                return -1;
            }

            unichar ch = [chars characterAtIndex:0];
            if ((ch >= 'a') && (ch <= 'z')) {
                ch = static_cast<unichar>(std::toupper(static_cast<unsigned char>(ch)));
            }

            if (((ch >= 'A') && (ch <= 'Z')) || ((ch >= '0') && (ch <= '9')) || (ch == '*')) {
                return static_cast<int>(ch);
            }

            if (ch == '#') {
                return EKA2L1_KEY_POUND;
            }
        }

        return -1;
    }
}

@interface EKA2L1VulkanView : UIView
@end

@implementation EKA2L1VulkanView
+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.multipleTouchEnabled = YES;
        self.contentScaleFactor = UIScreen.mainScreen.nativeScale;
        CAMetalLayer *layer = (CAMetalLayer *)self.layer;
        layer.opaque = YES;
        layer.contentsScale = self.contentScaleFactor;
    }
    return self;
}
@end

typedef NS_ENUM(NSInteger, EKA2L1ImportAction) {
    EKA2L1ImportActionNone = 0,
    EKA2L1ImportActionAppPackage,
    EKA2L1ImportActionDeviceVpl,
    EKA2L1ImportActionDeviceRom,
    EKA2L1ImportActionDeviceRpkg,
    EKA2L1ImportActionDataFolder,
    EKA2L1ImportActionNGageFolder,
    EKA2L1ImportActionNGageLicense,
    EKA2L1ImportActionMountSdCard,
    EKA2L1ImportActionImportSdCard,
    EKA2L1ImportActionBackgroundImage
};

@interface EKA2L1ViewController : UIViewController <UIDocumentPickerDelegate>
@property (nonatomic, strong) EKA2L1VulkanView *renderView;
@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, strong) UIToolbar *toolbar;
@property (nonatomic, assign) EKA2L1ImportAction pendingImportAction;
@property (nonatomic, copy) NSString *pendingRomPath;
@property (nonatomic, assign) uint32_t screenBackgroundColor;
@property (nonatomic, assign) uint32_t screenScaleRatio;
@property (nonatomic, assign) uint32_t screenScaleType;
@property (nonatomic, assign) uint32_t screenGravity;
@property (nonatomic, copy) NSString *backgroundImagePath;
@property (nonatomic, assign) float backgroundImageOpacity;
@property (nonatomic, assign) BOOL keepBackgroundAspect;
@property (nonatomic, strong) NSMutableArray<NSURL *> *securityScopedURLs;
@property (nonatomic, strong) NSURL *mountedSdCardURL;
@property (nonatomic, strong) UIView *keypadView;
@property (nonatomic, assign) BOOL keypadVisible;
@property (nonatomic, assign) CGFloat keypadAlpha;
@property (nonatomic, assign) BOOL keypadFeedbackEnabled;
@property (nonatomic, assign) BOOL touchInputEnabled;
@property (nonatomic, strong) UIImpactFeedbackGenerator *keyFeedback;
@property (nonatomic, strong) NSMutableDictionary<NSValue *, NSMutableSet<NSNumber *> *> *controllerHeldKeys;
@end

@implementation EKA2L1ViewController

- (void)viewDidLoad {
    [super viewDidLoad];

    self.view.backgroundColor = UIColor.blackColor;
    self.renderView = [[EKA2L1VulkanView alloc] initWithFrame:self.view.bounds];
    self.renderView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:self.renderView];

    self.statusLabel = [[UILabel alloc] initWithFrame:CGRectInset(self.view.bounds, 24, 80)];
    self.statusLabel.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.statusLabel.textColor = UIColor.whiteColor;
    self.statusLabel.numberOfLines = 0;
    self.statusLabel.textAlignment = NSTextAlignmentCenter;
    self.statusLabel.text = @"Starting EKA2L1";
    [self.view addSubview:self.statusLabel];

    self.pendingImportAction = EKA2L1ImportActionNone;
    self.screenBackgroundColor = 0xD0D0D0;
    self.screenScaleRatio = 100;
    self.screenScaleType = 1;
    self.screenGravity = 2;
    self.backgroundImagePath = @"";
    self.backgroundImageOpacity = 0.5f;
    self.keepBackgroundAspect = YES;
    self.securityScopedURLs = [NSMutableArray array];
    self.touchInputEnabled = YES;
    self.keypadVisible = YES;
    self.keypadAlpha = 0.58f;
    self.keypadFeedbackEnabled = YES;
    self.keyFeedback = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
    self.controllerHeldKeys = [NSMutableDictionary dictionary];
    [self loadPersistedSettings];
    [self configureToolbar];
    [self configureVirtualKeypad];
    [self configureGameControllers];

    UILongPressGestureRecognizer *longPress = [[UILongPressGestureRecognizer alloc] initWithTarget:self
                                                                                            action:@selector(showAppList:)];
    longPress.minimumPressDuration = 0.6;
    [self.view addGestureRecognizer:longPress];

    bootstrap_working_directory();

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        g_state = std::make_unique<eka2l1::ios::emulator>();
        const bool ready = eka2l1::ios::emulator_entry(*g_state);

        dispatch_async(dispatch_get_main_queue(), ^{
            [self restorePersistentMounts];
            [self applyScreenParams];
            [self updateSurface];
            self.statusLabel.text = ready
                ? @"Long press to choose an installed Symbian app"
                : @"No installed device found. Copy an existing EKA2L1 data folder into Files > EKA2L1, then relaunch.";
        });
    });
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [self becomeFirstResponder];
}

- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (void)loadPersistedSettings {
    self.screenBackgroundColor = unsigned_default(EKA2L1PrefScreenBackgroundColor, self.screenBackgroundColor);
    self.screenScaleRatio = unsigned_default(EKA2L1PrefScreenScaleRatio, self.screenScaleRatio);
    self.screenScaleType = unsigned_default(EKA2L1PrefScreenScaleType, self.screenScaleType);
    self.screenGravity = unsigned_default(EKA2L1PrefScreenGravity, self.screenGravity);
    self.backgroundImagePath = [[NSUserDefaults standardUserDefaults] stringForKey:EKA2L1PrefBackgroundImagePath] ?: @"";
    self.backgroundImageOpacity = float_default(EKA2L1PrefBackgroundImageOpacity, self.backgroundImageOpacity);
    self.keepBackgroundAspect = bool_default(EKA2L1PrefBackgroundImageKeepAspect, self.keepBackgroundAspect);
    self.keypadVisible = bool_default(EKA2L1PrefKeypadVisible, self.keypadVisible);
    self.keypadAlpha = static_cast<CGFloat>(float_default(EKA2L1PrefKeypadAlpha, static_cast<float>(self.keypadAlpha)));
    self.keypadFeedbackEnabled = bool_default(EKA2L1PrefKeypadFeedback, self.keypadFeedbackEnabled);
    self.touchInputEnabled = bool_default(EKA2L1PrefTouchInput, self.touchInputEnabled);
}

- (void)saveDisplaySettings {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setInteger:self.screenBackgroundColor forKey:EKA2L1PrefScreenBackgroundColor];
    [defaults setInteger:self.screenScaleRatio forKey:EKA2L1PrefScreenScaleRatio];
    [defaults setInteger:self.screenScaleType forKey:EKA2L1PrefScreenScaleType];
    [defaults setInteger:self.screenGravity forKey:EKA2L1PrefScreenGravity];
    [defaults setFloat:self.backgroundImageOpacity forKey:EKA2L1PrefBackgroundImageOpacity];
    [defaults setBool:self.keepBackgroundAspect forKey:EKA2L1PrefBackgroundImageKeepAspect];
    if (self.backgroundImagePath.length > 0) {
        [defaults setObject:self.backgroundImagePath forKey:EKA2L1PrefBackgroundImagePath];
    } else {
        [defaults removeObjectForKey:EKA2L1PrefBackgroundImagePath];
    }
}

- (void)saveInputSettings {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setBool:self.keypadVisible forKey:EKA2L1PrefKeypadVisible];
    [defaults setFloat:static_cast<float>(self.keypadAlpha) forKey:EKA2L1PrefKeypadAlpha];
    [defaults setBool:self.keypadFeedbackEnabled forKey:EKA2L1PrefKeypadFeedback];
    [defaults setBool:self.touchInputEnabled forKey:EKA2L1PrefTouchInput];
}

- (void)saveEmulatorConfig {
    if (g_state) {
        g_state->conf.serialize();
    }
}

- (UIBarButtonItem *)toolbarItemWithTitle:(NSString *)title symbol:(NSString *)symbol action:(SEL)action {
    UIImage *image = nil;
    if (@available(iOS 13.0, *)) {
        image = [UIImage systemImageNamed:symbol];
    }

    if (image) {
        UIBarButtonItem *item = [[UIBarButtonItem alloc] initWithImage:image
                                                                 style:UIBarButtonItemStylePlain
                                                                target:self
                                                                action:action];
        item.accessibilityLabel = title;
        return item;
    }

    return [[UIBarButtonItem alloc] initWithTitle:title style:UIBarButtonItemStylePlain target:self action:action];
}

- (void)configureToolbar {
    const CGFloat height = 52.0;
    CGRect frame = CGRectMake(0, CGRectGetHeight(self.view.bounds) - height, CGRectGetWidth(self.view.bounds), height);
    self.toolbar = [[UIToolbar alloc] initWithFrame:frame];
    self.toolbar.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleTopMargin;
    self.toolbar.translucent = YES;

    UIBarButtonItem *flex = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace
                                                                          target:nil
                                                                          action:nil];
    self.toolbar.items = @[
        [self toolbarItemWithTitle:@"Apps"
                            symbol:@"square.grid.2x2"
                            action:@selector(showAppsFromToolbar)],
        flex,
        [self toolbarItemWithTitle:@"Import"
                            symbol:@"square.and.arrow.down"
                            action:@selector(showImportMenu)],
        flex,
        [self toolbarItemWithTitle:@"Devices"
                            symbol:@"iphone"
                            action:@selector(showDeviceMenu)],
        flex,
        [self toolbarItemWithTitle:@"Packages"
                            symbol:@"shippingbox"
                            action:@selector(showPackageMenu)],
        flex,
        [self toolbarItemWithTitle:@"Settings"
                            symbol:@"gearshape"
                            action:@selector(showSettingsMenu)],
        flex,
        [self toolbarItemWithTitle:@"Keys"
                            symbol:@"keyboard"
                            action:@selector(toggleVirtualKeypad)],
        flex,
        [self toolbarItemWithTitle:@"Shot"
                            symbol:@"camera"
                            action:@selector(takeScreenshot)]
    ];

    [self.view addSubview:self.toolbar];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self layoutVirtualKeypad];
    [self updateSurface];
}

- (UIButton *)addKeyButtonWithTitle:(NSString *)title key:(int)key {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    button.tag = key;
    button.autoresizingMask = UIViewAutoresizingNone;
    button.backgroundColor = [UIColor colorWithWhite:0.02 alpha:self.keypadAlpha];
    button.tintColor = UIColor.whiteColor;
    button.layer.cornerRadius = 8.0;
    button.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.20].CGColor;
    button.layer.borderWidth = 1.0;
    button.titleLabel.font = [UIFont boldSystemFontOfSize:16.0];
    button.accessibilityLabel = title;
    [button setTitle:title forState:UIControlStateNormal];
    [button addTarget:self action:@selector(keypadButtonDown:) forControlEvents:UIControlEventTouchDown];
    [button addTarget:self action:@selector(keypadButtonUp:) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchCancel];
    [self.keypadView addSubview:button];
    return button;
}

- (void)configureVirtualKeypad {
    self.keypadView = [[UIView alloc] initWithFrame:self.view.bounds];
    self.keypadView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.keypadView.backgroundColor = UIColor.clearColor;
    self.keypadView.userInteractionEnabled = YES;
    [self.view insertSubview:self.keypadView belowSubview:self.toolbar];

    [self addKeyButtonWithTitle:@"L" key:EKA2L1_KEY_SOFT_LEFT];
    [self addKeyButtonWithTitle:@"R" key:EKA2L1_KEY_SOFT_RIGHT];
    [self addKeyButtonWithTitle:@"D" key:EKA2L1_KEY_DIAL];
    [self addKeyButtonWithTitle:@"C" key:EKA2L1_KEY_CLEAR];
    [self addKeyButtonWithTitle:@"↑" key:EKA2L1_KEY_UP];
    [self addKeyButtonWithTitle:@"↓" key:EKA2L1_KEY_DOWN];
    [self addKeyButtonWithTitle:@"←" key:EKA2L1_KEY_LEFT];
    [self addKeyButtonWithTitle:@"→" key:EKA2L1_KEY_RIGHT];
    [self addKeyButtonWithTitle:@"F" key:EKA2L1_KEY_FIRE];
    [self addKeyButtonWithTitle:@"1" key:'1'];
    [self addKeyButtonWithTitle:@"2" key:'2'];
    [self addKeyButtonWithTitle:@"3" key:'3'];
    [self addKeyButtonWithTitle:@"4" key:'4'];
    [self addKeyButtonWithTitle:@"5" key:'5'];
    [self addKeyButtonWithTitle:@"6" key:'6'];
    [self addKeyButtonWithTitle:@"7" key:'7'];
    [self addKeyButtonWithTitle:@"8" key:'8'];
    [self addKeyButtonWithTitle:@"9" key:'9'];
    [self addKeyButtonWithTitle:@"*" key:EKA2L1_KEY_STAR];
    [self addKeyButtonWithTitle:@"0" key:'0'];
    [self addKeyButtonWithTitle:@"#" key:EKA2L1_KEY_POUND];

    self.keypadView.hidden = !self.keypadVisible;
}

- (UIButton *)keypadButtonForKey:(int)key {
    return (UIButton *)[self.keypadView viewWithTag:key];
}

- (void)placeKey:(int)key x:(CGFloat)x y:(CGFloat)y size:(CGFloat)size {
    UIButton *button = [self keypadButtonForKey:key];
    button.frame = CGRectMake(x, y, size, size);
}

- (void)layoutVirtualKeypad {
    if (!self.keypadView) {
        return;
    }

    self.keypadView.frame = self.view.bounds;
    self.keypadView.hidden = !self.keypadVisible;

    const CGFloat width = CGRectGetWidth(self.view.bounds);
    const CGFloat height = CGRectGetHeight(self.view.bounds);
    const CGFloat safeBottom = self.view.safeAreaInsets.bottom;
    const CGFloat toolbarTop = self.toolbar ? CGRectGetMinY(self.toolbar.frame) : (height - safeBottom);
    const CGFloat compactSize = std::max<CGFloat>(36.0, std::min<CGFloat>(48.0, width / 9.8));
    const CGFloat gap = 8.0;
    const CGFloat panelHeight = compactSize * 4.0 + gap * 3.0;
    const CGFloat y0 = std::max<CGFloat>(self.view.safeAreaInsets.top + 8.0, toolbarTop - panelHeight - 10.0);
    const CGFloat leftX = 12.0;
    const CGFloat rightX = std::max<CGFloat>(leftX + compactSize * 3.0 + gap * 5.0,
        width - (compactSize * 3.0 + gap * 2.0) - 12.0);

    [self placeKey:EKA2L1_KEY_SOFT_LEFT x:leftX y:y0 size:compactSize];
    [self placeKey:EKA2L1_KEY_SOFT_RIGHT x:leftX + (compactSize + gap) * 2.0 y:y0 size:compactSize];
    [self placeKey:EKA2L1_KEY_UP x:leftX + compactSize + gap y:y0 + compactSize + gap size:compactSize];
    [self placeKey:EKA2L1_KEY_LEFT x:leftX y:y0 + (compactSize + gap) * 2.0 size:compactSize];
    [self placeKey:EKA2L1_KEY_FIRE x:leftX + compactSize + gap y:y0 + (compactSize + gap) * 2.0 size:compactSize];
    [self placeKey:EKA2L1_KEY_RIGHT x:leftX + (compactSize + gap) * 2.0 y:y0 + (compactSize + gap) * 2.0 size:compactSize];
    [self placeKey:EKA2L1_KEY_DIAL x:leftX y:y0 + (compactSize + gap) * 3.0 size:compactSize];
    [self placeKey:EKA2L1_KEY_DOWN x:leftX + compactSize + gap y:y0 + (compactSize + gap) * 3.0 size:compactSize];
    [self placeKey:EKA2L1_KEY_CLEAR x:leftX + (compactSize + gap) * 2.0 y:y0 + (compactSize + gap) * 3.0 size:compactSize];

    const int keys[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', EKA2L1_KEY_STAR, '0', EKA2L1_KEY_POUND };
    for (int i = 0; i < 12; i++) {
        const int row = i / 3;
        const int col = i % 3;
        [self placeKey:keys[i]
                     x:rightX + (compactSize + gap) * col
                     y:y0 + (compactSize + gap) * row
                  size:compactSize];
    }
}

- (void)sendKey:(int)key state:(eka2l1::drivers::key_state)state {
    if (!g_state) {
        return;
    }

    eka2l1::ios::press_key(*g_state, key, static_cast<int>(state));
}

- (void)keypadButtonDown:(UIButton *)sender {
    if (self.keypadFeedbackEnabled) {
        [self.keyFeedback impactOccurred];
    }
    [self sendKey:static_cast<int>(sender.tag) state:eka2l1::drivers::key_state::pressed];
}

- (void)keypadButtonUp:(UIButton *)sender {
    [self sendKey:static_cast<int>(sender.tag) state:eka2l1::drivers::key_state::released];
}

- (void)toggleVirtualKeypad {
    self.keypadVisible = !self.keypadVisible;
    self.keypadView.hidden = !self.keypadVisible;
    [self saveInputSettings];
    [self becomeFirstResponder];
}

- (NSValue *)controllerKey:(GCController *)controller {
    return [NSValue valueWithNonretainedObject:controller];
}

- (void)setController:(GCController *)controller key:(int)key pressed:(BOOL)pressed {
    if (!controller) {
        return;
    }

    NSValue *controllerKey = [self controllerKey:controller];
    NSMutableSet<NSNumber *> *held = self.controllerHeldKeys[controllerKey];
    if (!held) {
        held = [NSMutableSet set];
        self.controllerHeldKeys[controllerKey] = held;
    }

    NSNumber *boxedKey = @(key);
    const BOOL alreadyPressed = [held containsObject:boxedKey];
    if (pressed == alreadyPressed) {
        return;
    }

    if (pressed) {
        [held addObject:boxedKey];
        if (self.keypadFeedbackEnabled) {
            [self.keyFeedback impactOccurred];
        }
        [self sendKey:key state:eka2l1::drivers::key_state::pressed];
    } else {
        [held removeObject:boxedKey];
        [self sendKey:key state:eka2l1::drivers::key_state::released];
    }
}

- (void)setController:(GCController *)controller axisX:(float)x axisY:(float)y {
    static constexpr float threshold = 0.45f;
    [self setController:controller key:EKA2L1_KEY_LEFT pressed:x <= -threshold];
    [self setController:controller key:EKA2L1_KEY_RIGHT pressed:x >= threshold];
    [self setController:controller key:EKA2L1_KEY_UP pressed:y >= threshold];
    [self setController:controller key:EKA2L1_KEY_DOWN pressed:y <= -threshold];
}

- (void)releaseControllerKeys:(GCController *)controller {
    NSValue *controllerKey = [self controllerKey:controller];
    NSMutableSet<NSNumber *> *held = self.controllerHeldKeys[controllerKey];
    if (!held) {
        return;
    }

    NSArray<NSNumber *> *keys = held.allObjects;
    [held removeAllObjects];
    [self.controllerHeldKeys removeObjectForKey:controllerKey];

    for (NSNumber *key in keys) {
        [self sendKey:key.intValue state:eka2l1::drivers::key_state::released];
    }
}

- (void)configureController:(GCController *)controller {
    if (!controller) {
        return;
    }

    EKA2L1ViewController *callbackTarget = self;
    GCController *callbackController = controller;

    GCExtendedGamepad *extended = controller.extendedGamepad;
    if (extended) {
        extended.dpad.valueChangedHandler = ^(GCControllerDirectionPad *dpad, float xValue, float yValue) {
            [callbackTarget setController:callbackController axisX:xValue axisY:yValue];
        };
        extended.leftThumbstick.valueChangedHandler = ^(GCControllerDirectionPad *dpad, float xValue, float yValue) {
            [callbackTarget setController:callbackController axisX:xValue axisY:yValue];
        };
        extended.buttonA.pressedChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
            [callbackTarget setController:callbackController key:EKA2L1_KEY_FIRE pressed:pressed];
        };
        extended.buttonB.pressedChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
            [callbackTarget setController:callbackController key:EKA2L1_KEY_CLEAR pressed:pressed];
        };
        extended.buttonX.pressedChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
            [callbackTarget setController:callbackController key:EKA2L1_KEY_SOFT_LEFT pressed:pressed];
        };
        extended.buttonY.pressedChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
            [callbackTarget setController:callbackController key:EKA2L1_KEY_SOFT_RIGHT pressed:pressed];
        };
        extended.leftShoulder.pressedChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
            [callbackTarget setController:callbackController key:EKA2L1_KEY_SOFT_LEFT pressed:pressed];
        };
        extended.rightShoulder.pressedChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
            [callbackTarget setController:callbackController key:EKA2L1_KEY_SOFT_RIGHT pressed:pressed];
        };
        extended.leftTrigger.pressedChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
            [callbackTarget setController:callbackController key:EKA2L1_KEY_DIAL pressed:pressed];
        };
        extended.rightTrigger.pressedChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
            [callbackTarget setController:callbackController key:EKA2L1_KEY_FIRE pressed:pressed];
        };
        return;
    }

    GCMicroGamepad *micro = controller.microGamepad;
    if (micro) {
        micro.reportsAbsoluteDpadValues = YES;
        micro.dpad.valueChangedHandler = ^(GCControllerDirectionPad *dpad, float xValue, float yValue) {
            [callbackTarget setController:callbackController axisX:xValue axisY:yValue];
        };
        micro.buttonA.pressedChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
            [callbackTarget setController:callbackController key:EKA2L1_KEY_FIRE pressed:pressed];
        };
        micro.buttonX.pressedChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
            [callbackTarget setController:callbackController key:EKA2L1_KEY_SOFT_LEFT pressed:pressed];
        };
    }
}

- (void)configureGameControllers {
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    [center addObserverForName:GCControllerDidConnectNotification
                        object:nil
                         queue:NSOperationQueue.mainQueue
                    usingBlock:^(NSNotification *notification) {
                        GCController *controller = (GCController *)notification.object;
                        [self configureController:controller];
                    }];
    [center addObserverForName:GCControllerDidDisconnectNotification
                        object:nil
                         queue:NSOperationQueue.mainQueue
                    usingBlock:^(NSNotification *notification) {
                        GCController *controller = (GCController *)notification.object;
                        [self releaseControllerKeys:controller];
                    }];

    for (GCController *controller in GCController.controllers) {
        [self configureController:controller];
    }
}

- (void)updateSurface {
    if (!g_state) {
        return;
    }

    CGFloat scale = self.renderView.contentScaleFactor;
    CGSize size = self.renderView.bounds.size;
    eka2l1::ios::surface_changed(*g_state, self.renderView.layer,
        static_cast<int>(size.width * scale), static_cast<int>(size.height * scale),
        static_cast<float>(scale));
}

- (void)presentAlertWithTitle:(NSString *)title message:(NSString *)message {
    dispatch_async(dispatch_get_main_queue(), ^{
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:title
                                                                       message:message
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:alert animated:YES completion:nil];
    });
}

- (void)presentTextPromptWithTitle:(NSString *)title initialText:(NSString *)initialText
                        completion:(void (^)(NSString *text))completion {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:title
                                                                   message:nil
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addTextFieldWithConfigurationHandler:^(UITextField *textField) {
        textField.text = initialText;
        textField.clearButtonMode = UITextFieldViewModeWhileEditing;
    }];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                NSString *text = alert.textFields.firstObject.text ?: @"";
                                                completion(text);
                                            }]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)presentSheet:(UIAlertController *)sheet {
    UIPopoverPresentationController *popover = sheet.popoverPresentationController;
    popover.sourceView = self.view;
    popover.sourceRect = CGRectMake(CGRectGetMidX(self.view.bounds), CGRectGetMidY(self.view.bounds), 1, 1);
    [self presentViewController:sheet animated:YES completion:nil];
}

- (BOOL)hasLauncher {
    if (g_state && g_state->launcher) {
        return YES;
    }

    [self presentAlertWithTitle:@"EKA2L1" message:@"Emulator is still starting."];
    return NO;
}

- (void)updateKeypadAppearance {
    for (UIView *subview in self.keypadView.subviews) {
        if ([subview isKindOfClass:UIButton.class]) {
            subview.backgroundColor = [UIColor colorWithWhite:0.02 alpha:self.keypadAlpha];
        }
    }
}

- (NSString *)mmcIdFromPath:(NSString *)path {
    NSError *error = nil;
    NSRegularExpression *regex = [NSRegularExpression regularExpressionWithPattern:
            @"[0-9a-fA-F]+-[0-9a-fA-F]+-[0-9a-fA-F]+-[0-9a-fA-F]+"
                                                                           options:NSRegularExpressionCaseInsensitive
                                                                             error:&error];
    if (error) {
        return nil;
    }

    NSString *folderName = path.lastPathComponent ?: @"";
    NSTextCheckingResult *match = [regex firstMatchInString:folderName
                                                    options:0
                                                      range:NSMakeRange(0, folderName.length)];
    return match ? [folderName substringWithRange:match.range] : nil;
}

- (NSString *)applyMmcIdFromPath:(NSString *)path persist:(BOOL)persist {
    NSString *mmcId = [self mmcIdFromPath:path];
    if (!mmcId || !g_state) {
        return nil;
    }

    std::string mmc_id = [mmcId UTF8String];
    g_state->conf.current_mmc_id = mmc_id;
    if (g_state->launcher) {
        g_state->launcher->set_current_mmc_id(mmc_id);
    }
    if (persist) {
        [self saveEmulatorConfig];
    }

    return mmcId;
}

- (BOOL)persistMountBookmarkForURL:(NSURL *)url {
    NSError *error = nil;
    NSData *bookmark = [url bookmarkDataWithOptions:0
                     includingResourceValuesForKeys:nil
                                      relativeToURL:nil
                                              error:&error];
    if (!bookmark) {
        NSLog(@"Unable to persist mounted E: folder bookmark: %@", error);
        return NO;
    }

    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:bookmark forKey:EKA2L1PrefMountedSdCardBookmark];
    [defaults setObject:url.path forKey:EKA2L1PrefMountedSdCardPath];
    return YES;
}

- (void)mountSdCardURL:(NSURL *)url persist:(BOOL)persist showResult:(BOOL)showResult {
    if (!url || !g_state || !g_state->launcher) {
        return;
    }

    const BOOL scoped = [url startAccessingSecurityScopedResource];
    if (scoped && ![self.securityScopedURLs containsObject:url]) {
        [self.securityScopedURLs addObject:url];
    }

    self.mountedSdCardURL = url;
    std::string mutable_path = [url.path UTF8String];
    g_state->launcher->mount_sd_card(mutable_path);
    NSString *mmcId = [self applyMmcIdFromPath:url.path persist:persist];
    if (persist) {
        [self persistMountBookmarkForURL:url];
    }

    if (showResult) {
        NSString *message = mmcId
            ? [NSString stringWithFormat:@"Folder mounted as E:. Using MMC ID %@.", mmcId]
            : @"Folder mounted as E:.";
        [self presentAlertWithTitle:@"E: Drive" message:message];
    }
}

- (void)restorePersistentMounts {
    if (!g_state || !g_state->launcher) {
        return;
    }

    NSData *bookmark = [[NSUserDefaults standardUserDefaults] objectForKey:EKA2L1PrefMountedSdCardBookmark];
    if (![bookmark isKindOfClass:NSData.class]) {
        return;
    }

    BOOL stale = NO;
    NSError *error = nil;
    NSURL *url = [NSURL URLByResolvingBookmarkData:bookmark
                                           options:0
                                     relativeToURL:nil
                               bookmarkDataIsStale:&stale
                                             error:&error];
    if (!url) {
        NSLog(@"Unable to restore mounted E: folder bookmark: %@", error);
        return;
    }

    [self mountSdCardURL:url persist:NO showResult:NO];
    if (stale) {
        [self persistMountBookmarkForURL:url];
    }
}

- (NSString *)copyBackgroundImageIntoDocuments:(NSURL *)url {
    NSFileManager *fm = [NSFileManager defaultManager];
    NSString *directory = [documents_path() stringByAppendingPathComponent:@"Background"];
    [fm createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];

    NSArray<NSString *> *existing = [fm contentsOfDirectoryAtPath:directory error:nil];
    for (NSString *name in existing) {
        [fm removeItemAtPath:[directory stringByAppendingPathComponent:name] error:nil];
    }

    NSString *extension = url.pathExtension.length > 0 ? url.pathExtension : @"img";
    NSString *destination = [directory stringByAppendingPathComponent:
            [@"background" stringByAppendingPathExtension:extension]];

    if ([url.path isEqualToString:destination]) {
        return destination;
    }

    NSError *error = nil;
    if (![fm copyItemAtURL:url toURL:[NSURL fileURLWithPath:destination] error:&error]) {
        NSLog(@"Unable to persist iOS background image: %@", error);
        return nil;
    }

    return destination;
}

- (BOOL)keepSecurityScopeForURL:(NSURL *)url {
    const BOOL scoped = [url startAccessingSecurityScopedResource];
    if (scoped && ![self.securityScopedURLs containsObject:url]) {
        [self.securityScopedURLs addObject:url];
    }
    return scoped;
}

- (void)reloadAfterDataImportWithMessage:(NSString *)message {
    if (g_state && g_state->launcher) {
        g_state->launcher->load_config();
        g_state->launcher->rescan_devices();
        g_state->launcher->retrieve_servers();
        [self restorePersistentMounts];
        [self applyScreenParams];
    }

    [self presentAlertWithTitle:@"Import" message:message];
}

- (NSString *)internalSdCardPath {
    NSString *storage = g_state
        ? [NSString stringWithUTF8String:g_state->conf.storage.c_str()]
        : @"data";
    return [[documents_path() stringByAppendingPathComponent:storage] stringByAppendingPathComponent:@"drives/e"];
}

- (void)showAppsFromToolbar {
    [self showAppList:nil];
}

- (void)openDocumentPickerForAction:(EKA2L1ImportAction)action types:(NSArray<NSString *> *)types mode:(UIDocumentPickerMode)mode {
    self.pendingImportAction = action;
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initWithDocumentTypes:types inMode:mode];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)showImportMenu {
    if (![self hasLauncher]) {
        return;
    }

    UIAlertController *sheet = [UIAlertController alertControllerWithTitle:@"Import"
                                                                   message:nil
                                                            preferredStyle:UIAlertControllerStyleActionSheet];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Import EKA2L1 Data Folder"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self openDocumentPickerForAction:EKA2L1ImportActionDataFolder
                                                                            types:@[ @"public.folder" ]
                                                                             mode:UIDocumentPickerModeOpen];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Install App or Package"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self openDocumentPickerForAction:EKA2L1ImportActionAppPackage
                                                                            types:@[ @"public.item" ]
                                                                             mode:UIDocumentPickerModeImport];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Install Device from VPL"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self openDocumentPickerForAction:EKA2L1ImportActionDeviceVpl
                                                                            types:@[ @"public.item" ]
                                                                             mode:UIDocumentPickerModeImport];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Install Device from ROM/RPKG"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self openDocumentPickerForAction:EKA2L1ImportActionDeviceRom
                                                                            types:@[ @"public.item" ]
                                                                             mode:UIDocumentPickerModeImport];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Install N-Gage Folder"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self openDocumentPickerForAction:EKA2L1ImportActionNGageFolder
                                                                            types:@[ @"public.folder" ]
                                                                             mode:UIDocumentPickerModeOpen];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Import N-Gage 2 Licenses"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self openDocumentPickerForAction:EKA2L1ImportActionNGageLicense
                                                                            types:@[ @"public.text", @"public.item" ]
                                                                             mode:UIDocumentPickerModeImport];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Mount E: Folder"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self openDocumentPickerForAction:EKA2L1ImportActionMountSdCard
                                                                            types:@[ @"public.folder" ]
                                                                             mode:UIDocumentPickerModeOpen];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Import E: Folder Copy"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self openDocumentPickerForAction:EKA2L1ImportActionImportSdCard
                                                                            types:@[ @"public.folder" ]
                                                                             mode:UIDocumentPickerModeOpen];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Set Background Image"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self openDocumentPickerForAction:EKA2L1ImportActionBackgroundImage
                                                                            types:@[ @"public.image" ]
                                                                             mode:UIDocumentPickerModeImport];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentSheet:sheet];
}

- (void)showDeviceMenu {
    if (![self hasLauncher]) {
        return;
    }

    std::vector<std::string> devices = g_state->launcher->get_devices();
    if (devices.empty()) {
        [self presentAlertWithTitle:@"Devices" message:@"No devices are installed yet."];
        return;
    }

    UIAlertController *sheet = [UIAlertController alertControllerWithTitle:@"Devices"
                                                                   message:nil
                                                            preferredStyle:UIAlertControllerStyleActionSheet];
    for (std::size_t i = 0; i < devices.size(); i++) {
        NSString *name = [NSString stringWithUTF8String:devices[i].c_str()];
        if (i == g_state->launcher->get_current_device()) {
            name = [name stringByAppendingString:@" ✓"];
        }

        const std::uint32_t index = static_cast<std::uint32_t>(i);
        [sheet addAction:[UIAlertAction actionWithTitle:name
                                                  style:UIAlertActionStyleDefault
                                                handler:^(__unused UIAlertAction *action) {
                                                    g_state->launcher->set_current_device(index, false);
                                                    g_state->launcher->retrieve_servers();
                                                    g_state->launcher->load_config();
                                                    self.statusLabel.text = @"Device switched.";
                                                }]];
    }
    [sheet addAction:[UIAlertAction actionWithTitle:@"Rename Current Device"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                const std::uint32_t current = g_state->launcher->get_current_device();
                                                NSString *initial = current < devices.size()
                                                    ? [NSString stringWithUTF8String:devices[current].c_str()]
                                                    : @"";
                                                [self presentTextPromptWithTitle:@"Device Name"
                                                                     initialText:initial
                                                                      completion:^(NSString *text) {
                                                                          if (text.length == 0) {
                                                                              return;
                                                                          }
                                                                          g_state->launcher->set_device_name(current, text.UTF8String);
                                                                          [self presentAlertWithTitle:@"Devices" message:@"Device renamed."];
                                                                      }];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Rescan Devices"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                g_state->launcher->rescan_devices();
                                                [self presentAlertWithTitle:@"Devices" message:@"Device folders rescanned."];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentSheet:sheet];
}

- (void)showPackageMenu {
    if (![self hasLauncher]) {
        return;
    }

    std::vector<std::string> packages = g_state->launcher->get_packages();
    if (packages.empty()) {
        [self presentAlertWithTitle:@"Packages" message:@"No removable packages are installed."];
        return;
    }

    UIAlertController *sheet = [UIAlertController alertControllerWithTitle:@"Packages"
                                                                   message:@"Choose a package to uninstall."
                                                            preferredStyle:UIAlertControllerStyleActionSheet];
    for (std::size_t i = 0; i + 2 < packages.size(); i += 3) {
        const std::uint32_t uid = static_cast<std::uint32_t>(std::strtoul(packages[i].c_str(), nullptr, 10));
        const std::int32_t index = static_cast<std::int32_t>(std::strtol(packages[i + 1].c_str(), nullptr, 10));
        NSString *name = [NSString stringWithUTF8String:packages[i + 2].c_str()];
        [sheet addAction:[UIAlertAction actionWithTitle:name
                                                  style:UIAlertActionStyleDestructive
                                                handler:^(__unused UIAlertAction *action) {
                                                    g_state->launcher->uninstall_package(uid, index);
                                                    [self presentAlertWithTitle:@"Packages" message:@"Package uninstalled."];
                                                }]];
    }
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentSheet:sheet];
}

- (void)applyScreenParams {
    if (!g_state || !g_state->launcher) {
        return;
    }

    std::string background_path = self.backgroundImagePath.length > 0
        ? [self.backgroundImagePath UTF8String]
        : "";
    g_state->launcher->set_screen_params(self.screenBackgroundColor, self.screenScaleRatio,
        self.screenScaleType, self.screenGravity, background_path, self.backgroundImageOpacity, self.keepBackgroundAspect);
    [self saveDisplaySettings];
}

- (void)showScreenGravityMenu {
    if (![self hasLauncher]) {
        return;
    }

    UIAlertController *sheet = [UIAlertController alertControllerWithTitle:@"Screen Alignment"
                                                                   message:nil
                                                            preferredStyle:UIAlertControllerStyleActionSheet];
    NSArray<NSString *> *names = @[ @"Left", @"Top", @"Center", @"Right", @"Bottom" ];
    for (NSUInteger i = 0; i < names.count; i++) {
        NSString *title = (i == self.screenGravity)
            ? [names[i] stringByAppendingString:@" ✓"]
            : names[i];
        const uint32_t gravity = static_cast<uint32_t>(i);
        [sheet addAction:[UIAlertAction actionWithTitle:title
                                                  style:UIAlertActionStyleDefault
                                                handler:^(__unused UIAlertAction *action) {
                                                    self.screenGravity = gravity;
                                                    [self applyScreenParams];
                                                }]];
    }
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentSheet:sheet];
}

- (void)showDisplaySettingsMenu {
    if (![self hasLauncher]) {
        return;
    }

    UIAlertController *sheet = [UIAlertController alertControllerWithTitle:@"Display"
                                                                   message:nil
                                                            preferredStyle:UIAlertControllerStyleActionSheet];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Fit Screen"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.screenScaleType = 1;
                                                self.screenScaleRatio = 100;
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Fill Screen"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.screenScaleType = 2;
                                                self.screenScaleRatio = 100;
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Native Size"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.screenScaleType = 0;
                                                self.screenScaleRatio = 100;
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Scale 150%"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.screenScaleRatio = 150;
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Scale 125%"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.screenScaleRatio = 125;
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Scale 100%"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.screenScaleRatio = 100;
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Scale 75%"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.screenScaleRatio = 75;
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Scale 50%"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.screenScaleRatio = 50;
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Screen Alignment"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self showScreenGravityMenu];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Background Gray"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.screenBackgroundColor = 0xD0D0D0;
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Background Black"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.screenBackgroundColor = 0x000000;
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Background White"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.screenBackgroundColor = 0xFFFFFF;
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"BG Image Opacity 25%"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.backgroundImageOpacity = 0.25f;
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"BG Image Opacity 50%"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.backgroundImageOpacity = 0.50f;
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"BG Image Opacity 75%"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.backgroundImageOpacity = 0.75f;
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"BG Image Opacity 100%"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.backgroundImageOpacity = 1.0f;
                                                [self applyScreenParams];
                                            }]];
    NSString *aspectTitle = self.keepBackgroundAspect ? @"Stretch Background Image" : @"Keep Background Aspect";
    [sheet addAction:[UIAlertAction actionWithTitle:aspectTitle
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.keepBackgroundAspect = !self.keepBackgroundAspect;
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Clear Background Image"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.backgroundImagePath = @"";
                                                [self applyScreenParams];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentSheet:sheet];
}

- (void)showGeneralSettingsMenu {
    if (![self hasLauncher]) {
        return;
    }

    NSString *nearestTitle = g_state->conf.nearest_neighbor_filtering
        ? @"Disable Nearest Filtering"
        : @"Enable Nearest Filtering";
    NSString *hideSystemTitle = g_state->conf.hide_system_apps
        ? @"Show System Apps"
        : @"Hide System Apps";
    NSString *loggingTitle = g_state->conf.extensive_logging
        ? @"Disable Extensive Logging"
        : @"Enable Extensive Logging";

    UIAlertController *sheet = [UIAlertController alertControllerWithTitle:@"General"
                                                                   message:nil
                                                            preferredStyle:UIAlertControllerStyleActionSheet];
    [sheet addAction:[UIAlertAction actionWithTitle:nearestTitle
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                g_state->conf.nearest_neighbor_filtering = !g_state->conf.nearest_neighbor_filtering;
                                                [self saveEmulatorConfig];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:hideSystemTitle
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                g_state->conf.hide_system_apps = !g_state->conf.hide_system_apps;
                                                [self saveEmulatorConfig];
                                                [self presentAlertWithTitle:@"General" message:@"App list filter updated."];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:loggingTitle
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                g_state->conf.extensive_logging = !g_state->conf.extensive_logging;
                                                [self saveEmulatorConfig];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Reload Config"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                g_state->launcher->load_config();
                                                [self presentAlertWithTitle:@"General" message:@"Config reloaded."];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentSheet:sheet];
}

- (void)showInputSettingsMenu {
    if (![self hasLauncher]) {
        return;
    }

    NSString *touchTitle = self.touchInputEnabled ? @"Disable Touch Input" : @"Enable Touch Input";
    NSString *keypadTitle = self.keypadVisible ? @"Hide Virtual Keypad" : @"Show Virtual Keypad";
    NSString *feedbackTitle = self.keypadFeedbackEnabled ? @"Disable Key Haptics" : @"Enable Key Haptics";

    UIAlertController *sheet = [UIAlertController alertControllerWithTitle:@"Input"
                                                                   message:nil
                                                            preferredStyle:UIAlertControllerStyleActionSheet];
    [sheet addAction:[UIAlertAction actionWithTitle:touchTitle
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.touchInputEnabled = !self.touchInputEnabled;
                                                [self saveInputSettings];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:keypadTitle
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self toggleVirtualKeypad];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:feedbackTitle
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.keypadFeedbackEnabled = !self.keypadFeedbackEnabled;
                                                [self saveInputSettings];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Keypad Opacity 25%"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.keypadAlpha = 0.25f;
                                                [self updateKeypadAppearance];
                                                [self saveInputSettings];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Keypad Opacity 50%"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.keypadAlpha = 0.50f;
                                                [self updateKeypadAppearance];
                                                [self saveInputSettings];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Keypad Opacity 75%"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.keypadAlpha = 0.75f;
                                                [self updateKeypadAppearance];
                                                [self saveInputSettings];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Keypad Opacity 100%"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                self.keypadAlpha = 1.0f;
                                                [self updateKeypadAppearance];
                                                [self saveInputSettings];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentSheet:sheet];
}

- (void)showLanguageSettingsMenu {
    if (![self hasLauncher]) {
        return;
    }

    std::vector<std::string> ids = g_state->launcher->get_language_ids();
    std::vector<std::string> names = g_state->launcher->get_language_names();
    if (ids.empty() || names.empty()) {
        [self presentAlertWithTitle:@"Language" message:@"No language list is available for the current device."];
        return;
    }

    UIAlertController *sheet = [UIAlertController alertControllerWithTitle:@"Language"
                                                                   message:nil
                                                            preferredStyle:UIAlertControllerStyleActionSheet];
    for (std::size_t i = 0; i < ids.size() && i < names.size(); i++) {
        const uint32_t languageId = static_cast<uint32_t>(std::strtoul(ids[i].c_str(), nullptr, 10));
        NSString *title = [NSString stringWithUTF8String:names[i].c_str()];
        [sheet addAction:[UIAlertAction actionWithTitle:title
                                                  style:UIAlertActionStyleDefault
                                                handler:^(__unused UIAlertAction *action) {
                                                    g_state->launcher->set_language(languageId);
                                                    [self saveEmulatorConfig];
                                                }]];
    }
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentSheet:sheet];
}

- (void)showRtosSettingsMenu {
    if (![self hasLauncher]) {
        return;
    }

    UIAlertController *sheet = [UIAlertController alertControllerWithTitle:@"Real-time Accuracy"
                                                                   message:nil
                                                            preferredStyle:UIAlertControllerStyleActionSheet];
    NSArray<NSString *> *names = @[ @"Low", @"Mid", @"High" ];
    NSArray<NSString *> *values = @[ @"low", @"mid", @"high" ];
    for (NSUInteger i = 0; i < names.count; i++) {
        const uint32_t level = static_cast<uint32_t>(i);
        NSString *value = values[i];
        [sheet addAction:[UIAlertAction actionWithTitle:names[i]
                                                  style:UIAlertActionStyleDefault
                                                handler:^(__unused UIAlertAction *action) {
                                                    g_state->launcher->set_rtos_level(level);
                                                    g_state->conf.rtos_level = value.UTF8String;
                                                    [self saveEmulatorConfig];
                                                }]];
    }
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentSheet:sheet];
}

- (void)showSystemSettingsMenu {
    if (![self hasLauncher]) {
        return;
    }

    UIAlertController *sheet = [UIAlertController alertControllerWithTitle:@"System"
                                                                   message:nil
                                                            preferredStyle:UIAlertControllerStyleActionSheet];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Language"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self showLanguageSettingsMenu];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Real-time Accuracy"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self showRtosSettingsMenu];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Phone Name"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                NSString *initial = [NSString stringWithUTF8String:g_state->conf.device_display_name.c_str()];
                                                [self presentTextPromptWithTitle:@"Phone Name"
                                                                     initialText:initial
                                                                      completion:^(NSString *text) {
                                                                          if (text.length == 0) {
                                                                              return;
                                                                          }
                                                                          g_state->conf.device_display_name = text.UTF8String;
                                                                          [self saveEmulatorConfig];
                                                                      }];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"IMEI"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                NSString *initial = [NSString stringWithUTF8String:g_state->conf.imei.c_str()];
                                                [self presentTextPromptWithTitle:@"IMEI"
                                                                     initialText:initial
                                                                      completion:^(NSString *text) {
                                                                          if (text.length == 0) {
                                                                              return;
                                                                          }
                                                                          g_state->conf.imei = text.UTF8String;
                                                                          [self saveEmulatorConfig];
                                                                      }];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"MMC ID"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                NSString *initial = [NSString stringWithUTF8String:g_state->conf.mmc_id.c_str()];
                                                [self presentTextPromptWithTitle:@"MMC ID"
                                                                     initialText:initial
                                                                      completion:^(NSString *text) {
                                                                          if (text.length == 0) {
                                                                              return;
                                                                          }
                                                                          std::string mmc_id = text.UTF8String;
                                                                          g_state->conf.mmc_id = mmc_id;
                                                                          g_state->conf.current_mmc_id = mmc_id;
                                                                          g_state->launcher->set_current_mmc_id(mmc_id);
                                                                          [self saveEmulatorConfig];
                                                                      }];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentSheet:sheet];
}

- (void)showSettingsMenu {
    if (![self hasLauncher]) {
        return;
    }

    UIAlertController *sheet = [UIAlertController alertControllerWithTitle:@"Settings"
                                                                   message:nil
                                                            preferredStyle:UIAlertControllerStyleActionSheet];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Display"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self showDisplaySettingsMenu];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Input"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self showInputSettingsMenu];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"System"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self showSystemSettingsMenu];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"General"
                                              style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) {
                                                [self showGeneralSettingsMenu];
                                            }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentSheet:sheet];
}

- (void)takeScreenshot {
    if (![self hasLauncher]) {
        return;
    }

    NSString *screenshots = [documents_path() stringByAppendingPathComponent:@"Screenshots"];
    [[NSFileManager defaultManager] createDirectoryAtPath:screenshots
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
    NSString *file = [screenshots stringByAppendingPathComponent:
            [NSString stringWithFormat:@"EKA2L1-%lld.png", static_cast<long long>(time(nullptr))]];
    std::string path = [file UTF8String];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        const bool ok = g_state->launcher->save_screenshot_to(path);
        dispatch_async(dispatch_get_main_queue(), ^{
            [self presentAlertWithTitle:@"Screenshot"
                                message:ok ? [NSString stringWithFormat:@"Saved to %@", file.lastPathComponent]
                                           : @"Failed to save screenshot."];
        });
    });
}

- (void)showAppList:(UILongPressGestureRecognizer *)recognizer {
    if ((recognizer && recognizer.state != UIGestureRecognizerStateBegan) || !g_state || !g_state->launcher) {
        return;
    }

    std::vector<std::string> apps = g_state->launcher->get_apps();
    if (apps.empty()) {
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"No apps"
                                                                       message:@"Install a device and apps in the shared EKA2L1 data folder, then relaunch."
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:alert animated:YES completion:nil];
        return;
    }

    UIAlertController *sheet = [UIAlertController alertControllerWithTitle:@"Launch App"
                                                                   message:nil
                                                            preferredStyle:UIAlertControllerStyleActionSheet];

    for (std::size_t i = 0; i + 1 < apps.size(); i += 2) {
        const std::uint32_t uid = static_cast<std::uint32_t>(std::strtoul(apps[i].c_str(), nullptr, 10));
        NSString *name = [NSString stringWithUTF8String:apps[i + 1].c_str()];

        [sheet addAction:[UIAlertAction actionWithTitle:name
                                                  style:UIAlertActionStyleDefault
                                                handler:^(__unused UIAlertAction *action) {
                                                    self.statusLabel.hidden = YES;
                                                    g_state->launcher->launch_app(uid);
                                                }]];
    }

    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];

    UIPopoverPresentationController *popover = sheet.popoverPresentationController;
    popover.sourceView = self.view;
    popover.sourceRect = CGRectMake(CGRectGetMidX(self.view.bounds), CGRectGetMidY(self.view.bounds), 1, 1);

    [self presentViewController:sheet animated:YES completion:nil];
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    (void)controller;
    self.pendingImportAction = EKA2L1ImportActionNone;
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    (void)controller;
    NSURL *url = urls.firstObject;
    if (!url || ![self hasLauncher]) {
        self.pendingImportAction = EKA2L1ImportActionNone;
        return;
    }

    const EKA2L1ImportAction action = self.pendingImportAction;
    self.pendingImportAction = EKA2L1ImportActionNone;

    const BOOL needsScopedAccess = (action == EKA2L1ImportActionDataFolder) || (action == EKA2L1ImportActionImportSdCard) || (action == EKA2L1ImportActionNGageFolder);
    if (needsScopedAccess) {
        [self keepSecurityScopeForURL:url];
    }

    NSString *pathString = url.path;
    std::string path = [pathString UTF8String];

    switch (action) {
    case EKA2L1ImportActionDataFolder: {
        NSString *source = pathString;
        NSString *destination = documents_path();
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            const bool ok = eka2l1::common::copy_folder([source UTF8String], [destination UTF8String], 0);
            dispatch_async(dispatch_get_main_queue(), ^{
                [self reloadAfterDataImportWithMessage:
                        ok ? @"Data folder imported. Relaunch if a running app does not pick up the new files."
                           : @"Data folder import failed."];
            });
        });
        break;
    }

    case EKA2L1ImportActionAppPackage: {
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            std::string mutable_path = path;
            auto result = g_state->launcher->install_app(mutable_path);
            dispatch_async(dispatch_get_main_queue(), ^{
                NSString *message = (result == eka2l1::package::installation_result_success)
                    ? @"App/package installed."
                    : [NSString stringWithFormat:@"Install failed with code %d.", static_cast<int>(result)];
                [self presentAlertWithTitle:@"Install" message:message];
            });
        });
        break;
    }

    case EKA2L1ImportActionDeviceVpl: {
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            std::string empty;
            std::string mutable_path = path;
            auto result = g_state->launcher->install_device(empty, mutable_path, false);
            dispatch_async(dispatch_get_main_queue(), ^{
                [self presentAlertWithTitle:@"Device"
                                    message:(result == eka2l1::device_installation_none)
                        ? @"Device installed."
                        : [NSString stringWithFormat:@"Device install failed with code %d.", static_cast<int>(result)]];
            });
        });
        break;
    }

    case EKA2L1ImportActionDeviceRom: {
        if (g_state->launcher->does_rom_need_rpkg(path)) {
            self.pendingRomPath = pathString;
            [self presentAlertWithTitle:@"Device" message:@"This ROM needs an RPKG. Choose the matching RPKG next."];
            dispatch_async(dispatch_get_main_queue(), ^{
                [self openDocumentPickerForAction:EKA2L1ImportActionDeviceRpkg
                                            types:@[ @"public.item" ]
                                             mode:UIDocumentPickerModeImport];
            });
        } else {
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
                std::string empty;
                std::string mutable_path = path;
                auto result = g_state->launcher->install_device(empty, mutable_path, true);
                dispatch_async(dispatch_get_main_queue(), ^{
                    [self presentAlertWithTitle:@"Device"
                                        message:(result == eka2l1::device_installation_none)
                            ? @"Device installed."
                            : [NSString stringWithFormat:@"Device install failed with code %d.", static_cast<int>(result)]];
                });
            });
        }
        break;
    }

    case EKA2L1ImportActionDeviceRpkg: {
        if (!self.pendingRomPath) {
            [self presentAlertWithTitle:@"Device" message:@"No ROM is waiting for this RPKG."];
            break;
        }

        std::string rom = [self.pendingRomPath UTF8String];
        self.pendingRomPath = nil;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            std::string rpkg = path;
            std::string rom_path = rom;
            auto result = g_state->launcher->install_device(rpkg, rom_path, true);
            dispatch_async(dispatch_get_main_queue(), ^{
                [self presentAlertWithTitle:@"Device"
                                    message:(result == eka2l1::device_installation_none)
                        ? @"Device installed."
                        : [NSString stringWithFormat:@"Device install failed with code %d.", static_cast<int>(result)]];
            });
        });
        break;
    }

    case EKA2L1ImportActionNGageFolder: {
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            const int result = g_state->launcher->install_ngage_game(path);
            dispatch_async(dispatch_get_main_queue(), ^{
                [self presentAlertWithTitle:@"N-Gage"
                                    message:(result == 0) ? @"N-Gage game installed."
                                                          : [NSString stringWithFormat:@"N-Gage install finished with code %d.", result]];
            });
        });
        break;
    }

    case EKA2L1ImportActionNGageLicense: {
        NSError *error = nil;
        NSString *content = [NSString stringWithContentsOfFile:pathString encoding:NSUTF8StringEncoding error:&error];
        if (!content) {
            [self presentAlertWithTitle:@"N-Gage Licenses" message:@"Unable to read the selected license file."];
            break;
        }

        std::string license_content = [content UTF8String];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            const bool ok = g_state->launcher->install_ng2_game_licenses(license_content);
            dispatch_async(dispatch_get_main_queue(), ^{
                [self presentAlertWithTitle:@"N-Gage Licenses"
                                    message:ok ? @"Licenses imported." : @"License import failed."];
            });
        });
        break;
    }

    case EKA2L1ImportActionMountSdCard: {
        [self mountSdCardURL:url persist:YES showResult:YES];
        break;
    }

    case EKA2L1ImportActionImportSdCard: {
        NSString *source = pathString;
        NSString *destination = [self internalSdCardPath];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            const std::string dest = [destination UTF8String];
            const std::string src = [source UTF8String];
            bool ok = true;
            if (src != dest) {
                eka2l1::common::delete_folder(dest);
                eka2l1::common::create_directories(dest);
                ok = eka2l1::common::copy_folder(src, dest, 0);
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                if (ok) {
                    std::string mount_path = dest;
                    g_state->launcher->mount_sd_card(mount_path);
                    [self applyMmcIdFromPath:destination persist:YES];
                    [[NSUserDefaults standardUserDefaults] removeObjectForKey:EKA2L1PrefMountedSdCardBookmark];
                    [[NSUserDefaults standardUserDefaults] removeObjectForKey:EKA2L1PrefMountedSdCardPath];
                }
                [self presentAlertWithTitle:@"E: Drive"
                                    message:ok ? @"Folder copied into the internal E: drive."
                                               : @"Unable to copy the selected E: folder."];
            });
        });
        break;
    }

    case EKA2L1ImportActionBackgroundImage: {
        NSString *persistentPath = [self copyBackgroundImageIntoDocuments:url];
        if (!persistentPath) {
            [self presentAlertWithTitle:@"Display" message:@"Unable to copy background image."];
            break;
        }

        self.backgroundImagePath = persistentPath;
        self.backgroundImageOpacity = 0.5f;
        [self applyScreenParams];
        [self presentAlertWithTitle:@"Display" message:@"Background image set."];
        break;
    }

    case EKA2L1ImportActionNone:
    default:
        break;
    }
}

- (void)sendTouch:(NSSet<UITouch *> *)touches action:(int)action {
    if (!g_state || !self.touchInputEnabled) {
        return;
    }

    int pointerId = 0;
    CGFloat scale = self.renderView.contentScaleFactor;

    for (UITouch *touch in touches) {
        CGPoint point = [touch locationInView:self.renderView];
        if (g_state->window) {
            g_state->window->update_mouse_state(static_cast<int>(point.x * scale),
                static_cast<int>(point.y * scale),
                action != eka2l1::drivers::mouse_action_release);
        }
        eka2l1::ios::touch_screen(*g_state,
            static_cast<int>(point.x * scale),
            static_cast<int>(point.y * scale),
            eka2l1::PRESSURE_MAX_NUM,
            action,
            pointerId++);
    }
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self sendTouch:touches action:eka2l1::drivers::mouse_action_press];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self sendTouch:touches action:eka2l1::drivers::mouse_action_repeat];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self sendTouch:touches action:eka2l1::drivers::mouse_action_release];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self sendTouch:touches action:eka2l1::drivers::mouse_action_release];
}

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    BOOL handled = NO;
    for (UIPress *press in presses) {
        const int key = key_for_press(press);
        if (key >= 0) {
            [self sendKey:key state:eka2l1::drivers::key_state::pressed];
            handled = YES;
        }
    }

    if (!handled) {
        [super pressesBegan:presses withEvent:event];
    }
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    BOOL handled = NO;
    for (UIPress *press in presses) {
        const int key = key_for_press(press);
        if (key >= 0) {
            [self sendKey:key state:eka2l1::drivers::key_state::released];
            handled = YES;
        }
    }

    if (!handled) {
        [super pressesEnded:presses withEvent:event];
    }
}

- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    [self pressesEnded:presses withEvent:event];
}

@end

@interface EKA2L1AppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end

@implementation EKA2L1AppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[EKA2L1ViewController alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}

- (void)applicationWillResignActive:(UIApplication *)application {
    if (g_state) {
        eka2l1::ios::pause_threads(*g_state);
    }
}

- (void)applicationDidBecomeActive:(UIApplication *)application {
    if (g_state) {
        eka2l1::ios::start_threads(*g_state);
    }
}

- (void)applicationWillTerminate:(UIApplication *)application {
    if (g_state) {
        eka2l1::ios::stop_threads(*g_state);
    }
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([EKA2L1AppDelegate class]));
    }
}
