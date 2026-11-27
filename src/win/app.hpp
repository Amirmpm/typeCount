#pragma once

#include "../core/db.hpp"
#include "../core/engine.hpp"
#include "../core/json.hpp"
#include "../core/key_mapper.hpp"
#include "../core/preferences.hpp"
#include "platform.hpp"
#include "webview_host.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace tc {
namespace win {

constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_APP_ACTIVATE = WM_APP + 2;

constexpr UINT_PTR TIMER_FLUSH = 101;
constexpr UINT_PTR TIMER_SPORT = 102;
constexpr UINT_PTR TIMER_DISPOSE_UI = 103;
constexpr int HOTKEY_WAKE_ID = 1;

constexpr UINT MENU_OPEN = 1001;
constexpr UINT MENU_MINI = 1002;
constexpr UINT MENU_STEALTH = 1003;
constexpr UINT MENU_PAUSE = 1004;
constexpr UINT MENU_AUTOSTART = 1005;
constexpr UINT MENU_QUIT = 1006;

class TypeCountApp {
public:
    TypeCountApp() : engine_(db_) {}
    ~TypeCountApp() { shutdown(); }

    int run(HINSTANCE hinst, const std::wstring& cmd_line) {
        hinst_ = hinst;

        // Parse launch flags
        bool arg_background = cmd_line.find(L"--background") != std::wstring::npos;
        bool arg_stealth = cmd_line.find(L"--stealth") != std::wstring::npos;
        bool arg_tray = cmd_line.find(L"--tray") != std::wstring::npos;
        bool arg_demo = cmd_line.find(L"--demo") != std::wstring::npos;
        bool arg_no_autostart = cmd_line.find(L"--no-autostart") != std::wstring::npos;
        demo_mode_ = arg_demo;

        // Single-instance check & activation of existing instance (even in no-tray stealth mode)
        mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\TypeCount_SingleInstance_v1");
        if (mutex_ && GetLastError() == ERROR_ALREADY_EXISTS) {
            for (int i = 0; i < 16; ++i) {
                HWND existing = FindWindowW(L"TypeCountCaptureHost", nullptr);
                if (existing) {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(existing, &pid);
                    if (pid) AllowSetForegroundWindow(pid);
                    PostMessageW(existing, WM_APP_ACTIVATE, 0, 0);
                    break;
                }
                Sleep(50);
            }
            CloseHandle(mutex_);
            mutex_ = nullptr;
            return 0;
        }

        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        data_dir_ = default_data_dir_wide();
        ensure_dir_wide(data_dir_);
        prefs_path_ = data_dir_ + L"\\preferences.json";

        bool prefs_existed = false;
        prefs_ = Preferences::load_file(to_utf8(prefs_path_), &prefs_existed);
        if (!prefs_existed && !arg_no_autostart && !demo_mode_) {
            set_autostart_enabled(true);
            prefs_.save_file(to_utf8(prefs_path_));
        }

        // Open SQLite DB (custom dir or default)
        std::wstring active_db_dir = data_dir_;
        db_mode_ = "default";
        if (!prefs_.custom_db_dir.empty()) {
            std::wstring custom = to_wide(prefs_.custom_db_dir);
            if (std::filesystem::exists(std::filesystem::path(custom))) {
                active_db_dir = custom;
                db_mode_ = "custom";
            }
        }
        std::wstring db_file = active_db_dir + L"\\typecount.sqlite3";
        std::string db_err;
        if (!db_.open(to_utf8(db_file), &db_err)) {
            capture_status_.error = true;
            capture_status_.error_code = "error.storage";
            capture_status_.error_message = db_err;
        }

        // Determine initial visibility & tray/stealth state
        if (arg_stealth) {
            prefs_.stealth_mode = true;
        } else if (arg_tray) {
            prefs_.stealth_mode = false;
        } else if (arg_background) {
            prefs_.stealth_mode = (prefs_.startup_mode == "stealth");
        }

        wm_taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");

        if (!create_capture_host()) {
            MessageBoxW(nullptr, L"Unable to initialize TypeCount host window.", L"TypeCount", MB_ICONERROR);
            return 1;
        }

        register_raw_input();
        apply_wake_hotkey();

        if (!prefs_.stealth_mode) {
            add_tray_icon();
        }

        SetTimer(capture_hwnd_, TIMER_FLUSH, 1500, nullptr);

        bool start_hidden = arg_background || arg_stealth || arg_tray;
        if (!start_hidden) {
            show_main_window();
        }

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        shutdown();
        return static_cast<int>(msg.wParam);
    }

private:
    HINSTANCE hinst_ = nullptr;
    HANDLE mutex_ = nullptr;
    HWND capture_hwnd_ = nullptr;
    HWND ui_hwnd_ = nullptr;
    HICON app_icon_ = nullptr;
    UINT wm_taskbar_created_ = 0;
    bool tray_added_ = false;
    bool ui_visible_ = false;
    bool paused_ = false;
    bool demo_mode_ = false;
    bool break_notified_ = false;

    std::wstring data_dir_;
    std::wstring prefs_path_;
    std::string db_mode_ = "default";
    std::string last_backup_day_;

    Preferences prefs_;
    Db db_;
    StatsEngine engine_;
    KeyTracker key_tracker_;
    CaptureStatusInfo capture_status_;
    WebViewHost webview_;
    JsonValue session_state_;
    std::vector<uint8_t> raw_buf_;

    // Mouse absolute movement state
    bool has_last_abs_ = false;
    LONG last_abs_x_ = 0;
    LONG last_abs_y_ = 0;

    // Sport window tick state
    int64_t last_sport_tick_ms_ = 0;
    uint64_t sport_total_prev_ = 0;

    // Saved normal window rect when switching to Mini mode
    RECT normal_rect_{100, 100, 1260, 940};
    bool has_normal_rect_ = false;

    void shutdown() {
        KillTimer(capture_hwnd_, TIMER_FLUSH);
        KillTimer(capture_hwnd_, TIMER_SPORT);
        KillTimer(capture_hwnd_, TIMER_DISPOSE_UI);
        if (capture_hwnd_) UnregisterHotKey(capture_hwnd_, HOTKEY_WAKE_ID);
        remove_tray_icon();
        webview_.destroy();
        if (ui_hwnd_) {
            DestroyWindow(ui_hwnd_);
            ui_hwnd_ = nullptr;
        }
        if (capture_hwnd_) {
            DestroyWindow(capture_hwnd_);
            capture_hwnd_ = nullptr;
        }
        engine_.flush();
        db_.close();
        if (app_icon_) {
            DestroyIcon(app_icon_);
            app_icon_ = nullptr;
        }
        if (mutex_) {
            CloseHandle(mutex_);
            mutex_ = nullptr;
        }
        CoUninitialize();
    }

    // ---------- Window creation ----------

    bool create_capture_host() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = capture_wnd_proc_static;
        wc.hInstance = hinst_;
        wc.lpszClassName = L"TypeCountCaptureHost";
        RegisterClassExW(&wc);

        capture_hwnd_ = CreateWindowExW(
            WS_EX_TOOLWINDOW, L"TypeCountCaptureHost", L"TypeCountCaptureHost",
            WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hinst_, this);
        if (!capture_hwnd_) return false;

        app_icon_ = static_cast<HICON>(LoadImageW(
            hinst_, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        if (!app_icon_) {
            app_icon_ = LoadIconW(nullptr, IDI_APPLICATION);
        }
        return true;
    }

    void ensure_ui_window() {
        if (ui_hwnd_) return;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ui_wnd_proc_static;
        wc.hInstance = hinst_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = static_cast<HICON>(LoadImageW(hinst_, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
        wc.hIconSm = app_icon_;
        wc.hbrBackground = CreateSolidBrush(RGB(243, 246, 252));
        wc.lpszClassName = L"TypeCountMainWindow";
        RegisterClassExW(&wc);

        int x = prefs_.window_x >= 0 ? prefs_.window_x : CW_USEDEFAULT;
        int y = prefs_.window_y >= 0 ? prefs_.window_y : CW_USEDEFAULT;
        int w = prefs_.window_width;
        int h = prefs_.window_height;

        if (prefs_.compact) {
            w = 320;
            h = 208;
        }

        ui_hwnd_ = CreateWindowExW(
            WS_EX_APPWINDOW,
            L"TypeCountMainWindow",
            L"TypeCount",
            WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
            x, y, w, h,
            nullptr, nullptr, hinst_, this);
        if (!ui_hwnd_) return;

        // Win11 rounded corners
        DWORD round_pref = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(ui_hwnd_, static_cast<DWMWINDOWATTRIBUTE>(33), &round_pref, sizeof(round_pref));

        if (x == CW_USEDEFAULT) {
            RECT rc{};
            GetWindowRect(ui_hwnd_, &rc);
            int sw = GetSystemMetrics(SM_CXSCREEN);
            int sh = GetSystemMetrics(SM_CYSCREEN);
            int rw = rc.right - rc.left;
            int rh = rc.bottom - rc.top;
            SetWindowPos(ui_hwnd_, nullptr, (sw - rw) / 2, (sh - rh) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }

        std::wstring wv_dir = data_dir_ + L"\\WebView2";
        ensure_dir_wide(wv_dir);
        std::string init_loc = (prefs_.language == "system") ? "en" : prefs_.language;

        webview_.init(
            ui_hwnd_,
            wv_dir,
            init_loc,
            prefs_.theme,
            [this](const std::string& json_str) { handle_ipc_message(json_str); },
            [this]() { on_webview_ready(); },
            [this]() {
                MessageBoxW(
                    ui_hwnd_,
                    L"Microsoft Edge WebView2 Runtime is required to display the TypeCount interface.\n"
                    L"Background counting remains active.\n\n"
                    L"Download: https://go.microsoft.com/fwlink/p/?LinkId=2124703",
                    L"TypeCount",
                    MB_ICONWARNING);
            });
    }

    void show_main_window() {
        KillTimer(capture_hwnd_, TIMER_DISPOSE_UI);
        ensure_ui_window();
        if (!ui_hwnd_) return;

        apply_window_geometry_for_mode();

        ui_visible_ = true;
        if (IsIconic(ui_hwnd_)) ShowWindow(ui_hwnd_, SW_RESTORE);
        ShowWindow(ui_hwnd_, SW_SHOW);
        SetWindowPos(
            ui_hwnd_,
            (prefs_.compact && prefs_.pinned) ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(ui_hwnd_);
        webview_.set_visible(true);
        webview_.resize_to_parent();
        webview_.focus();

        if (webview_.is_ready()) {
            send_view_state();
            send_snapshot();
        }
        update_sport_timer();
    }

    void hide_main_window(bool stealth) {
        if (stealth) {
            prefs_.stealth_mode = true;
            save_prefs();
            remove_tray_icon();
            apply_wake_hotkey();
        } else {
            prefs_.stealth_mode = false;
            save_prefs();
            add_tray_icon();
        }

        ui_visible_ = false;
        webview_.set_visible(false);
        if (ui_hwnd_) {
            ShowWindow(ui_hwnd_, SW_HIDE);
        }
        update_sport_timer();
        // Low-memory background disposal: close Chromium controller after 45s while hidden
        SetTimer(capture_hwnd_, TIMER_DISPOSE_UI, 45000, nullptr);
    }

    void apply_window_geometry_for_mode() {
        if (!ui_hwnd_) return;
        UINT dpi = window_dpi(ui_hwnd_);
        if (prefs_.compact) {
            if (!IsZoomed(ui_hwnd_)) {
                GetWindowRect(ui_hwnd_, &normal_rect_);
                has_normal_rect_ = true;
            }
            int cw = MulDiv(320, static_cast<int>(dpi), 96);
            int ch = MulDiv(208, static_cast<int>(dpi), 96);
            SetWindowPos(
                ui_hwnd_,
                prefs_.pinned ? HWND_TOPMOST : HWND_NOTOPMOST,
                0, 0, cw, ch,
                SWP_NOMOVE | SWP_NOACTIVATE);
        } else {
            int rw = has_normal_rect_ ? (normal_rect_.right - normal_rect_.left) : MulDiv(prefs_.window_width, static_cast<int>(dpi), 96);
            int rh = has_normal_rect_ ? (normal_rect_.bottom - normal_rect_.top) : MulDiv(prefs_.window_height, static_cast<int>(dpi), 96);
            SetWindowPos(ui_hwnd_, HWND_NOTOPMOST, 0, 0, rw, rh, SWP_NOMOVE | SWP_NOACTIVATE);
        }
        webview_.resize_to_parent();
    }

    // ---------- Raw Input & Hotkey ----------

    void register_raw_input() {
        RAWINPUTDEVICE devs[2]{};
        devs[0].usUsagePage = 0x01;
        devs[0].usUsage = 0x06; // Keyboard
        devs[0].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
        devs[0].hwndTarget = capture_hwnd_;

        devs[1].usUsagePage = 0x01;
        devs[1].usUsage = 0x02; // Mouse
        devs[1].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
        devs[1].hwndTarget = capture_hwnd_;

        if (!RegisterRawInputDevices(devs, 2, sizeof(RAWINPUTDEVICE))) {
            capture_status_.listening = false;
            capture_status_.error = true;
            capture_status_.error_code = "error.capture";
        }
    }

    void apply_wake_hotkey() {
        if (!capture_hwnd_) return;
        UnregisterHotKey(capture_hwnd_, HOTKEY_WAKE_ID);
        UINT mods = 0, vk = 0;
        if (parse_wake_hotkey(prefs_.wake_hotkey, mods, vk)) {
            RegisterHotKey(capture_hwnd_, HOTKEY_WAKE_ID, mods, vk);
        }
    }

    void handle_raw_input(HRAWINPUT hraw) {
        if (paused_) return;

        UINT size = 0;
        if (GetRawInputData(hraw, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0) {
            return;
        }
        if (raw_buf_.size() < size) raw_buf_.resize(size);
        if (GetRawInputData(hraw, RID_INPUT, raw_buf_.data(), &size, sizeof(RAWINPUTHEADER)) != size) {
            return;
        }

        const RAWINPUT* ri = reinterpret_cast<const RAWINPUT*>(raw_buf_.data());
        int64_t now_ms = now_utc_ms();
        int tz_off = current_tz_offset_sec();
        std::string app_name;
        if (prefs_.track_apps) {
            app_name = foreground_process_name();
            if (!app_name.empty()) {
                for (const auto& ex : prefs_.excluded_apps) {
                    if (_stricmp(app_name.c_str(), ex.c_str()) == 0) {
                        app_name.clear();
                        break;
                    }
                }
            }
        }

        if (ri->header.dwType == RIM_TYPEKEYBOARD) {
            const RAWKEYBOARD& rk = ri->data.keyboard;
            uintptr_t dev = reinterpret_cast<uintptr_t>(ri->header.hDevice);
            auto code = key_tracker_.process_event(
                dev,
                rk.MakeCode,
                rk.Flags,
                rk.VKey,
                static_cast<uint64_t>(now_ms),
                prefs_.ignore_software_input);

            if (code.has_value()) {
                auto is_down = [](uint16_t vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
                auto chord = key_tracker_.shortcut_for(dev, *code, static_cast<uint64_t>(now_ms), is_down);
                bool is_mod = (modifier_vk(*code) != 0);
                engine_.on_key_press(now_ms, tz_off, *code, is_mod, chord, app_name, prefs_.track_apps);
                sport_pulse_event();
            }
        } else if (ri->header.dwType == RIM_TYPEMOUSE) {
            const RAWMOUSE& rm = ri->data.mouse;
            if (prefs_.ignore_software_input && ri->header.hDevice == nullptr) return;

            auto btns = extract_mouse_buttons(rm.usButtonFlags);
            for (uint32_t b : btns) {
                engine_.on_mouse_click(now_ms, tz_off, b, app_name, prefs_.track_apps);
                sport_pulse_event();
            }

            if ((rm.usButtonFlags & 0x0400) || (rm.usButtonFlags & 0x0800)) {
                SHORT wdelta = static_cast<SHORT>(rm.usButtonData);
                int steps = std::max(1, static_cast<int>(std::abs(wdelta) / 120));
                engine_.on_mouse_scroll(now_ms, tz_off, steps, app_name, prefs_.track_apps);
            }

            int64_t dist_px = 0;
            if (rm.usFlags & MOUSE_MOVE_ABSOLUTE) {
                if (has_last_abs_) {
                    LONG dx = rm.lLastX - last_abs_x_;
                    LONG dy = rm.lLastY - last_abs_y_;
                    // Normalized 0..65535 scaled roughly to screen
                    double sx = static_cast<double>(dx) * GetSystemMetrics(SM_CXSCREEN) / 65535.0;
                    double sy = static_cast<double>(dy) * GetSystemMetrics(SM_CYSCREEN) / 65535.0;
                    dist_px = static_cast<int64_t>(std::hypot(sx, sy));
                }
                last_abs_x_ = rm.lLastX;
                last_abs_y_ = rm.lLastY;
                has_last_abs_ = true;
            } else if (rm.lLastX != 0 || rm.lLastY != 0) {
                dist_px = static_cast<int64_t>(std::hypot(static_cast<double>(rm.lLastX), static_cast<double>(rm.lLastY)));
            }
            if (dist_px > 0 && dist_px < 20000) {
                engine_.on_mouse_move(now_ms, tz_off, dist_px, app_name, prefs_.track_apps);
            }
        }
    }

    // ---------- Tray Icon ----------

    void add_tray_icon() {
        if (tray_added_ || !capture_hwnd_) return;
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = capture_hwnd_;
        nid.uID = 1;
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = app_icon_;
        std::wstring tip = (prefs_.language == "fa")
            ? L"TypeCount · شمارشگر کلید و ماوس (کلیک چپ برای بازکردن)"
            : L"TypeCount · Click left button to open";
        wcsncpy(nid.szTip, tip.c_str(), 127);
        if (Shell_NotifyIconW(NIM_ADD, &nid)) {
            tray_added_ = true;
        }
    }

    void remove_tray_icon() {
        if (!tray_added_ || !capture_hwnd_) return;
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = capture_hwnd_;
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        tray_added_ = false;
    }

    void show_tray_balloon(const std::wstring& title, const std::wstring& body) {
        if (!tray_added_ || !capture_hwnd_) return;
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = capture_hwnd_;
        nid.uID = 1;
        nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
        wcsncpy(nid.szInfoTitle, title.c_str(), 63);
        wcsncpy(nid.szInfo, body.c_str(), 255);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    void show_tray_menu() {
        HMENU menu = CreatePopupMenu();
        if (!menu) return;
        bool fa = (prefs_.language == "fa");
        AppendMenuW(menu, MF_STRING, MENU_OPEN, fa ? L"باز کردن TypeCount" : L"Open TypeCount");
        AppendMenuW(menu, MF_STRING | (prefs_.compact ? MF_CHECKED : 0), MENU_MINI, fa ? L"حالت کوچک (Mini)" : L"Mini mode");
        AppendMenuW(menu, MF_STRING, MENU_STEALTH, fa ? L"حالت مخفی کامل (بدون آیکون)" : L"Stealth mode (hide tray icon)");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (paused_ ? MF_CHECKED : 0), MENU_PAUSE,
                    paused_ ? (fa ? L"ادامه ثبت" : L"Resume recording") : (fa ? L"توقف موقت ثبت" : L"Pause recording"));
        AppendMenuW(menu, MF_STRING | (is_autostart_enabled() ? MF_CHECKED : 0), MENU_AUTOSTART,
                    fa ? L"اجرای خودکار با ویندوز" : L"Launch at Windows startup");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, MENU_QUIT, fa ? L"ذخیره و خروج" : L"Save & Quit");
        SetMenuDefaultItem(menu, MENU_OPEN, FALSE);

        POINT pt{};
        GetCursorPos(&pt);
        SetForegroundWindow(capture_hwnd_);
        UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, capture_hwnd_, nullptr);
        DestroyMenu(menu);

        switch (cmd) {
            case MENU_OPEN:
                prefs_.compact = false;
                save_prefs();
                show_main_window();
                break;
            case MENU_MINI:
                prefs_.compact = !prefs_.compact;
                save_prefs();
                show_main_window();
                break;
            case MENU_STEALTH:
                hide_main_window(true);
                break;
            case MENU_PAUSE:
                toggle_pause();
                break;
            case MENU_AUTOSTART:
                set_autostart_enabled(!is_autostart_enabled());
                if (ui_visible_ && webview_.is_ready()) send_snapshot();
                break;
            case MENU_QUIT:
                PostQuitMessage(0);
                break;
        }
    }

    // ---------- JS <-> C++ Messaging ----------

    void on_webview_ready() {
        if (session_state_.is_object()) {
            webview_.eval_js("window.restoreSessionState(" + session_state_.dump() + ");");
        }
        send_view_state();
        send_snapshot();
    }

    void send_view_state() {
        if (!webview_.is_ready()) return;
        JsonValue v = prefs_.to_json();
        v.set("locale", (prefs_.language == "system") ? "en" : prefs_.language);
        v.set("language_preference", prefs_.language);
        v.set("visible", ui_visible_);
        v.set("maximized", ui_hwnd_ ? (IsZoomed(ui_hwnd_) != FALSE) : false);
        v.set("fullscreen", false);
        v.set("auto_check_updates", false);

        JsonValue tzs = JsonValue::array();
        for (const auto& z : system_timezones_list()) tzs.push(z);
        v.set("timezones", std::move(tzs));

        webview_.eval_js("window.receiveView(" + v.dump() + ");");
    }

    SnapshotQuery make_query() {
        SnapshotQuery q;
        q.now_ms = now_utc_ms();
        q.tz_offset_sec = current_tz_offset_sec();
        q.include_shortcuts = true;
        q.include_apps = true;
        q.demo = demo_mode_;
        q.prefs = prefs_;
        q.capture = capture_status_;
        q.capture.paused = paused_;
        q.auto_start = is_autostart_enabled();
        q.auto_start_available = (db_mode_ == "default" && !demo_mode_);
        q.database_mode = db_mode_;

        // If Share/Report page is active or we have persisted share params, fill report
        const auto& ins = session_state_.get("insights");
        q.report_period = ins.get("share_period").as_string_or("daily");
        std::string sday = ins.get("share_day").as_string_or("");
        if (sday.empty()) sday = Db::format_day(q.now_ms / 1000, q.tz_offset_sec);
        q.report_day = sday;
        q.report_start = ins.get("share_start").as_string_or("");
        return q;
    }

    void send_snapshot() {
        if (!webview_.is_ready() || !ui_visible_) return;
        JsonValue snap = engine_.snapshot(make_query());
        if (prefs_.compact) {
            webview_.eval_js("window.receiveMini(" + snap.dump() + ");");
        } else {
            webview_.eval_js("window.receive(" + snap.dump() + ");");
        }
    }

    void toggle_pause() {
        paused_ = !paused_;
        if (paused_) key_tracker_.reset();
        if (webview_.is_ready()) {
            webview_.eval_js(std::string("window.receivePaused(") + (paused_ ? "true" : "false") + ");");
        }
    }

    void update_sport_timer() {
        if (ui_visible_ && prefs_.compact && prefs_.sport_enabled && !paused_) {
            SetTimer(capture_hwnd_, TIMER_SPORT, 150, nullptr);
        } else {
            KillTimer(capture_hwnd_, TIMER_SPORT);
        }
    }

    void sport_pulse_event() {
        sport_total_prev_++;
    }

    void send_sport_sample() {
        if (!webview_.is_ready() || !ui_visible_ || !prefs_.compact || !prefs_.sport_enabled) return;
        auto live = engine_.get_live_metrics(now_utc_ms(), prefs_.break_interval_min);
        JsonValue s = JsonValue::object();
        s.set("available", true);
        s.set("paused", paused_);
        s.set("cps", live.live_cps);
        s.set("rate", live.live_cps);
        s.set("wpm", live.live_wpm);
        webview_.eval_js("window.receiveSport(" + s.dump() + ");");
    }

    void save_prefs() {
        prefs_.save_file(to_utf8(prefs_path_));
    }

    void toast_key(const std::string& key, const std::string& path = "") {
        if (!webview_.is_ready()) return;
        if (path.empty()) {
            webview_.eval_js("window.toast(ClickPulseI18n.t(" + JsonValue::escape_string(key) + "));");
        } else {
            webview_.eval_js("window.toast(ClickPulseI18n.t(" + JsonValue::escape_string(key) + ",{path:" + JsonValue::escape_string(path) + "}));");
        }
    }

    void handle_ipc_message(const std::string& raw) {
        JsonValue msg;
        if (!JsonValue::parse(raw, msg) || !msg.is_object()) return;
        const std::string action = msg.get("action").as_string();

        if (action == "ready") {
            send_view_state();
            send_snapshot();
        } else if (action == "pause") {
            toggle_pause();
        } else if (action == "drag") {
            if (ui_hwnd_) {
                ReleaseCapture();
                SendMessageW(ui_hwnd_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            }
        } else if (action == "minimize") {
            if (ui_hwnd_) ShowWindow(ui_hwnd_, SW_MINIMIZE);
        } else if (action == "maximize") {
            if (ui_hwnd_) {
                ShowWindow(ui_hwnd_, IsZoomed(ui_hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
                send_view_state();
            }
        } else if (action == "close") {
            if (prefs_.close_action == "hide") hide_main_window(false);
            else if (prefs_.close_action == "stealth") hide_main_window(true);
            else if (prefs_.close_action == "quit") PostQuitMessage(0);
            else webview_.eval_js("window.requestClose();");
        } else if (action == "close_preference") {
            prefs_.close_action = msg.get("close_action").as_string_or("ask");
            save_prefs();
            send_view_state();
        } else if (action == "close_decision") {
            std::string act = msg.get("close_action").as_string_or("hide");
            if (msg.get("remember").as_bool(false)) {
                prefs_.close_action = act;
                save_prefs();
            }
            if (act == "quit") PostQuitMessage(0);
            else if (act == "stealth") hide_main_window(true);
            else hide_main_window(false);
        } else if (action == "compact") {
            prefs_.compact = true;
            save_prefs();
            apply_window_geometry_for_mode();
            send_view_state();
            send_snapshot();
            update_sport_timer();
        } else if (action == "expand") {
            prefs_.compact = false;
            save_prefs();
            apply_window_geometry_for_mode();
            send_view_state();
            send_snapshot();
            update_sport_timer();
        } else if (action == "hide") {
            hide_main_window(false);
        } else if (action == "enter_stealth") {
            hide_main_window(true);
        } else if (action == "pin") {
            prefs_.pinned = !prefs_.pinned;
            save_prefs();
            if (ui_hwnd_ && prefs_.compact) {
                SetWindowPos(ui_hwnd_, prefs_.pinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            }
            send_view_state();
        } else if (action == "language") {
            prefs_.language = msg.get("language").as_string_or("en");
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "theme") {
            prefs_.theme = msg.get("theme").as_string_or("blue");
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "ui_scale") {
            prefs_.ui_scale = msg.get("scale").as_string_or("100");
            save_prefs();
            send_view_state();
        } else if (action == "today_period") {
            prefs_.today_period = msg.get("period").as_string_or("calendar_day");
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "timezone") {
            prefs_.timezone = msg.get("timezone").as_string_or("system");
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "keyboard_layout") {
            prefs_.keyboard_layout = msg.get("layout").as_string_or("compact");
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "keyboard_view") {
            prefs_.keyboard_view = msg.get("view").as_string_or("layout");
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "ignore_software_input") {
            prefs_.ignore_software_input = msg.get("enabled").as_bool(false);
            save_prefs();
            send_view_state();
        } else if (action == "track_apps") {
            prefs_.track_apps = msg.get("enabled").as_bool(true);
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "startup_mode") {
            prefs_.startup_mode = msg.get("mode").as_string_or("tray");
            save_prefs();
            send_view_state();
        } else if (action == "wake_hotkey") {
            prefs_.wake_hotkey = msg.get("hotkey").as_string_or("Ctrl+Alt+Shift+T");
            save_prefs();
            apply_wake_hotkey();
            send_view_state();
        } else if (action == "daily_goal") {
            prefs_.daily_goal = std::max<int64_t>(500, std::min<int64_t>(500000, msg.get("goal").as_int(8000)));
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "break_interval") {
            prefs_.break_interval_min = static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(240, msg.get("minutes").as_int(45))));
            break_notified_ = false;
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "dpi_ppi") {
            prefs_.dpi_ppi = static_cast<int>(std::max<int64_t>(72, std::min<int64_t>(320, msg.get("ppi").as_int(96))));
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "auto_backup") {
            prefs_.auto_backup = msg.get("enabled").as_bool(true);
            save_prefs();
            send_view_state();
        } else if (action == "reset_break") {
            engine_.reset_break_timer(now_utc_ms());
            break_notified_ = false;
            send_snapshot();
        } else if (action == "save_focus_session") {
            int dur = static_cast<int>(msg.get("duration_sec").as_int(1500));
            int64_t tot = msg.get("total").as_int(0);
            db_.record_focus_session(now_utc_ms() / 1000 - dur, dur, tot, 0, "Focus");
            send_snapshot();
        } else if (action == "sport") {
            prefs_.sport_enabled = msg.get("enabled").as_bool(false);
            save_prefs();
            send_view_state();
            update_sport_timer();
        } else if (action == "sport_visible") {
            prefs_.sport_visible = msg.get("visible").as_bool(true);
            save_prefs();
            send_view_state();
        } else if (action == "sport_sample") {
            send_sport_sample();
        } else if (action == "session_state") {
            session_state_ = msg.get("state");
            if (session_state_.get("page").as_string() == "share") {
                send_snapshot();
            }
        } else if (action == "history_hours") {
            int64_t req_id = msg.get("id").as_int(0);
            std::string st = msg.get("start").as_string();
            std::string en = msg.get("end").as_string();
            JsonValue rows = db_.query_history_hours(st, en, current_tz_offset_sec());
            webview_.eval_js("window.receiveHistoryHours(" + std::to_string(req_id) + "," + rows.dump() + ");");
        } else if (action == "autostart") {
            bool cur = is_autostart_enabled();
            if (!set_autostart_enabled(!cur)) {
                toast_key("message.autostart_failed");
            }
            send_snapshot();
        } else if (action == "reveal") {
            std::wstring dir = prefs_.custom_db_dir.empty() ? data_dir_ : to_wide(prefs_.custom_db_dir);
            ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else if (action == "storage_directory") {
            std::wstring chosen = pick_folder_dialog(ui_hwnd_, L"Choose TypeCount Data Folder");
            if (!chosen.empty()) {
                engine_.flush();
                std::wstring dst_db = chosen + L"\\typecount.sqlite3";
                if (std::filesystem::exists(std::filesystem::path(dst_db))) {
                    toast_key("storage.destination_exists");
                    return;
                }
                std::string err;
                if (db_.backup_to(to_utf8(dst_db), &err)) {
                    prefs_.custom_db_dir = to_utf8(chosen);
                    save_prefs();
                    db_.open(to_utf8(dst_db));
                    db_mode_ = "custom";
                    send_snapshot();
                } else {
                    toast_key("storage.failed");
                }
            }
        } else if (action == "import_history") {
            std::wstring src = open_sqlite_dialog(ui_hwnd_);
            if (!src.empty()) {
                webview_.eval_js("window.receiveDataBusy(true);");
                auto rep = db_.import_database(to_utf8(src), to_utf8(data_dir_ + L"\\backups"));
                webview_.eval_js("window.receiveDataBusy(false);");
                if (rep.ok) {
                    send_snapshot();
                    if (rep.added_ops > 0) {
                        std::string js = "window.toast(ClickPulseI18n.t('storage.imported',{count:ClickPulseI18n.number(" +
                            std::to_string(rep.added_ops) + "),overlap:ClickPulseI18n.number(" +
                            std::to_string(rep.overlapped_minutes) + "),path:" +
                            JsonValue::escape_string(rep.backup_path) + "}),7000);";
                        webview_.eval_js(js);
                    } else {
                        toast_key("storage.no_new_history");
                    }
                } else {
                    toast_key("storage.failed");
                }
            }
        } else if (action == "export_csv") {
            std::string today = Db::format_day(now_utc_ms() / 1000, current_tz_offset_sec());
            std::wstring def = to_wide("TypeCount-" + today + ".csv");
            std::wstring out = save_file_dialog(ui_hwnd_, L"CSV Files (*.csv)\0*.csv\0", L"csv", def);
            if (!out.empty()) {
                engine_.flush();
                if (db_.export_csv(to_utf8(out), current_tz_offset_sec())) {
                    toast_key("export.saved", to_utf8(out));
                } else {
                    toast_key("export.failed");
                }
            }
        } else if (action == "export_json") {
            std::string today = Db::format_day(now_utc_ms() / 1000, current_tz_offset_sec());
            std::wstring def = to_wide("TypeCount-" + today + ".json");
            std::wstring out = save_file_dialog(ui_hwnd_, L"JSON Files (*.json)\0*.json\0", L"json", def);
            if (!out.empty()) {
                engine_.flush();
                if (db_.export_json(to_utf8(out), current_tz_offset_sec())) {
                    toast_key("export.saved", to_utf8(out));
                } else {
                    toast_key("export.failed");
                }
            }
        } else if (action == "backup_now") {
            engine_.flush();
            std::string created;
            if (db_.create_rotating_backup(to_utf8(data_dir_ + L"\\backups"), 5, &created)) {
                toast_key("export.backup_saved", created);
            } else {
                toast_key("export.failed");
            }
        } else if (action == "save_share_card") {
            std::string data_url = msg.get("png").as_string();
            std::string day = msg.get("day").as_string_or("report");
            std::string period = msg.get("period").as_string_or("");
            std::string fname = "TypeCount-" + (period == "weekly" ? ("Weekly-" + msg.get("start").as_string_or("") + "_") : "") + day + ".png";
            std::wstring out = save_file_dialog(ui_hwnd_, L"PNG Image (*.png)\0*.png\0", L"png", to_wide(fname));
            if (!out.empty()) {
                auto bytes = decode_base64_data_url(data_url);
                if (!bytes.empty() && write_bytes_wide(out, bytes.data(), bytes.size())) {
                    toast_key("share.saved");
                } else {
                    toast_key("share.failed");
                }
            }
        } else if (action == "copy_share_card") {
            auto bytes = decode_base64_data_url(msg.get("png").as_string());
            if (!bytes.empty() && copy_png_to_clipboard(ui_hwnd_, bytes)) {
                toast_key("share.copied");
            } else {
                toast_key("share.copy_failed");
            }
        } else if (action == "project_home" || action == "shortcut_atlas") {
            ShellExecuteW(nullptr, L"open", L"https://github.com/Amirmpm/typeCount", nullptr, nullptr, SW_SHOWNORMAL);
        } else if (action == "send_feedback" || action == "suggest_shortcut") {
            ShellExecuteW(nullptr, L"open", L"https://github.com/Amirmpm/typeCount/issues/new", nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    // ---------- Timers ----------

    void on_flush_timer() {
        engine_.flush();
        int64_t ms = now_utc_ms();
        int tz = current_tz_offset_sec();
        std::string today = Db::format_day(ms / 1000, tz);

        // Auto daily SQLite rotating backup
        if (prefs_.auto_backup && !demo_mode_ && last_backup_day_ != today) {
            db_.create_rotating_backup(to_utf8(data_dir_ + L"\\backups"), 5);
            last_backup_day_ = today;
        }

        // Break reminder notification
        auto live = engine_.get_live_metrics(ms, prefs_.break_interval_min);
        if (live.break_due && !break_notified_) {
            break_notified_ = true;
            if (!prefs_.stealth_mode && tray_added_) {
                bool fa = (prefs_.language == "fa");
                show_tray_balloon(
                    fa ? L"TypeCount · یادآور استراحت" : L"TypeCount · Break Reminder",
                    fa ? L"۲۰ ثانیه به دوردست نگاه کنید یا مچ دست خود را استراحت دهید."
                       : L"Look 20 ft away for 20 seconds or stretch your wrists.");
            }
        } else if (!live.break_due) {
            break_notified_ = false;
        }

        if (ui_visible_ && webview_.is_ready()) {
            send_snapshot();
        }
    }

    // ---------- Win32 WndProcs ----------

    static LRESULT CALLBACK capture_wnd_proc_static(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        TypeCountApp* self = reinterpret_cast<TypeCountApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = reinterpret_cast<TypeCountApp*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->capture_wnd_proc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT capture_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == wm_taskbar_created_ && wm_taskbar_created_ != 0) {
            tray_added_ = false;
            if (!prefs_.stealth_mode) add_tray_icon();
            return 0;
        }
        switch (msg) {
            case WM_INPUT:
                handle_raw_input(reinterpret_cast<HRAWINPUT>(lp));
                return 0;
            case WM_INPUT_DEVICE_CHANGE:
                if (wp == GIDC_REMOVAL) {
                    key_tracker_.remove_device(static_cast<uintptr_t>(lp));
                }
                return 0;
            case WM_HOTKEY:
                if (wp == HOTKEY_WAKE_ID) {
                    show_main_window();
                }
                return 0;
            case WM_APP_ACTIVATE:
                show_main_window();
                return 0;
            case WM_TRAYICON:
                if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK) {
                    show_main_window();
                } else if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
                    show_tray_menu();
                }
                return 0;
            case WM_TIMER:
                if (wp == TIMER_FLUSH) on_flush_timer();
                else if (wp == TIMER_SPORT) send_sport_sample();
                else if (wp == TIMER_DISPOSE_UI) {
                    KillTimer(hwnd, TIMER_DISPOSE_UI);
                    if (!ui_visible_) {
                        webview_.destroy();
                        if (ui_hwnd_) {
                            DestroyWindow(ui_hwnd_);
                            ui_hwnd_ = nullptr;
                        }
                    }
                }
                return 0;
            case WM_QUERYENDSESSION:
                return TRUE;
            case WM_ENDSESSION:
                if (wp) {
                    engine_.flush();
                    save_prefs();
                }
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    static LRESULT CALLBACK ui_wnd_proc_static(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        TypeCountApp* self = reinterpret_cast<TypeCountApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = reinterpret_cast<TypeCountApp*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->ui_wnd_proc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT ui_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_NCCALCSIZE: {
                if (wp == TRUE && IsZoomed(hwnd)) {
                    auto* nc = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO mi{};
                    mi.cbSize = sizeof(mi);
                    if (GetMonitorInfoW(mon, &mi)) {
                        nc->rgrc[0] = mi.rcWork;
                    }
                }
                return 0;
            }
            case WM_NCHITTEST: {
                if (IsZoomed(hwnd) || prefs_.compact) return HTCLIENT;
                POINT pt{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
                RECT rc{};
                GetWindowRect(hwnd, &rc);
                const int b = 7;
                bool left = pt.x < rc.left + b;
                bool right = pt.x >= rc.right - b;
                bool top = pt.y < rc.top + b;
                bool bot = pt.y >= rc.bottom - b;
                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bot && left) return HTBOTTOMLEFT;
                if (bot && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bot) return HTBOTTOM;
                return HTCLIENT;
            }
            case WM_GETMINMAXINFO: {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
                UINT dpi = window_dpi(hwnd);
                if (prefs_.compact) {
                    mmi->ptMinTrackSize.x = MulDiv(320, static_cast<int>(dpi), 96);
                    mmi->ptMinTrackSize.y = MulDiv(208, static_cast<int>(dpi), 96);
                    mmi->ptMaxTrackSize = mmi->ptMinTrackSize;
                } else {
                    mmi->ptMinTrackSize.x = MulDiv(760, static_cast<int>(dpi), 96);
                    mmi->ptMinTrackSize.y = MulDiv(560, static_cast<int>(dpi), 96);
                }
                return 0;
            }
            case WM_SIZE:
                webview_.resize_to_parent();
                if (!prefs_.compact && wp == SIZE_RESTORED) {
                    RECT rc{};
                    GetWindowRect(hwnd, &rc);
                    prefs_.window_width = rc.right - rc.left;
                    prefs_.window_height = rc.bottom - rc.top;
                    prefs_.window_x = rc.left;
                    prefs_.window_y = rc.top;
                }
                if (webview_.is_ready()) send_view_state();
                return 0;
            case WM_MOVE:
                if (!prefs_.compact && !IsZoomed(hwnd) && !IsIconic(hwnd)) {
                    RECT rc{};
                    GetWindowRect(hwnd, &rc);
                    prefs_.window_x = rc.left;
                    prefs_.window_y = rc.top;
                }
                return 0;
            case WM_ACTIVATE:
                if (LOWORD(wp) != WA_INACTIVE) {
                    webview_.focus();
                }
                return 0;
            case WM_CLOSE:
                if (prefs_.close_action == "hide") {
                    hide_main_window(false);
                } else if (prefs_.close_action == "stealth") {
                    hide_main_window(true);
                } else if (prefs_.close_action == "quit") {
                    PostQuitMessage(0);
                } else if (webview_.is_ready()) {
                    webview_.eval_js("window.requestClose();");
                } else {
                    hide_main_window(false);
                }
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
};

} // namespace win
} // namespace tc
