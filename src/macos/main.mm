#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ApplicationServices/ApplicationServices.h>
#import <UserNotifications/UserNotifications.h>

#include "../core/db.hpp"
#include "../core/engine.hpp"
#include "../core/json.hpp"
#include "../core/key_mapper.hpp"
#include "../core/posix_host_util.hpp"
#include "../core/preferences.hpp"
#include "../core/sound_synth.hpp"
#include "../core/version.hpp"
#include "../win/ui_bundle.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <sys/file.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

inline std::string to_std(NSString* s) {
    return s ? std::string([s UTF8String]) : std::string();
}

inline NSString* to_ns(const std::string& s) {
    return [NSString stringWithUTF8String:s.c_str()];
}

inline std::string exe_path_mac() {
    NSString* p = [[NSBundle mainBundle] executablePath];
    return p ? to_std(p) : std::string("TypeCount");
}

inline std::string default_data_dir_mac() {
    NSArray<NSString*>* dirs = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    NSString* base = dirs.count > 0 ? dirs[0] : [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support"];
    NSString* full = [base stringByAppendingPathComponent:@"TypeCount"];
    std::error_code ec;
    std::filesystem::create_directories(to_std(full), ec);
    return to_std(full);
}

inline std::string detect_system_locale_mac() {
    NSArray<NSString*>* langs = [NSLocale preferredLanguages];
    for (NSString* item in langs) {
        std::string s = to_std(item);
        for (char& c : s) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (c == '_') c = '-';
        }
        if (s.rfind("fa", 0) == 0 || s.rfind("pes", 0) == 0) return "fa";
        if (s.rfind("zh", 0) == 0) {
            if (s.find("hant") != std::string::npos || s.find("-tw") != std::string::npos ||
                s.find("-hk") != std::string::npos || s.find("-mo") != std::string::npos) {
                return "zh-TW";
            }
            return "zh-CN";
        }
        if (s.rfind("ja", 0) == 0) return "ja";
        if (s.rfind("ko", 0) == 0) return "ko";
        if (s.rfind("fr", 0) == 0) return "fr";
        if (s.rfind("de", 0) == 0) return "de";
        if (s.rfind("es", 0) == 0) return "es";
        if (s.rfind("en", 0) == 0) return "en";
    }
    return "en";
}

inline bool is_macos_dark() {
    NSString* style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
    return style && [style caseInsensitiveCompare:@"Dark"] == NSOrderedSame;
}

inline int detect_monitor_ppi_mac(NSWindow* win) {
    NSScreen* scr = win ? win.screen : [NSScreen mainScreen];
    if (!scr) scr = [[NSScreen screens] firstObject];
    if (!scr) return 110;
    NSNumber* num = scr.deviceDescription[@"NSScreenNumber"];
    if (!num) return 110;
    CGDirectDisplayID disp = [num unsignedIntValue];
    CGSize mm = CGDisplayScreenSize(disp);
    size_t pt_w = CGDisplayPixelsWide(disp);
    size_t pt_h = CGDisplayPixelsHigh(disp);
    if (mm.width > 1.0 && mm.height > 1.0 && pt_w > 0 && pt_h > 0) {
        double diag_pt = std::hypot(static_cast<double>(pt_w), static_cast<double>(pt_h));
        double diag_mm = std::hypot(mm.width, mm.height);
        int ppi = static_cast<int>(std::lround(diag_pt * 25.4 / diag_mm));
        return std::clamp(ppi, 72, 360);
    }
    return 110;
}

inline std::string launch_agent_path_mac() {
    NSString* p = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/LaunchAgents/io.typecount.app.plist"];
    return to_std(p);
}

inline bool is_autostart_enabled_mac() {
    std::error_code ec;
    return std::filesystem::exists(launch_agent_path_mac(), ec);
}

inline bool set_autostart_enabled_mac(bool enabled) {
    std::filesystem::path p(launch_agent_path_mac());
    std::error_code ec;
    if (!enabled) {
        std::filesystem::remove(p, ec);
        return true;
    }
    std::filesystem::create_directories(p.parent_path(), ec);
    NSDictionary* plist = @{
        @"Label": @"io.typecount.app",
        @"ProgramArguments": @[to_ns(exe_path_mac()), @"--background"],
        @"RunAtLoad": @YES,
        @"KeepAlive": @NO
    };
    return [plist writeToFile:to_ns(p.string()) atomically:YES] == YES;
}

inline std::string frontmost_app_mac() {
    NSRunningApplication* app = [[NSWorkspace sharedWorkspace] frontmostApplication];
    if (!app) return "Finder";
    NSString* name = app.localizedName;
    if (!name || name.length == 0) name = app.bundleIdentifier;
    return name ? to_std(name) : "Finder";
}

struct MacHotkey {
    uint8_t mods = 0; // bit 1=Ctrl, 2=Alt/Opt, 4=Shift, 8=Cmd
    uint16_t vk = 0;
    bool valid = false;
};

inline MacHotkey parse_mac_hotkey(const std::string& spec) {
    MacHotkey hk;
    std::string cur;
    std::vector<std::string> parts;
    for (char ch : spec) {
        if (ch == '+') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else if (!std::isspace(static_cast<unsigned char>(ch))) {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    if (parts.empty()) return hk;

    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        std::string p = parts[i];
        for (char& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (p == "ctrl" || p == "control") hk.mods |= tc::MOD_CTRL;
        else if (p == "alt" || p == "option" || p == "opt") hk.mods |= tc::MOD_ALT;
        else if (p == "shift") hk.mods |= tc::MOD_SHIFT;
        else if (p == "win" || p == "cmd" || p == "command" || p == "super" || p == "meta") hk.mods |= tc::MOD_WIN;
    }

    std::string kl = parts.back();
    for (char& c : kl) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (kl.size() == 1) {
        hk.vk = static_cast<uint16_t>(tc::code_for_us_char(kl[0]));
        hk.valid = true;
    } else if (kl == "space") {
        hk.vk = 49;
        hk.valid = true;
    } else if (kl.size() >= 2 && kl[0] == 'f') {
        static const uint16_t f_codes[12] = {122, 120, 99, 118, 96, 97, 98, 100, 101, 109, 103, 111};
        int fn = std::atoi(kl.c_str() + 1);
        if (fn >= 1 && fn <= 12) {
            hk.vk = f_codes[fn - 1];
            hk.valid = true;
        }
    }
    return hk;
}

inline int parse_version_num(const std::string& v) {
    std::string s = v;
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
    int a = 0, b = 0, c = 0;
    if (std::sscanf(s.c_str(), "%d.%d.%d", &a, &b, &c) >= 2) return a * 10000 + b * 100 + c;
    return 0;
}

} // namespace

@interface TypeCountSchemeHandler : NSObject <WKURLSchemeHandler>
@end

@implementation TypeCountSchemeHandler
- (void)webView:(WKWebView*)webView startURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    (void)webView;
    NSURL* url = urlSchemeTask.request.URL;
    std::string rel = "index.html";
    if (url && url.path) {
        std::string p = [url.path UTF8String];
        while (!p.empty() && p.front() == '/') p.erase(0, 1);
        if (!p.empty()) rel = p;
    }
    const tc::UiAsset* asset = tc::find_ui_asset(rel);
    if (!asset) asset = tc::find_ui_asset("index.html");
    if (!asset) {
        [urlSchemeTask didFailWithError:[NSError errorWithDomain:NSURLErrorDomain code:404 userInfo:nil]];
        return;
    }
    NSData* data = [NSData dataWithBytes:asset->data length:asset->size];
    NSString* mime = to_ns(asset->mime);
    NSString* pureMime = mime;
    NSRange semi = [mime rangeOfString:@";"];
    if (semi.location != NSNotFound) pureMime = [mime substringToIndex:semi.location];
    NSURLResponse* resp = [[NSURLResponse alloc] initWithURL:url
                                                    MIMEType:pureMime
                                       expectedContentLength:static_cast<NSInteger>(asset->size)
                                            textEncodingName:@"utf-8"];
    [urlSchemeTask didReceiveResponse:resp];
    [urlSchemeTask didReceiveData:data];
    [urlSchemeTask didFinish];
}
- (void)webView:(WKWebView*)webView stopURLSchemeTask:(id<WKURLSchemeTask>)urlSchemeTask {
    (void)webView;
    (void)urlSchemeTask;
}
@end

@interface TypeCountMacController : NSObject <NSApplicationDelegate, NSWindowDelegate, WKNavigationDelegate, WKScriptMessageHandler, NSSoundDelegate>
- (void)handleCGTapEvent:(CGEventType)type event:(CGEventRef)event;
- (std::string)effectiveLocale;
- (bool)effectiveDark;
- (void)savePrefs;
- (void)applyAppearance;
- (void)createMainWindow;
- (void)setupWebView;
- (void)setupStatusItem;
- (void)rebuildStatusMenu;
- (void)setTrayVisible:(BOOL)vis;
- (void)setupInputTap;
- (void)applyWindowGeometryForMode;
- (void)showMainWindow;
- (void)hideMainWindow:(bool)stealth;
- (void)togglePause;
- (void)evalJS:(const std::string&)js;
- (void)toastKey:(const std::string&)key path:(const std::string&)pathVal;
- (void)sendViewState;
- (void)sendUpdateState;
- (tc::SnapshotQuery)makeQuery;
- (void)sendSnapshot;
- (void)updateSportTimer;
- (void)startUpdateCheck:(BOOL)manual;
- (void)playPastelWav:(const std::string&)preset;
- (void)sendNotificationTitle:(const std::string&)title body:(const std::string&)body;
- (void)handleIpcMessage:(const std::string&)raw;
@end

@implementation TypeCountMacController {
    std::string default_dir_;
    std::string data_dir_;
    std::string prefs_path_;
    std::string db_path_;
    std::string db_mode_;
    std::string last_backup_day_;

    tc::Preferences prefs_;
    tc::Db db_;
    std::unique_ptr<tc::StatsEngine> engine_;
    tc::CaptureStatusInfo capture_status_;
    tc::JsonValue session_state_;

    NSWindow* window_;
    WKWebView* webview_;
    NSStatusItem* status_item_;
    NSMutableSet<NSSound*>* active_sounds_;

    CFMachPortRef event_tap_;
    CFRunLoopSourceRef tap_source_;
    CGPoint last_cursor_;
    bool has_last_cursor_;
    CGEventFlags prev_flags_;
    MacHotkey wake_hotkey_;

    int lock_fd_;
    bool demo_mode_;
    bool paused_;
    bool typing_test_running_;
    bool system_dark_;
    int detected_dpi_;
    bool ui_ready_;
    bool ui_visible_;
    bool quitting_;
    bool break_notified_;
    bool ergo_notified_;

    NSTimer* flush_timer_;
    NSTimer* sport_timer_;
    NSTimer* perm_timer_;

    std::atomic<bool> update_checking_;
    std::string update_status_;
    std::string available_version_;
    std::string available_release_url_;
    std::string available_download_url_;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        lock_fd_ = -1;
        db_mode_ = "default";
        update_status_ = "idle";
        active_sounds_ = [NSMutableSet set];
    }
    return self;
}

- (void)applicationDidFinishLaunching:(NSNotification*)note {
    (void)note;

    bool start_hidden = false;
    for (NSString* arg in [[NSProcessInfo processInfo] arguments]) {
        if ([arg isEqualToString:@"--background"] || [arg isEqualToString:@"--stealth"]) start_hidden = true;
        else if ([arg isEqualToString:@"--demo"]) demo_mode_ = true;
    }

    default_dir_ = default_data_dir_mac();
    data_dir_ = default_dir_;

    // Single-instance check
    std::string lock_p = (std::filesystem::path(default_dir_) / ".lock").string();
    lock_fd_ = open(lock_p.c_str(), O_CREAT | O_RDWR, 0644);
    if (lock_fd_ >= 0 && flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
        if (!start_hidden) {
            [[NSDistributedNotificationCenter defaultCenter] postNotificationName:@"io.typecount.app.show" object:nil userInfo:nil deliverImmediately:YES];
        }
        close(lock_fd_);
        [NSApp terminate:nil];
        return;
    }

    prefs_path_ = (std::filesystem::path(default_dir_) / "preferences.json").string();
    bool loaded = false;
    prefs_ = tc::Preferences::load_file(prefs_path_, &loaded);
    if (!loaded) prefs_.save_file(prefs_path_);

    std::string db_dir = prefs_.custom_db_dir.empty() ? data_dir_ : prefs_.custom_db_dir;
    std::error_code ec;
    std::filesystem::create_directories(db_dir, ec);
    db_path_ = (std::filesystem::path(db_dir) / "typecount.sqlite3").string();
    db_mode_ = prefs_.custom_db_dir.empty() ? "default" : "custom";

    if (!db_.open(db_path_)) {
        [NSApp terminate:nil];
        return;
    }

    engine_ = std::make_unique<tc::StatsEngine>(db_);
    if (!demo_mode_ && db_.get_meta("jalali_user_set").empty()) {
        prefs_.jalali_calendar = ([self effectiveLocale] == "fa");
    }
    engine_->set_idle_timeout_sec(prefs_.idle_detection_enabled ? prefs_.idle_timeout_sec : 180);
    if (prefs_.autostart) set_autostart_enabled_mac(true);
    wake_hotkey_ = parse_mac_hotkey(prefs_.wake_hotkey);
    system_dark_ = is_macos_dark();

    [self setupStatusItem];
    [self createMainWindow];
    detected_dpi_ = detect_monitor_ppi_mac(window_);
    if (prefs_.dpi_auto && detected_dpi_ != prefs_.dpi_ppi) {
        prefs_.dpi_ppi = detected_dpi_;
        prefs_.save_file(prefs_path_);
    }
    [self setupWebView];
    [self applyAppearance];

    // Listen to OS dark mode changes & single-instance activate requests
    [[NSDistributedNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(onSystemThemeChanged:)
               name:@"AppleInterfaceThemeChangedNotification"
             object:nil];
    [[NSDistributedNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(onExternalShow:)
               name:@"io.typecount.app.show"
             object:nil];

    if (NSBundle.mainBundle.bundleIdentifier != nil) {
        [[UNUserNotificationCenter currentNotificationCenter]
            requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                          completionHandler:^(BOOL, NSError*) {}];
    }

    if (!start_hidden && !prefs_.stealth_mode && prefs_.startup_mode != "stealth") {
        [self showMainWindow];
    } else {
        ui_visible_ = false;
        if (prefs_.startup_mode == "stealth" || prefs_.stealth_mode) {
            prefs_.stealth_mode = true;
            [self setTrayVisible:NO];
        }
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    }

    [self setupInputTap];

    flush_timer_ = [NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(onFlushTimer:) userInfo:nil repeats:YES];
    [self updateSportTimer];

    if (prefs_.auto_check_updates && !demo_mode_) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(6 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            int64_t now_s = tc::posix_util::now_utc_ms() / 1000;
            if (self->prefs_.last_update_check <= 0 || (now_s - self->prefs_.last_update_check) >= 21600) {
                [self startUpdateCheck:NO];
            }
        });
    }
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)flag {
    (void)sender;
    if (!flag) [self showMainWindow];
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)note {
    (void)note;
    quitting_ = true;
    if (flush_timer_) [flush_timer_ invalidate];
    if (sport_timer_) [sport_timer_ invalidate];
    if (perm_timer_) [perm_timer_ invalidate];
    if (event_tap_) {
        CGEventTapEnable(event_tap_, false);
        CFRelease(event_tap_);
        event_tap_ = nullptr;
    }
    if (tap_source_) {
        CFRelease(tap_source_);
        tap_source_ = nullptr;
    }
    if (engine_) engine_->flush();
    db_.close();
    if (lock_fd_ >= 0) {
        flock(lock_fd_, LOCK_UN);
        close(lock_fd_);
    }
}

- (std::string)effectiveLocale {
    if (prefs_.language == "system" || prefs_.language.empty()) return detect_system_locale_mac();
    return prefs_.language;
}

- (bool)effectiveDark {
    if (prefs_.color_mode == "dark") return true;
    if (prefs_.color_mode == "light") return false;
    return system_dark_;
}

- (void)savePrefs {
    prefs_.save_file(prefs_path_);
}

- (void)applyAppearance {
    if (!window_) return;
    if (prefs_.color_mode == "dark") {
        window_.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    } else if (prefs_.color_mode == "light") {
        window_.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    } else {
        window_.appearance = nil;
    }
    bool dark = [self effectiveDark];
    window_.backgroundColor = dark
        ? [NSColor colorWithRed:0.0706 green:0.0824 blue:0.1059 alpha:1.0]
        : [NSColor colorWithRed:0.9608 green:0.9686 blue:0.9804 alpha:1.0];
}

- (void)onSystemThemeChanged:(NSNotification*)note {
    (void)note;
    dispatch_async(dispatch_get_main_queue(), ^{
        bool dark = is_macos_dark();
        if (dark != self->system_dark_) {
            self->system_dark_ = dark;
            [self applyAppearance];
            [self sendViewState];
        }
    });
}

- (void)onExternalShow:(NSNotification*)note {
    (void)note;
    dispatch_async(dispatch_get_main_queue(), ^{
        [self showMainWindow];
    });
}

- (void)createMainWindow {
    NSRect frame = NSMakeRect(0, 0, 1040, 720);
    NSWindowStyleMask mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
                             NSWindowStyleMaskFullSizeContentView;
    window_ = [[NSWindow alloc] initWithContentRect:frame styleMask:mask backing:NSBackingStoreBuffered defer:NO];
    window_.title = @"TypeCount";
    window_.titleVisibility = NSWindowTitleHidden;
    window_.titlebarAppearsTransparent = YES;
    window_.delegate = self;
    window_.minSize = NSMakeSize(300, 68);
    window_.releasedWhenClosed = NO;
    [window_ center];
}

- (void)setupWebView {
    WKWebViewConfiguration* cfg = [[WKWebViewConfiguration alloc] init];
    [cfg setURLSchemeHandler:[[TypeCountSchemeHandler alloc] init] forURLScheme:@"typecount"];

    WKUserContentController* ucc = cfg.userContentController;
    [ucc addScriptMessageHandler:self name:@"ipc"];

    std::string loc = [self effectiveLocale];
    std::string boot =
        "window.clickpulsePlatform='macos';"
        "window.typeCountVersion=" + tc::JsonValue::escape_string(TC_VERSION_STR) + ";"
        "window.clickpulseLocale=" + tc::JsonValue::escape_string(loc) + ";"
        "window.clickpulseTheme=" + tc::JsonValue::escape_string(prefs_.theme) + ";"
        "window.clickpulseColorMode=" + tc::JsonValue::escape_string(prefs_.color_mode) + ";"
        "window.clickpulseSystemDark=" + std::string(system_dark_ ? "true" : "false") + ";";

    WKUserScript* us = [[WKUserScript alloc] initWithSource:to_ns(boot)
                                              injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                           forMainFrameOnly:YES];
    [ucc addUserScript:us];

    webview_ = [[WKWebView alloc] initWithFrame:window_.contentView.bounds configuration:cfg];
    webview_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    webview_.navigationDelegate = self;
    [webview_ setValue:@NO forKey:@"drawsBackground"];
    [window_.contentView addSubview:webview_];
    [webview_ loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"typecount://app/index.html"]]];
}

- (void)setupStatusItem {
    status_item_ = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    const tc::UiAsset* mark = tc::find_ui_asset("typecount-mark.png");
    if (mark) {
        NSData* d = [NSData dataWithBytes:mark->data length:mark->size];
        NSImage* img = [[NSImage alloc] initWithData:d];
        [img setSize:NSMakeSize(18, 18)];
        [img setTemplate:YES];
        status_item_.button.image = img;
    } else {
        status_item_.button.title = @"TC";
    }
    status_item_.button.toolTip = @"TypeCount";

    [self rebuildStatusMenu];
}

- (void)rebuildStatusMenu {
    bool fa = ([self effectiveLocale] == "fa");
    NSMenu* menu = [[NSMenu alloc] init];
    [menu addItemWithTitle:fa ? @"باز کردن TypeCount" : @"Open TypeCount" action:@selector(menuOpen:) keyEquivalent:@""];
    NSMenuItem* mini = [menu addItemWithTitle:fa ? @"حالت کوچک (Mini)" : @"Mini mode" action:@selector(menuMini:) keyEquivalent:@""];
    mini.state = prefs_.compact ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItemWithTitle:fa ? @"حالت مخفی کامل" : @"Stealth mode (hide menu bar icon)" action:@selector(menuStealth:) keyEquivalent:@""];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* pItem = [menu addItemWithTitle:paused_ ? (fa ? @"ادامه ثبت" : @"Resume recording")
                                                       : (fa ? @"توقف موقت ثبت" : @"Pause recording")
                                        action:@selector(menuPause:)
                                 keyEquivalent:@""];
    pItem.state = paused_ ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:fa ? @"ذخیره و خروج" : @"Save & Quit" action:@selector(menuQuit:) keyEquivalent:@"q"];
    for (NSMenuItem* it in menu.itemArray) it.target = self;
    status_item_.menu = menu;
}

- (void)setTrayVisible:(BOOL)vis {
    if (!status_item_) return;
    status_item_.visible = vis;
}

- (void)menuOpen:(id)sender {
    (void)sender;
    prefs_.compact = false;
    [self savePrefs];
    [self applyWindowGeometryForMode];
    [self showMainWindow];
}
- (void)menuMini:(id)sender {
    (void)sender;
    prefs_.compact = !prefs_.compact;
    [self savePrefs];
    [self applyWindowGeometryForMode];
    [self showMainWindow];
}
- (void)menuStealth:(id)sender {
    (void)sender;
    [self hideMainWindow:true];
}
- (void)menuPause:(id)sender {
    (void)sender;
    [self togglePause];
}
- (void)menuQuit:(id)sender {
    (void)sender;
    quitting_ = true;
    [NSApp terminate:nil];
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    if (quitting_) return YES;
    if (prefs_.close_action == "hide") { [self hideMainWindow:false]; return NO; }
    if (prefs_.close_action == "stealth") { [self hideMainWindow:true]; return NO; }
    if (prefs_.close_action == "quit") { quitting_ = true; [NSApp terminate:nil]; return YES; }
    [self evalJS:@"window.requestClose&&window.requestClose();"];
    return NO;
}

- (void)windowDidChangeScreen:(NSNotification*)note {
    (void)note;
    int ppi = detect_monitor_ppi_mac(window_);
    if (ppi != detected_dpi_) {
        detected_dpi_ = ppi;
        if (prefs_.dpi_auto && prefs_.dpi_ppi != ppi) {
            prefs_.dpi_ppi = ppi;
            [self savePrefs];
        }
        if (ui_ready_) [self sendViewState];
    }
}

- (void)windowDidEnterFullScreen:(NSNotification*)note { (void)note; [self sendViewState]; }
- (void)windowDidExitFullScreen:(NSNotification*)note { (void)note; [self sendViewState]; }

static CGEventRef mac_cg_tap_callback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void* refcon) {
    (void)proxy;
    TypeCountMacController* ctrl = (__bridge TypeCountMacController*)refcon;
    if (ctrl) [ctrl handleCGTapEvent:type event:event];
    return event;
}

- (void)setupInputTap {
    if (event_tap_) return;
    bool granted = CGPreflightListenEventAccess() || AXIsProcessTrusted();
    if (!granted) {
        capture_status_.permission_needed = true;
        capture_status_.listening = false;
        if (!perm_timer_) {
            perm_timer_ = [NSTimer scheduledTimerWithTimeInterval:1.5 target:self selector:@selector(pollPermission:) userInfo:nil repeats:YES];
        }
        return;
    }

    CGEventMask mask =
        CGEventMaskBit(kCGEventKeyDown) |
        CGEventMaskBit(kCGEventFlagsChanged) |
        CGEventMaskBit(kCGEventLeftMouseDown) |
        CGEventMaskBit(kCGEventRightMouseDown) |
        CGEventMaskBit(kCGEventOtherMouseDown) |
        CGEventMaskBit(kCGEventMouseMoved) |
        CGEventMaskBit(kCGEventLeftMouseDragged) |
        CGEventMaskBit(kCGEventRightMouseDragged) |
        CGEventMaskBit(kCGEventOtherMouseDragged) |
        CGEventMaskBit(kCGEventScrollWheel);

    event_tap_ = CGEventTapCreate(
        kCGSessionEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly,
        mask,
        mac_cg_tap_callback,
        (__bridge void*)self);

    if (!event_tap_) {
        capture_status_.permission_needed = true;
        capture_status_.listening = false;
        if (!perm_timer_) {
            perm_timer_ = [NSTimer scheduledTimerWithTimeInterval:1.5 target:self selector:@selector(pollPermission:) userInfo:nil repeats:YES];
        }
        return;
    }

    tap_source_ = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, event_tap_, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), tap_source_, kCFRunLoopCommonModes);
    CGEventTapEnable(event_tap_, true);
    capture_status_.permission_needed = false;
    capture_status_.listening = true;
    if (perm_timer_) {
        [perm_timer_ invalidate];
        perm_timer_ = nil;
    }
}

- (void)pollPermission:(NSTimer*)t {
    (void)t;
    if (CGPreflightListenEventAccess() || AXIsProcessTrusted()) {
        [self setupInputTap];
        if (ui_ready_) [self sendSnapshot];
    }
}

- (void)handleCGTapEvent:(CGEventType)type event:(CGEventRef)event {
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (event_tap_) CGEventTapEnable(event_tap_, true);
        return;
    }

    if (type == kCGEventKeyDown) {
        if (CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat) != 0) return;
        uint16_t vk = static_cast<uint16_t>(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
        CGEventFlags fl = CGEventGetFlags(event);

        uint8_t mods = 0;
        if (fl & kCGEventFlagMaskControl) mods |= tc::MOD_CTRL;
        if (fl & kCGEventFlagMaskAlternate) mods |= tc::MOD_ALT;
        if (fl & kCGEventFlagMaskShift) mods |= tc::MOD_SHIFT;
        if (fl & kCGEventFlagMaskCommand) mods |= tc::MOD_WIN;

        if (wake_hotkey_.valid && vk == wake_hotkey_.vk && mods == wake_hotkey_.mods) {
            dispatch_async(dispatch_get_main_queue(), ^{ [self showMainWindow]; });
            return;
        }

        if (paused_ || demo_mode_) return;
        if (typing_test_running_ && !prefs_.typing_test_count_stats) return;

        std::string app = prefs_.track_apps ? frontmost_app_mac() : "";
        if (tc::posix_util::is_excluded_app(prefs_, app)) {
            if (prefs_.pause_in_excluded_apps) return;
            app.clear();
        }

        std::optional<tc::ShortcutChord> chord;
        if (mods != 0 && !(mods == tc::MOD_SHIFT)) {
            chord = tc::ShortcutChord{mods, static_cast<uint32_t>(vk)};
        }
        int64_t ms = tc::posix_util::now_utc_ms();
        int tz = tc::posix_util::timezone_offset_sec_for(prefs_.timezone);
        engine_->on_key_press(ms, tz, tc::mac_keycode_to_canonical(vk), false, chord, app, prefs_.track_apps);
        return;
    }

    if (type == kCGEventFlagsChanged) {
        CGEventFlags fl = CGEventGetFlags(event);
        CGEventFlags rising = fl & ~prev_flags_;
        prev_flags_ = fl;
        if (rising == 0 || paused_ || demo_mode_) return;
        uint16_t vk = static_cast<uint16_t>(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
        std::string app = prefs_.track_apps ? frontmost_app_mac() : "";
        if (tc::posix_util::is_excluded_app(prefs_, app)) {
            if (prefs_.pause_in_excluded_apps) return;
            app.clear();
        }
        int64_t ms = tc::posix_util::now_utc_ms();
        int tz = tc::posix_util::timezone_offset_sec_for(prefs_.timezone);
        engine_->on_key_press(ms, tz, tc::mac_keycode_to_canonical(vk), true, std::nullopt, app, prefs_.track_apps);
        return;
    }

    if (paused_ || demo_mode_) return;

    std::string app = prefs_.track_apps ? frontmost_app_mac() : "";
    if (tc::posix_util::is_excluded_app(prefs_, app)) {
        if (prefs_.pause_in_excluded_apps) return;
        app.clear();
    }

    int64_t ms = tc::posix_util::now_utc_ms();
    int tz = tc::posix_util::timezone_offset_sec_for(prefs_.timezone);

    if (type == kCGEventLeftMouseDown) {
        engine_->on_mouse_click(ms, tz, 1, app, prefs_.track_apps);
    } else if (type == kCGEventRightMouseDown) {
        engine_->on_mouse_click(ms, tz, 2, app, prefs_.track_apps);
    } else if (type == kCGEventOtherMouseDown) {
        int64_t btn = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
        uint32_t mapped = (btn == 2) ? 3u : (btn == 3 ? 4u : (btn == 4 ? 5u : 3u));
        engine_->on_mouse_click(ms, tz, mapped, app, prefs_.track_apps);
    } else if (type == kCGEventScrollWheel) {
        engine_->on_mouse_scroll(ms, tz, 1, app, prefs_.track_apps);
    } else if (type == kCGEventMouseMoved || type == kCGEventLeftMouseDragged ||
               type == kCGEventRightMouseDragged || type == kCGEventOtherMouseDragged) {
        CGPoint cur = CGEventGetLocation(event);
        if (has_last_cursor_) {
            double dist = std::hypot(cur.x - last_cursor_.x, cur.y - last_cursor_.y);
            if (dist > 0.0 && dist <= 320.0) {
                engine_->on_mouse_move(ms, tz, static_cast<int64_t>(std::llround(dist)), app, prefs_.track_apps);
            }
        }
        last_cursor_ = cur;
        has_last_cursor_ = true;
    }
}

- (void)playPastelWav:(const std::string&)preset {
    if (preset == "none") return;
    const auto& wav = tc::sound::get_cached_pastel_wav(preset);
    if (wav.empty()) return;
    NSData* data = [NSData dataWithBytes:wav.data() length:wav.size()];
    NSSound* sound = [[NSSound alloc] initWithData:data];
    if (sound) {
        sound.delegate = self;
        [active_sounds_ addObject:sound];
        [sound play];
    }
}

- (void)sound:(NSSound*)sound didFinishPlaying:(BOOL)finishedPlaying {
    (void)finishedPlaying;
    if (sound) [active_sounds_ removeObject:sound];
}

- (void)sendNotificationTitle:(const std::string&)title body:(const std::string&)body {
    if (NSBundle.mainBundle.bundleIdentifier == nil) return;
    UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
    content.title = to_ns(title);
    content.body = to_ns(body);
    UNNotificationRequest* req = [UNNotificationRequest requestWithIdentifier:[[NSUUID UUID] UUIDString] content:content trigger:nil];
    [[UNUserNotificationCenter currentNotificationCenter] addNotificationRequest:req withCompletionHandler:nil];
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
    (void)webView;
    (void)navigation;
    ui_ready_ = true;
    if (session_state_.is_object()) {
        [self evalJS:"window.restoreSessionState&&window.restoreSessionState(" + session_state_.dump() + ");"];
    }
    [self sendViewState];
    [self sendSnapshot];
    [self sendUpdateState];
}

- (void)userContentController:(WKUserContentController*)ucc didReceiveScriptMessage:(WKScriptMessage*)message {
    (void)ucc;
    if ([message.body isKindOfClass:[NSString class]]) {
        [self handleIpcMessage:to_std(static_cast<NSString*>(message.body))];
    }
}

- (void)evalJS:(const std::string&)js {
    if (!webview_ || !ui_ready_) return;
    [webview_ evaluateJavaScript:to_ns(js) completionHandler:nil];
}

- (void)toastKey:(const std::string&)key path:(const std::string&)pathVal {
    if (pathVal.empty()) {
        [self evalJS:"window.toast&&window.toast(ClickPulseI18n.t(" + tc::JsonValue::escape_string(key) + "));"];
    } else {
        [self evalJS:"window.toast&&window.toast(ClickPulseI18n.t(" + tc::JsonValue::escape_string(key) +
                     ",{path:" + tc::JsonValue::escape_string(pathVal) + "}));"];
    }
}

- (void)sendViewState {
    if (!ui_ready_) return;
    tc::JsonValue v = prefs_.to_json();
    v.set("app_version", std::string(TC_VERSION_STR));
    v.set("platform", std::string("macos"));
    v.set("locale", [self effectiveLocale]);
    v.set("language_preference", prefs_.language);
    v.set("color_mode", prefs_.color_mode);
    v.set("system_dark", system_dark_);
    v.set("effective_dark", [self effectiveDark]);
    v.set("detected_dpi", detected_dpi_);
    v.set("visible", ui_visible_);
    v.set("maximized", window_ ? (window_.isZoomed == YES) : false);
    v.set("fullscreen", window_ ? ((window_.styleMask & NSWindowStyleMaskFullScreen) != 0) : false);
    v.set("auto_check_updates", prefs_.auto_check_updates);
    v.set("last_update_check", prefs_.last_update_check);
    v.set("msi_installed", false);
    v.set("registered_uninstall", false);

    tc::JsonValue tzs = tc::JsonValue::array();
    for (const auto& z : tc::posix_util::system_timezones_list()) tzs.push(z);
    v.set("timezones", std::move(tzs));

    [self evalJS:"window.receiveView&&window.receiveView(" + v.dump() + ");"];
}

- (void)sendUpdateState {
    if (!ui_ready_) return;
    tc::JsonValue u = tc::JsonValue::object();
    u.set("current_version", std::string(TC_VERSION_STR));
    u.set("automatic", prefs_.auto_check_updates);
    u.set("status", update_status_);
    u.set("checking", update_checking_.load());
    u.set("available_version", available_version_);
    u.set("release_url", available_release_url_);
    u.set("download_url", available_download_url_);
    u.set("last_checked_at", prefs_.last_update_check);
    [self evalJS:"window.receiveUpdateState&&window.receiveUpdateState(" + u.dump() + ");"];
}

- (tc::SnapshotQuery)makeQuery {
    tc::SnapshotQuery q;
    q.now_ms = tc::posix_util::now_utc_ms();
    q.tz_offset_sec = tc::posix_util::timezone_offset_sec_for(prefs_.timezone);
    q.include_shortcuts = true;
    q.include_apps = true;
    q.demo = demo_mode_;
    q.prefs = prefs_;
    q.capture = capture_status_;
    q.capture.paused = paused_;
    q.auto_start = is_autostart_enabled_mac();
    q.auto_start_available = (db_mode_ == "default" && !demo_mode_);
    q.database_mode = db_mode_;

    const auto& ins = session_state_.get("insights");
    q.report_period = ins.get("share_period").as_string_or("daily");
    std::string sday = ins.get("share_day").as_string_or("");
    if (sday.empty()) sday = tc::Db::format_day(q.now_ms / 1000, q.tz_offset_sec);
    q.report_day = sday;
    q.report_start = ins.get("share_start").as_string_or("");
    return q;
}

- (void)sendSnapshot {
    if (!ui_ready_ || !ui_visible_) return;
    tc::JsonValue snap = engine_->snapshot([self makeQuery]);
    if (prefs_.compact) {
        [self evalJS:"window.receiveMini&&window.receiveMini(" + snap.dump() + ");"];
    } else {
        [self evalJS:"window.receive&&window.receive(" + snap.dump() + ");"];
    }
}

- (void)onFlushTimer:(NSTimer*)t {
    (void)t;
    engine_->flush();
    int64_t ms = tc::posix_util::now_utc_ms();
    int tz = tc::posix_util::timezone_offset_sec_for(prefs_.timezone);
    std::string today = tc::Db::format_day(ms / 1000, tz);
    bool fa = ([self effectiveLocale] == "fa");

    if (prefs_.auto_backup && !demo_mode_ && last_backup_day_ != today) {
        db_.create_rotating_backup((std::filesystem::path(data_dir_) / "backups").string(), 5);
        last_backup_day_ = today;
    }

    int idle_s = prefs_.idle_detection_enabled ? prefs_.idle_timeout_sec : 180;
    engine_->set_idle_timeout_sec(idle_s);
    auto live = engine_->get_live_metrics(ms, prefs_.break_interval_min, idle_s);

    if (live.break_due && !break_notified_) {
        break_notified_ = true;
        if (prefs_.sound_alerts_enabled) [self playPastelWav:prefs_.notification_sound];
        if (!prefs_.stealth_mode) {
            [self sendNotificationTitle:fa ? "TypeCount · یادآور استراحت" : "TypeCount · Break Reminder"
                                   body:fa ? "۲۰ ثانیه به دوردست نگاه کنید یا مچ دست خود را استراحت دهید."
                                           : "Look 20 ft away for 20 seconds or stretch your wrists."];
        }
    } else if (!live.break_due) {
        break_notified_ = false;
    }

    if (prefs_.ergo_guard_enabled && live.ergo_strain_level >= 2 && !ergo_notified_) {
        ergo_notified_ = true;
        if (prefs_.sound_alerts_enabled) [self playPastelWav:prefs_.notification_sound];
        if (!prefs_.stealth_mode) {
            [self sendNotificationTitle:fa ? "TypeCount · هشدار ارگونومی (RSI)" : "TypeCount · Ergonomic Strain Guard"
                                   body:fa ? "فشار تایپ/کلیک در ۱۰ دقیقه اخیر بالا بوده است؛ چند لحظه دست‌ها را استراحت دهید."
                                           : "High 10-minute typing/click intensity detected. Take a 30-second wrist stretch."];
        }
        if (ui_visible_ && ui_ready_) [self toastKey:"ergo.strain_alert" path:""];
    } else if (live.ergo_strain_level < 2) {
        ergo_notified_ = false;
    }

    if (prefs_.smart_alerts_enabled && !demo_mode_) {
        tc::JsonValue snap = engine_->snapshot([self makeQuery]);
        int64_t today_total = snap.get("today").get("keyboard").as_int(0) + snap.get("today").get("mouse").as_int(0);
        if (prefs_.daily_goal > 0 && today_total >= prefs_.daily_goal && db_.get_meta("goal_notified_day") != today) {
            db_.set_meta("goal_notified_day", today);
            if (prefs_.sound_alerts_enabled) [self playPastelWav:prefs_.notification_sound];
            if (!prefs_.stealth_mode) {
                [self sendNotificationTitle:fa ? "TypeCount · هدف روزانه کامل شد!" : "TypeCount · Daily Goal Reached!"
                                       body:fa ? "آفرین! به هدف روزانه فعالیت خود رسیدید." : "Great work! You reached your daily activity target."];
            }
            if (ui_visible_ && ui_ready_) [self toastKey:"smart.goal_reached" path:""];
        }
        double peak_wpm = snap.get("today_peak_wpm").as_double(0.0);
        double prev_best = 0.0;
        std::string prev_best_s = db_.get_meta("best_wpm_record");
        if (!prev_best_s.empty()) { try { prev_best = std::stod(prev_best_s); } catch (...) {} }
        if (peak_wpm >= 35.0 && (prev_best <= 0.0 || peak_wpm >= prev_best + 2.0)) {
            db_.set_meta("best_wpm_record", std::to_string(static_cast<int>(std::round(peak_wpm))));
            if (prev_best > 0.0) {
                if (prefs_.sound_alerts_enabled) [self playPastelWav:prefs_.notification_sound];
                if (!prefs_.stealth_mode) {
                    [self sendNotificationTitle:fa ? "TypeCount · رکورد جدید سرعت تایپ!" : "TypeCount · New Peak Speed Record!"
                                           body:(fa ? "رکورد جدید سرعت تایپ: " : "New peak typing speed: ") +
                                                std::to_string(static_cast<int>(std::round(peak_wpm))) + " WPM"];
                }
                if (ui_visible_ && ui_ready_) [self toastKey:"smart.new_record" path:""];
            }
        }
        if (ui_visible_ && ui_ready_) {
            [self evalJS:(prefs_.compact ? "window.receiveMini(" : "window.receive(") + snap.dump() + ");"];
            return;
        }
    }

    if (ui_visible_ && ui_ready_) [self sendSnapshot];
}

- (void)updateSportTimer {
    if (sport_timer_) {
        [sport_timer_ invalidate];
        sport_timer_ = nil;
    }
    if (ui_visible_ && prefs_.compact && prefs_.sport_enabled && !paused_) {
        sport_timer_ = [NSTimer scheduledTimerWithTimeInterval:0.15 target:self selector:@selector(onSportTimer:) userInfo:nil repeats:YES];
    }
}

- (void)onSportTimer:(NSTimer*)t {
    (void)t;
    if (!ui_ready_ || !ui_visible_ || !prefs_.compact || !prefs_.sport_enabled) return;
    int idle_s = prefs_.idle_detection_enabled ? prefs_.idle_timeout_sec : 180;
    auto live = engine_->get_live_metrics(tc::posix_util::now_utc_ms(), prefs_.break_interval_min, idle_s);
    tc::JsonValue s = tc::JsonValue::object();
    s.set("available", true);
    s.set("paused", paused_);
    s.set("cps", live.live_cps);
    s.set("rate", live.live_cps);
    s.set("wpm", live.live_wpm);
    [self evalJS:"window.receiveSport&&window.receiveSport(" + s.dump() + ");"];
}

- (void)applyWindowGeometryForMode {
    if (!window_) return;
    if (prefs_.compact) {
        if (window_.isZoomed) [window_ zoom:nil];
        [window_ setFrame:NSMakeRect(window_.frame.origin.x, window_.frame.origin.y, 320, 208) display:YES animate:NO];
        window_.level = prefs_.pinned ? NSFloatingWindowLevel : NSNormalWindowLevel;
    } else {
        window_.level = NSNormalWindowLevel;
        [window_ setFrame:NSMakeRect(window_.frame.origin.x, window_.frame.origin.y, 1040, 720) display:YES animate:NO];
        [window_ center];
    }
}

- (void)showMainWindow {
    if (prefs_.stealth_mode) {
        prefs_.stealth_mode = false;
        [self savePrefs];
    }
    [self setTrayVisible:YES];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    ui_visible_ = true;
    [self applyWindowGeometryForMode];
    [window_ makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [self sendViewState];
    [self sendSnapshot];
    [self updateSportTimer];
}

- (void)hideMainWindow:(bool)stealth {
    prefs_.stealth_mode = stealth;
    [self savePrefs];
    [self setTrayVisible:!stealth];
    ui_visible_ = false;
    [window_ orderOut:nil];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [self updateSportTimer];
}

- (void)togglePause {
    paused_ = !paused_;
    [self rebuildStatusMenu];
    [self evalJS:std::string("window.receivePaused&&window.receivePaused(") + (paused_ ? "true" : "false") + ");"];
    [self updateSportTimer];
}

- (void)startUpdateCheck:(BOOL)manual {
    if (update_checking_.exchange(true)) return;
    update_status_ = "checking";
    [self sendUpdateState];

    NSURL* url = [NSURL URLWithString:@"https://api.github.com/repos/Amirmpm/typeCount/releases/latest"];
    NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:url cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:10.0];
    [req setValue:[NSString stringWithFormat:@"TypeCount/%s", TC_VERSION_STR] forHTTPHeaderField:@"User-Agent"];
    [req setValue:@"application/vnd.github+json" forHTTPHeaderField:@"Accept"];

    [[[NSURLSession sharedSession] dataTaskWithRequest:req completionHandler:^(NSData* data, NSURLResponse* resp, NSError* err) {
        NSInteger code = [resp isKindOfClass:[NSHTTPURLResponse class]] ? ((NSHTTPURLResponse*)resp).statusCode : 0;
        std::string body = (!err && data) ? std::string(static_cast<const char*>(data.bytes), data.length) : "";
        dispatch_async(dispatch_get_main_queue(), ^{
            self->update_checking_.store(false);
            self->prefs_.last_update_check = std::time(nullptr);
            [self savePrefs];

            tc::JsonValue root;
            if (body.empty() || !tc::JsonValue::parse(body, root) || !root.is_object() || code == 403 || code == 429) {
                self->update_status_ = (code == 403 || code == 429) ? "rate_limited" : "error";
                if (manual) [self toastKey:(code == 403 || code == 429) ? "update.rate_limited" : "update.failed" path:""];
                [self sendUpdateState];
                return;
            }
            std::string tag = root.get("tag_name").as_string();
            std::string ver = (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) ? tag.substr(1) : tag;
            int num = parse_version_num(ver);
            std::string rel_url = root.get("html_url").as_string("https://github.com/Amirmpm/typeCount/releases");
            std::string dmg_url, zip_url;
            const auto& assets = root.get("assets");
            if (assets.is_array()) {
                for (const auto& a : assets.arr) {
                    std::string name = a.get("name").as_string();
                    std::string dl = a.get("browser_download_url").as_string();
                    if (name.find("macos") != std::string::npos) {
                        if (name.size() >= 4 && name.substr(name.size() - 4) == ".dmg") dmg_url = dl;
                        else if (name.size() >= 4 && name.substr(name.size() - 4) == ".zip") zip_url = dl;
                    }
                }
            }
            std::string dl_url = !dmg_url.empty() ? dmg_url : (!zip_url.empty() ? zip_url : rel_url);
            if (!ver.empty() && num > TC_VERSION_NUM) {
                self->update_status_ = "available";
                self->available_version_ = ver;
                self->available_release_url_ = rel_url;
                self->available_download_url_ = dl_url;
                tc::JsonValue p = tc::JsonValue::object();
                p.set("version", ver);
                p.set("url", rel_url);
                p.set("notes", root.get("body").as_string());
                [self evalJS:"window.receiveUpdateAvailable&&window.receiveUpdateAvailable(" + p.dump() + ");"];
            } else {
                self->update_status_ = "up_to_date";
                self->available_version_ = ver;
                self->available_release_url_ = rel_url;
                if (manual) {
                    [self evalJS:"window.toast&&window.toast(ClickPulseI18n.t('update.up_to_date',{version:" +
                                 tc::JsonValue::escape_string(TC_VERSION_STR) + "}));"];
                }
            }
            [self sendUpdateState];
        });
    }] resume];
}

- (std::string)pickSaveFile:(NSString*)suggested {
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.nameFieldStringValue = suggested;
    if ([panel runModal] == NSModalResponseOK && panel.URL) {
        return to_std(panel.URL.path);
    }
    return "";
}

- (std::string)pickOpenFile {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    if ([panel runModal] == NSModalResponseOK && panel.URL) {
        return to_std(panel.URL.path);
    }
    return "";
}

- (std::string)pickFolder {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = YES;
    panel.allowsMultipleSelection = NO;
    if ([panel runModal] == NSModalResponseOK && panel.URL) {
        return to_std(panel.URL.path);
    }
    return "";
}

- (void)handleIpcMessage:(const std::string&)raw {
    tc::JsonValue msg;
    if (!tc::JsonValue::parse(raw, msg) || !msg.is_object()) return;
    const std::string action = msg.get("action").as_string();
    if (action.empty()) return;

    if (action == "ready") {
        if (session_state_.is_object()) {
            [self evalJS:"window.restoreSessionState&&window.restoreSessionState(" + session_state_.dump() + ");"];
        }
        [self sendViewState];
        [self sendSnapshot];
        [self sendUpdateState];
    } else if (action == "pause") {
        [self togglePause];
    } else if (action == "drag") {
        if (window_ && [NSApp currentEvent]) {
            [window_ performWindowDragWithEvent:[NSApp currentEvent]];
        }
    } else if (action == "minimize") {
        [window_ miniaturize:nil];
    } else if (action == "maximize") {
        [window_ zoom:nil];
    } else if (action == "close") {
        [self windowShouldClose:window_];
    } else if (action == "close_preference") {
        prefs_.close_action = msg.get("close_action").as_string_or("ask");
        [self savePrefs];
        [self sendViewState];
    } else if (action == "close_decision") {
        std::string act = msg.get("close_action").as_string_or("hide");
        if (msg.get("remember").as_bool(false)) {
            prefs_.close_action = act;
            [self savePrefs];
        }
        if (act == "quit") { quitting_ = true; [NSApp terminate:nil]; }
        else if (act == "stealth") [self hideMainWindow:true];
        else [self hideMainWindow:false];
    } else if (action == "compact") {
        prefs_.compact = true;
        [self savePrefs];
        [self applyWindowGeometryForMode];
        [self rebuildStatusMenu];
        [self sendViewState];
        [self sendSnapshot];
        [self updateSportTimer];
    } else if (action == "expand") {
        prefs_.compact = false;
        [self savePrefs];
        [self applyWindowGeometryForMode];
        [self rebuildStatusMenu];
        [self sendViewState];
        [self sendSnapshot];
        [self updateSportTimer];
    } else if (action == "hide") {
        [self hideMainWindow:false];
    } else if (action == "enter_stealth") {
        [self hideMainWindow:true];
    } else if (action == "pin") {
        prefs_.pinned = !prefs_.pinned;
        [self savePrefs];
        if (prefs_.compact && window_) window_.level = prefs_.pinned ? NSFloatingWindowLevel : NSNormalWindowLevel;
        [self sendViewState];
    } else if (action == "language") {
        std::string lang = msg.get("language").as_string_or("en");
        prefs_.language = tc::Preferences::is_valid_language(lang) ? lang : "en";
        if (!demo_mode_ && db_.get_meta("jalali_user_set").empty()) {
            prefs_.jalali_calendar = ([self effectiveLocale] == "fa");
        }
        [self savePrefs];
        [self rebuildStatusMenu];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "theme") {
        std::string th = msg.get("theme").as_string_or("blue");
        if (th == "blue" || th == "teal" || th == "violet" || th == "graphite") prefs_.theme = th;
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "color_mode") {
        std::string cm = msg.get("color_mode").as_string_or("system");
        prefs_.color_mode = tc::Preferences::is_valid_color_mode(cm) ? cm : "system";
        [self savePrefs];
        [self applyAppearance];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "ui_scale") {
        prefs_.ui_scale = msg.get("scale").as_string_or("100");
        [self savePrefs];
        [self sendViewState];
    } else if (action == "today_period") {
        prefs_.today_period = msg.get("period").as_string_or("calendar_day");
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "timezone") {
        prefs_.timezone = msg.get("timezone").as_string_or("system");
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "keyboard_layout") {
        prefs_.keyboard_layout = msg.get("layout").as_string_or("compact");
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "keyboard_view") {
        prefs_.keyboard_view = msg.get("view").as_string_or("layout");
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "ignore_software_input") {
        prefs_.ignore_software_input = msg.get("enabled").as_bool(false);
        [self savePrefs];
        [self sendViewState];
    } else if (action == "track_apps") {
        prefs_.track_apps = msg.get("enabled").as_bool(true);
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "startup_mode") {
        prefs_.startup_mode = msg.get("mode").as_string_or("tray");
        [self savePrefs];
        [self sendViewState];
    } else if (action == "wake_hotkey") {
        prefs_.wake_hotkey = msg.get("hotkey").as_string_or("Ctrl+Alt+Shift+T");
        wake_hotkey_ = parse_mac_hotkey(prefs_.wake_hotkey);
        [self savePrefs];
        [self toastKey:"message.saved" path:""];
        [self sendViewState];
    } else if (action == "daily_goal") {
        prefs_.daily_goal = std::max<int64_t>(500, std::min<int64_t>(500000, msg.get("goal").as_int(8000)));
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "break_interval") {
        prefs_.break_interval_min = static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(240, msg.get("minutes").as_int(45))));
        break_notified_ = false;
        ergo_notified_ = false;
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "dpi_auto") {
        prefs_.dpi_auto = msg.get("enabled").as_bool(true);
        if (prefs_.dpi_auto) {
            detected_dpi_ = detect_monitor_ppi_mac(window_);
            prefs_.dpi_ppi = detected_dpi_;
        }
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "detect_dpi") {
        prefs_.dpi_auto = true;
        detected_dpi_ = detect_monitor_ppi_mac(window_);
        prefs_.dpi_ppi = detected_dpi_;
        [self savePrefs];
        [self toastKey:"message.saved" path:""];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "dpi_ppi") {
        prefs_.dpi_auto = false;
        prefs_.dpi_ppi = static_cast<int>(std::max<int64_t>(72, std::min<int64_t>(360, msg.get("ppi").as_int(110))));
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "auto_backup") {
        prefs_.auto_backup = msg.get("enabled").as_bool(true);
        [self savePrefs];
        [self sendViewState];
    } else if (action == "reset_break") {
        engine_->reset_break_timer(tc::posix_util::now_utc_ms());
        break_notified_ = false;
        ergo_notified_ = false;
        [self sendSnapshot];
    } else if (action == "save_focus_session") {
        int dur = static_cast<int>(msg.get("duration_sec").as_int(1500));
        int64_t tot = msg.get("total").as_int(0);
        db_.record_focus_session(tc::posix_util::now_utc_ms() / 1000 - dur, dur, tot, 0, "Focus");
        if (prefs_.sound_alerts_enabled) [self playPastelWav:prefs_.notification_sound];
        [self sendSnapshot];
    } else if (action == "sport") {
        prefs_.sport_enabled = msg.get("enabled").as_bool(false);
        [self savePrefs];
        [self sendViewState];
        [self updateSportTimer];
    } else if (action == "sport_visible") {
        prefs_.sport_visible = msg.get("visible").as_bool(true);
        [self savePrefs];
        [self sendViewState];
    } else if (action == "sport_sample") {
        [self onSportTimer:nil];
    } else if (action == "typing_test_enabled") {
        prefs_.typing_test_enabled = msg.get("enabled").as_bool(true);
        [self savePrefs];
        [self sendViewState];
    } else if (action == "typing_test_count_stats") {
        prefs_.typing_test_count_stats = msg.get("enabled").as_bool(false);
        [self savePrefs];
        [self sendViewState];
    } else if (action == "typing_test_state") {
        typing_test_running_ = msg.get("running").as_bool(false);
    } else if (action == "save_typing_test") {
        int dur = static_cast<int>(std::max<int64_t>(5, msg.get("duration_sec").as_int(30)));
        double wpm = msg.get("wpm").as_double(0.0);
        double raw_cpm = msg.get("raw_cpm").as_double(wpm * 5.0);
        double acc = msg.get("accuracy").as_double(100.0);
        int errors = static_cast<int>(std::max<int64_t>(0, msg.get("errors").as_int(0)));
        int chars = static_cast<int>(std::max<int64_t>(0, msg.get("chars").as_int(0)));
        std::string lang = msg.get("lang").as_string_or("en");
        if (!demo_mode_) {
            db_.record_typing_test(tc::posix_util::now_utc_ms() / 1000, dur, wpm, raw_cpm, acc, errors, chars, lang);
        }
        typing_test_running_ = false;
        [self sendSnapshot];
    } else if (action == "ergo_insights_enabled") {
        prefs_.ergo_insights_enabled = msg.get("enabled").as_bool(true);
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "ergo_guard_enabled") {
        prefs_.ergo_guard_enabled = msg.get("enabled").as_bool(true);
        ergo_notified_ = false;
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "smart_alerts_enabled") {
        prefs_.smart_alerts_enabled = msg.get("enabled").as_bool(true);
        [self savePrefs];
        [self sendViewState];
    } else if (action == "sound_alerts_enabled") {
        prefs_.sound_alerts_enabled = msg.get("enabled").as_bool(true);
        [self savePrefs];
        if (prefs_.sound_alerts_enabled) [self playPastelWav:prefs_.notification_sound];
        [self sendViewState];
    } else if (action == "notification_sound") {
        std::string snd = msg.get("sound").as_string_or("pastel_pearl");
        prefs_.notification_sound = tc::Preferences::is_valid_sound(snd) ? snd : "pastel_pearl";
        [self savePrefs];
        if (prefs_.notification_sound != "none") [self playPastelWav:prefs_.notification_sound];
        [self sendViewState];
    } else if (action == "preview_sound") {
        std::string snd = msg.get("sound").as_string_or(prefs_.notification_sound);
        if (!tc::Preferences::is_valid_sound(snd) || snd == "none") snd = prefs_.notification_sound;
        if (snd != "none") [self playPastelWav:snd];
    } else if (action == "app_exclusions_enabled") {
        prefs_.app_exclusions_enabled = msg.get("enabled").as_bool(true);
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "pause_in_excluded_apps") {
        prefs_.pause_in_excluded_apps = msg.get("enabled").as_bool(false);
        [self savePrefs];
        [self sendViewState];
    } else if (action == "exclude_app") {
        std::string app = msg.get("app").as_string();
        if (!app.empty() && !tc::posix_util::is_excluded_app(prefs_, app)) {
            prefs_.excluded_apps.push_back(app);
            [self savePrefs];
            [self toastKey:"apps.excluded_toast" path:""];
        }
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "unexclude_app") {
        std::string app = msg.get("app").as_string();
        prefs_.excluded_apps.erase(
            std::remove_if(prefs_.excluded_apps.begin(), prefs_.excluded_apps.end(),
                           [&](const std::string& x) { return tc::posix_util::iequals(x, app); }),
            prefs_.excluded_apps.end());
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "idle_detection_enabled") {
        prefs_.idle_detection_enabled = msg.get("enabled").as_bool(true);
        engine_->set_idle_timeout_sec(prefs_.idle_detection_enabled ? prefs_.idle_timeout_sec : 180);
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "idle_timeout_sec") {
        prefs_.idle_timeout_sec = static_cast<int>(std::max<int64_t>(30, std::min<int64_t>(1800, msg.get("seconds").as_int(180))));
        engine_->set_idle_timeout_sec(prefs_.idle_detection_enabled ? prefs_.idle_timeout_sec : 180);
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "jalali_calendar") {
        prefs_.jalali_calendar = msg.get("enabled").as_bool(true);
        if (!demo_mode_) db_.set_meta("jalali_user_set", "1");
        [self savePrefs];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "automatic_updates") {
        prefs_.auto_check_updates = msg.get("enabled").as_bool(true);
        [self savePrefs];
        [self sendViewState];
        [self sendUpdateState];
        if (prefs_.auto_check_updates && !demo_mode_) [self startUpdateCheck:NO];
    } else if (action == "check_updates") {
        [self startUpdateCheck:YES];
    } else if (action == "update_release" || action == "install_update") {
        std::string url = (action == "install_update" && !available_download_url_.empty())
            ? available_download_url_ : available_release_url_;
        if (url.empty()) url = TC_RELEASES_URL;
        [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:to_ns(url)]];
    } else if (action == "session_state") {
        session_state_ = msg.get("state");
        if (session_state_.get("page").as_string() == "share") [self sendSnapshot];
    } else if (action == "history_hours") {
        int64_t req_id = msg.get("id").as_int(0);
        std::string st = msg.get("start").as_string();
        std::string en = msg.get("end").as_string();
        int tz = tc::posix_util::timezone_offset_sec_for(prefs_.timezone);
        tc::JsonValue rows = demo_mode_
            ? db_.query_history_hours_demo(st, en)
            : db_.query_history_hours(st, en, tz);
        [self evalJS:"window.receiveHistoryHours&&window.receiveHistoryHours(" + std::to_string(req_id) + "," + rows.dump() + ");"];
    } else if (action == "autostart") {
        bool cur = is_autostart_enabled_mac();
        if (!set_autostart_enabled_mac(!cur)) [self toastKey:"message.autostart_failed" path:""];
        else [self toastKey:(!cur ? "message.autostart_enabled" : "message.autostart_disabled") path:""];
        [self sendViewState];
        [self sendSnapshot];
    } else if (action == "reveal") {
        std::string dir = prefs_.custom_db_dir.empty() ? data_dir_ : prefs_.custom_db_dir;
        [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:to_ns(dir)]];
    } else if (action == "storage_directory") {
        std::string chosen = [self pickFolder];
        if (!chosen.empty()) {
            engine_->flush();
            std::string dst_db = (std::filesystem::path(chosen) / "typecount.sqlite3").string();
            if (std::filesystem::exists(dst_db)) {
                prefs_.custom_db_dir = chosen;
                [self savePrefs];
                db_.open(dst_db);
                db_mode_ = "custom";
                [self toastKey:"storage.opened_existing" path:""];
                [self sendViewState];
                [self sendSnapshot];
                return;
            }
            std::string err;
            if (db_.backup_to(dst_db, &err)) {
                prefs_.custom_db_dir = chosen;
                [self savePrefs];
                db_.open(dst_db);
                db_mode_ = "custom";
                [self toastKey:"storage.moved" path:""];
                [self sendViewState];
                [self sendSnapshot];
            } else {
                [self toastKey:"storage.failed" path:""];
            }
        }
    } else if (action == "reset_storage_directory") {
        if (!prefs_.custom_db_dir.empty()) {
            engine_->flush();
            std::string def_db = (std::filesystem::path(data_dir_) / "typecount.sqlite3").string();
            prefs_.custom_db_dir.clear();
            [self savePrefs];
            db_.open(def_db);
            db_mode_ = "default";
            [self toastKey:"storage.reset_done" path:""];
            [self sendViewState];
            [self sendSnapshot];
        }
    } else if (action == "import_history") {
        std::string src = [self pickOpenFile];
        if (!src.empty()) {
            [self evalJS:"window.receiveDataBusy&&window.receiveDataBusy(true);"];
            auto rep = db_.import_database(src, (std::filesystem::path(data_dir_) / "backups").string());
            [self evalJS:"window.receiveDataBusy&&window.receiveDataBusy(false);"];
            if (rep.ok) {
                [self sendSnapshot];
                if (rep.added_ops > 0) {
                    [self evalJS:"window.toast&&window.toast(ClickPulseI18n.t('storage.imported',{count:ClickPulseI18n.number(" +
                                 std::to_string(rep.added_ops) + "),overlap:ClickPulseI18n.number(" +
                                 std::to_string(rep.overlapped_minutes) + "),path:" +
                                 tc::JsonValue::escape_string(rep.backup_path) + "}),7000);"];
                } else {
                    [self toastKey:"storage.no_new_history" path:""];
                }
            } else {
                [self toastKey:"storage.failed" path:""];
            }
        }
    } else if (action == "export_csv") {
        std::string today = tc::Db::format_day(tc::posix_util::now_utc_ms() / 1000, tc::posix_util::timezone_offset_sec_for(prefs_.timezone));
        std::string out = [self pickSaveFile:to_ns("TypeCount-" + today + ".csv")];
        if (!out.empty()) {
            engine_->flush();
            if (db_.export_csv(out, tc::posix_util::timezone_offset_sec_for(prefs_.timezone), nullptr, prefs_.dpi_ppi)) {
                [self toastKey:"export.saved" path:out];
            } else {
                [self toastKey:"export.failed" path:""];
            }
        }
    } else if (action == "export_json") {
        std::string today = tc::Db::format_day(tc::posix_util::now_utc_ms() / 1000, tc::posix_util::timezone_offset_sec_for(prefs_.timezone));
        std::string out = [self pickSaveFile:to_ns("TypeCount-" + today + ".json")];
        if (!out.empty()) {
            engine_->flush();
            if (db_.export_json(out, tc::posix_util::timezone_offset_sec_for(prefs_.timezone), nullptr, prefs_.dpi_ppi)) {
                [self toastKey:"export.saved" path:out];
            } else {
                [self toastKey:"export.failed" path:""];
            }
        }
    } else if (action == "backup_now") {
        engine_->flush();
        std::string created;
        if (db_.create_rotating_backup((std::filesystem::path(data_dir_) / "backups").string(), 5, &created)) {
            [self toastKey:"export.backup_saved" path:created];
        } else {
            [self toastKey:"export.failed" path:""];
        }
    } else if (action == "save_share_card") {
        std::string day = msg.get("day").as_string_or("report");
        std::string period = msg.get("period").as_string_or("");
        std::string fname = "TypeCount-" + (period == "weekly" ? ("Weekly-" + msg.get("start").as_string_or("") + "_") : "") + day + ".png";
        std::string out = [self pickSaveFile:to_ns(fname)];
        if (!out.empty()) {
            auto bytes = tc::posix_util::decode_base64_data_url(msg.get("png").as_string());
            std::ofstream f(out, std::ios::binary | std::ios::trunc);
            if (!bytes.empty() && f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())).good()) {
                [self toastKey:"share.saved" path:""];
            } else {
                [self toastKey:"share.failed" path:""];
            }
        }
    } else if (action == "copy_share_card") {
        auto bytes = tc::posix_util::decode_base64_data_url(msg.get("png").as_string());
        bool copied = false;
        if (!bytes.empty()) {
            NSData* d = [NSData dataWithBytes:bytes.data() length:bytes.size()];
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            copied = ([pb setData:d forType:NSPasteboardTypePNG] == YES);
        }
        [self toastKey:copied ? "share.copied" : "share.copy_failed" path:""];
    } else if (action == "project_home" || action == "shortcut_atlas") {
        [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://github.com/Amirmpm/typeCount"]];
    } else if (action == "send_feedback" || action == "suggest_shortcut") {
        [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://github.com/Amirmpm/typeCount/issues/new"]];
    } else if (action == "permission") {
        CGRequestListenEventAccess();
        [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent"]];
        [self setupInputTap];
        [self sendSnapshot];
    }
}

@end

int main(int argc, const char** argv) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        TypeCountMacController* ctrl = [[TypeCountMacController alloc] init];
        app.delegate = ctrl;
        [app run];
    }
    return 0;
}
