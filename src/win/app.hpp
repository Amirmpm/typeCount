#pragma once

#include "../core/db.hpp"
#include "../core/engine.hpp"
#include "../core/json.hpp"
#include "../core/key_mapper.hpp"
#include "../core/preferences.hpp"
#include "../core/version.hpp"
#include "platform.hpp"
#include "webview_host.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tc {
namespace win {

constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_APP_ACTIVATE = WM_APP + 2;
constexpr UINT WM_APP_IPC = WM_APP + 3;
constexpr UINT WM_APP_REPLACE_FOR_UPGRADE = WM_APP + 4;
constexpr UINT WM_APP_UPDATE_RESULT = WM_APP + 5;

constexpr UINT_PTR TIMER_FLUSH = 101;
constexpr UINT_PTR TIMER_SPORT = 102;
constexpr UINT_PTR TIMER_DISPOSE_UI = 103;
constexpr UINT_PTR TIMER_AUTO_UPDATE = 104;
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
        bool arg_replace = cmd_line.find(L"--replace") != std::wstring::npos;
        demo_mode_ = arg_demo;

        // Single-instance check: automatically replace older running versions (e.g. v1.0.0/v1.0.1)
        // so users downloading/launching v1.1.0 never keep seeing the old version in tray/UI!
        mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\TypeCount_SingleInstance_v1");
        if (mutex_ && GetLastError() == ERROR_ALREADY_EXISTS) {
            bool replaced = false;
            HWND existing = nullptr;
            for (int i = 0; i < 16; ++i) {
                existing = FindWindowW(L"TypeCountCaptureHost", nullptr);
                if (existing) break;
                Sleep(50);
            }
            if (existing) {
                DWORD pid = 0;
                GetWindowThreadProcessId(existing, &pid);
                DWORD tid = GetWindowThreadProcessId(existing, nullptr);
                int running_ver = 0;
                if (pid != 0 && pid != GetCurrentProcessId()) {
                    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
                    if (proc) {
                        wchar_t exe_buf[MAX_PATH]{};
                        DWORD len = MAX_PATH;
                        if (QueryFullProcessImageNameW(proc, 0, exe_buf, &len)) {
                            running_ver = read_file_version_num(exe_buf);
                        }
                        if (arg_replace || (running_ver > 0 && running_ver < TC_VERSION_NUM)) {
                            PostMessageW(existing, WM_APP_REPLACE_FOR_UPGRADE, 0, 0);
                            if (WaitForSingleObject(proc, 500) != WAIT_OBJECT_0) {
                                if (tid) PostThreadMessageW(tid, WM_QUIT, 0, 0);
                                if (WaitForSingleObject(proc, 400) != WAIT_OBJECT_0) {
                                    TerminateProcess(proc, 0);
                                    WaitForSingleObject(proc, 2000);
                                }
                            }
                            CloseHandle(proc);
                            CloseHandle(mutex_);
                            mutex_ = nullptr;
                            Sleep(150);
                            mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\TypeCount_SingleInstance_v1");
                            replaced = true;
                        } else {
                            CloseHandle(proc);
                        }
                    }
                }
                if (!replaced) {
                    if (pid) AllowSetForegroundWindow(pid);
                    PostMessageW(existing, WM_APP_ACTIVATE, 0, 0);
                    CloseHandle(mutex_);
                    mutex_ = nullptr;
                    return 0;
                }
            } else {
                CloseHandle(mutex_);
                mutex_ = nullptr;
                return 0;
            }
        }

        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        data_dir_ = default_data_dir_wide();
        ensure_dir_wide(data_dir_);
        prefs_path_ = data_dir_ + L"\\preferences.json";

        bool prefs_existed = false;
        prefs_ = Preferences::load_file(to_utf8(prefs_path_), &prefs_existed);
        if (!prefs_existed && !arg_no_autostart && !demo_mode_) {
            if (effective_locale() == "fa") prefs_.jalali_calendar = true;
            set_autostart_enabled(true);
            prefs_.save_file(to_utf8(prefs_path_));
        } else if (!demo_mode_) {
            // Keep Windows Run registry path synced to the currently launched binary (post-upgrade/move)
            sync_autostart_path_if_enabled();
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
        if (prefs_.auto_check_updates && !demo_mode_) {
            SetTimer(capture_hwnd_, TIMER_AUTO_UPDATE, 4500, nullptr);
        }

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
    bool ergo_notified_ = false;
    bool idle_paused_ = false;
    bool typing_test_running_ = false;

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
    bool full_maximized_ = false;
    bool prev_compact_ = false;

    std::mutex ipc_mu_;
    std::deque<std::string> ipc_queue_;

    // Update checker state
    std::atomic<bool> update_checking_{false};
    std::mutex update_mu_;
    ReleaseCheckResult pending_update_res_;
    std::string update_status_ = "idle";
    std::string available_version_;
    std::string available_release_url_ = TC_RELEASES_URL;
    std::string available_download_url_;
    bool update_manual_trigger_ = false;

    std::string effective_locale() const {
        return (prefs_.language == "system") ? resolve_system_locale() : prefs_.language;
    }

    void shutdown() {
        KillTimer(capture_hwnd_, TIMER_FLUSH);
        KillTimer(capture_hwnd_, TIMER_SPORT);
        KillTimer(capture_hwnd_, TIMER_DISPOSE_UI);
        KillTimer(capture_hwnd_, TIMER_AUTO_UPDATE);
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
        save_prefs();
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

    void update_dwm_corners() {
        if (!ui_hwnd_) return;
        bool zoomed = (IsZoomed(ui_hwnd_) != FALSE) && !prefs_.compact;
        DWORD pref = zoomed ? 1 /* DWMWCP_DONOTROUND */ : 2 /* DWMWCP_ROUND */;
        HRESULT hr = DwmSetWindowAttribute(ui_hwnd_, static_cast<DWMWINDOWATTRIBUTE>(33), &pref, sizeof(pref));
        if (prefs_.compact && FAILED(hr)) {
            RECT cr{};
            GetClientRect(ui_hwnd_, &cr);
            UINT dpi = window_dpi(ui_hwnd_);
            int diam = MulDiv(24, static_cast<int>(dpi), 96);
            HRGN rgn = CreateRoundRectRgn(0, 0, cr.right + 1, cr.bottom + 1, diam, diam);
            if (rgn && !SetWindowRgn(ui_hwnd_, rgn, TRUE)) {
                DeleteObject(rgn);
            }
        } else {
            SetWindowRgn(ui_hwnd_, nullptr, TRUE);
        }
    }

    void toggle_maximize() {
        if (!ui_hwnd_ || prefs_.compact) return;
        bool next_max = (IsZoomed(ui_hwnd_) == FALSE);
        ShowWindow(ui_hwnd_, next_max ? SW_MAXIMIZE : SW_RESTORE);
        full_maximized_ = (IsZoomed(ui_hwnd_) != FALSE);
        update_dwm_corners();
        webview_.resize_to_parent();
        send_view_state();
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

        POINT ref_pt{std::max(0, prefs_.window_x), std::max(0, prefs_.window_y)};
        HMONITOR mon = MonitorFromPoint(ref_pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);
        int work_w = std::max(800, static_cast<int>(mi.rcWork.right - mi.rcWork.left));
        int work_h = std::max(560, static_cast<int>(mi.rcWork.bottom - mi.rcWork.top));

        // Estimate scale for initial window sizing before HWND exists
        HDC hdc = GetDC(nullptr);
        int init_dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
        if (hdc) ReleaseDC(nullptr, hdc);
        if (init_dpi <= 0) init_dpi = 96;

        int w = prefs_.compact
            ? MulDiv(320, init_dpi, 96)
            : std::min(work_w - 32, std::max(MulDiv(760, init_dpi, 96), MulDiv(prefs_.window_width, init_dpi, 96)));
        int h = prefs_.compact
            ? MulDiv(208, init_dpi, 96)
            : std::min(work_h - 32, std::max(MulDiv(560, init_dpi, 96), MulDiv(prefs_.window_height, init_dpi, 96)));

        int x = prefs_.window_x;
        int y = prefs_.window_y;
        if (x < mi.rcWork.left || x + 120 > mi.rcWork.right || y < mi.rcWork.top || y + 80 > mi.rcWork.bottom) {
            x = mi.rcWork.left + std::max(0, (work_w - w) / 2);
            y = mi.rcWork.top + std::max(0, (work_h - h) / 2);
        }

        DWORD ex_style = prefs_.compact ? WS_EX_TOOLWINDOW : WS_EX_APPWINDOW;
        ui_hwnd_ = CreateWindowExW(
            ex_style,
            L"TypeCountMainWindow",
            L"TypeCount",
            WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
            x, y, w, h,
            nullptr, nullptr, hinst_, this);
        if (!ui_hwnd_) return;

        prev_compact_ = prefs_.compact;
        update_dwm_corners();

        std::wstring wv_dir = data_dir_ + L"\\WebView2";
        ensure_dir_wide(wv_dir);
        std::string init_loc = effective_locale();

        webview_.init(
            ui_hwnd_,
            wv_dir,
            init_loc,
            prefs_.theme,
            [this](const std::string& json_str) { enqueue_ipc_message(json_str); },
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

        ui_visible_ = true;
        if (IsIconic(ui_hwnd_)) ShowWindow(ui_hwnd_, SW_RESTORE);
        apply_window_geometry_for_mode();

        if (!prefs_.compact && full_maximized_) {
            ShowWindow(ui_hwnd_, SW_MAXIMIZE);
        } else {
            ShowWindow(ui_hwnd_, SW_SHOW);
        }
        SetWindowPos(
            ui_hwnd_,
            (prefs_.compact && prefs_.pinned) ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(ui_hwnd_);
        update_dwm_corners();
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
        LONG_PTR ex = GetWindowLongPtrW(ui_hwnd_, GWL_EXSTYLE);
        if (prefs_.compact) {
            if (!prev_compact_) {
                full_maximized_ = (IsZoomed(ui_hwnd_) != FALSE);
                if (full_maximized_) ShowWindow(ui_hwnd_, SW_RESTORE);
                GetWindowRect(ui_hwnd_, &normal_rect_);
                has_normal_rect_ = true;
            }
            SetWindowLongPtrW(ui_hwnd_, GWL_EXSTYLE, (ex & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW);
            int cw = MulDiv(320, static_cast<int>(dpi), 96);
            int ch = MulDiv(208, static_cast<int>(dpi), 96);
            UINT flags = SWP_NOACTIVATE | SWP_FRAMECHANGED;
            int mx = 0, my = 0;
            if (!prev_compact_) {
                HMONITOR mon = MonitorFromWindow(ui_hwnd_, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{};
                mi.cbSize = sizeof(mi);
                GetMonitorInfoW(mon, &mi);
                mx = mi.rcWork.right - cw - MulDiv(24, static_cast<int>(dpi), 96);
                my = mi.rcWork.bottom - ch - MulDiv(24, static_cast<int>(dpi), 96);
            } else {
                flags |= SWP_NOMOVE;
            }
            SetWindowPos(
                ui_hwnd_,
                prefs_.pinned ? HWND_TOPMOST : HWND_NOTOPMOST,
                mx, my, cw, ch,
                flags);
        } else {
            SetWindowLongPtrW(ui_hwnd_, GWL_EXSTYLE, (ex & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW);
            if (prev_compact_) {
                HMONITOR mon = MonitorFromWindow(ui_hwnd_, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{};
                mi.cbSize = sizeof(mi);
                GetMonitorInfoW(mon, &mi);
                int work_w = std::max(800, static_cast<int>(mi.rcWork.right - mi.rcWork.left));
                int work_h = std::max(560, static_cast<int>(mi.rcWork.bottom - mi.rcWork.top));
                int rw = has_normal_rect_
                    ? (normal_rect_.right - normal_rect_.left)
                    : std::min(work_w - 32, MulDiv(prefs_.window_width, static_cast<int>(dpi), 96));
                int rh = has_normal_rect_
                    ? (normal_rect_.bottom - normal_rect_.top)
                    : std::min(work_h - 32, MulDiv(prefs_.window_height, static_cast<int>(dpi), 96));
                int rx = has_normal_rect_ ? normal_rect_.left : (mi.rcWork.left + std::max(0, (work_w - rw) / 2));
                int ry = has_normal_rect_ ? normal_rect_.top : (mi.rcWork.top + std::max(0, (work_h - rh) / 2));
                SetWindowPos(ui_hwnd_, HWND_NOTOPMOST, rx, ry, rw, rh, SWP_NOACTIVATE | SWP_FRAMECHANGED);
                if (full_maximized_) ShowWindow(ui_hwnd_, SW_MAXIMIZE);
            } else {
                SetWindowPos(ui_hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
        }
        prev_compact_ = prefs_.compact;
        update_dwm_corners();
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

    bool apply_wake_hotkey() {
        if (!capture_hwnd_) return false;
        UnregisterHotKey(capture_hwnd_, HOTKEY_WAKE_ID);
        UINT mods = 0, vk = 0;
        if (parse_wake_hotkey(prefs_.wake_hotkey, mods, vk)) {
            return RegisterHotKey(capture_hwnd_, HOTKEY_WAKE_ID, mods, vk) != FALSE;
        }
        return false;
    }

    bool is_excluded_app(const std::string& app_name) const {
        if (app_name.empty()) return false;
        for (const auto& ex : prefs_.excluded_apps) {
            if (_stricmp(app_name.c_str(), ex.c_str()) == 0) return true;
        }
        return false;
    }

    void handle_raw_input(HRAWINPUT hraw) {
        if (paused_) return;
        if (typing_test_running_ && !prefs_.typing_test_count_stats) return;

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
        int tz_off = timezone_offset_sec_for(prefs_.timezone);
        std::string app_name;
        if (prefs_.track_apps || (prefs_.app_exclusions_enabled && !prefs_.excluded_apps.empty())) {
            app_name = foreground_process_name();
            if (prefs_.app_exclusions_enabled && is_excluded_app(app_name)) {
                if (prefs_.pause_in_excluded_apps) return;
                app_name.clear();
            }
            if (!prefs_.track_apps) app_name.clear();
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
        std::wstring tip = (effective_locale() == "fa")
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
        bool fa = (effective_locale() == "fa");
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
                if (ui_visible_ && webview_.is_ready()) { send_view_state(); send_snapshot(); }
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
        send_update_state();
    }

    void send_view_state() {
        if (!webview_.is_ready()) return;
        JsonValue v = prefs_.to_json();
        v.set("app_version", std::string(TC_VERSION_STR));
        v.set("locale", effective_locale());
        v.set("language_preference", prefs_.language);
        v.set("visible", ui_visible_);
        v.set("maximized", ui_hwnd_ ? (IsZoomed(ui_hwnd_) != FALSE) : false);
        v.set("fullscreen", false);
        v.set("auto_check_updates", prefs_.auto_check_updates);
        v.set("last_update_check", prefs_.last_update_check);
        v.set("msi_installed", !find_installed_msi_product_code().empty());
        v.set("registered_uninstall", !find_registered_uninstall_command().empty());

        JsonValue tzs = JsonValue::array();
        for (const auto& z : system_timezones_list()) tzs.push(z);
        v.set("timezones", std::move(tzs));

        webview_.eval_js("window.receiveView(" + v.dump() + ");");
    }

    void send_update_state() {
        if (!webview_.is_ready()) return;
        JsonValue u = JsonValue::object();
        u.set("current_version", std::string(TC_VERSION_STR));
        u.set("automatic", prefs_.auto_check_updates);
        u.set("status", update_status_);
        u.set("checking", update_checking_.load());
        u.set("available_version", available_version_);
        u.set("release_url", available_release_url_);
        u.set("download_url", available_download_url_);
        u.set("last_checked_at", prefs_.last_update_check);
        webview_.eval_js("window.receiveUpdateState(" + u.dump() + ");");
    }

    SnapshotQuery make_query() {
        SnapshotQuery q;
        q.now_ms = now_utc_ms();
        q.tz_offset_sec = timezone_offset_sec_for(prefs_.timezone);
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

    void update_tray_tooltip() {
        if (!tray_added_ || !capture_hwnd_) return;
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = capture_hwnd_;
        nid.uID = 1;
        nid.uFlags = NIF_TIP;
        bool fa = (prefs_.language == "fa");
        std::wstring tip;
        if (paused_) {
            tip = fa ? L"TypeCount · متوقف شده" : L"TypeCount · Paused";
        } else {
            tip = fa ? L"TypeCount · در حال ثبت (کلیک چپ برای بازکردن)" : L"TypeCount · Recording (left-click to open)";
        }
        wcsncpy(nid.szTip, tip.c_str(), 127);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    void toggle_pause() {
        paused_ = !paused_;
        if (paused_) key_tracker_.reset();
        update_tray_tooltip();
        if (webview_.is_ready()) {
            webview_.eval_js(std::string("window.receivePaused(") + (paused_ ? "true" : "false") + ");");
        }
    }

    void enqueue_ipc_message(const std::string& raw) {
        {
            std::lock_guard<std::mutex> lk(ipc_mu_);
            ipc_queue_.push_back(raw);
        }
        if (capture_hwnd_) PostMessageW(capture_hwnd_, WM_APP_IPC, 0, 0);
    }

    void drain_ipc_queue() {
        for (;;) {
            std::string raw;
            {
                std::lock_guard<std::mutex> lk(ipc_mu_);
                if (ipc_queue_.empty()) break;
                raw = std::move(ipc_queue_.front());
                ipc_queue_.pop_front();
            }
            handle_ipc_message(raw);
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
        int idle_s = prefs_.idle_detection_enabled ? prefs_.idle_timeout_sec : 180;
        auto live = engine_.get_live_metrics(now_utc_ms(), prefs_.break_interval_min, idle_s);
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

    void start_update_check(bool manual) {
        if (update_checking_.exchange(true)) return;
        update_manual_trigger_ = manual;
        update_status_ = "checking";
        send_update_state();
        std::thread([this]() {
            ReleaseCheckResult res = fetch_latest_github_release();
            {
                std::lock_guard<std::mutex> lk(update_mu_);
                pending_update_res_ = std::move(res);
            }
            if (capture_hwnd_) {
                PostMessageW(capture_hwnd_, WM_APP_UPDATE_RESULT, 0, 0);
            } else {
                update_checking_.store(false);
            }
        }).detach();
    }

    void on_update_result() {
        ReleaseCheckResult res;
        {
            std::lock_guard<std::mutex> lk(update_mu_);
            res = pending_update_res_;
        }
        update_checking_.store(false);
        prefs_.last_update_check = now_utc_ms() / 1000;
        save_prefs();

        if (!res.ok) {
            update_status_ = res.rate_limited ? "rate_limited" : "error";
            send_update_state();
            if (update_manual_trigger_) {
                toast_key(res.rate_limited ? "update.rate_limited" : "update.failed");
            }
            return;
        }

        available_release_url_ = res.release_url;
        available_download_url_ = res.download_url;
        int cmp = compare_semver(TC_VERSION_STR, res.latest_version);
        if (cmp < 0) {
            available_version_ = res.latest_version;
            update_status_ = "available";
            send_update_state();
            if (webview_.is_ready()) {
                JsonValue u = JsonValue::object();
                u.set("current_version", std::string(TC_VERSION_STR));
                u.set("version", res.latest_version);
                u.set("release_url", res.release_url);
                u.set("download_url", res.download_url);
                webview_.eval_js("window.receiveUpdateAvailable(" + u.dump() + ");");
            }
            if (!prefs_.stealth_mode && tray_added_) {
                bool fa = (effective_locale() == "fa");
                show_tray_balloon(
                    fa ? L"TypeCount · نسخه جدید آماده است" : L"TypeCount · Update Available",
                    (fa ? L"نسخه v" : L"Version v") + to_wide(res.latest_version) +
                        (fa ? L" در دسترس است. برای دریافت به تنظیمات بروید." : L" is available in Settings."));
            }
        } else {
            available_version_.clear();
            update_status_ = "up_to_date";
            send_update_state();
            if (update_manual_trigger_) {
                toast_key("update.up_to_date");
            }
        }
    }

    void launch_uninstall() {
        // 1. Installed via Windows Installer (.msi) -> launch msiexec /x {ProductCode}
        std::wstring msi_code = find_installed_msi_product_code();
        if (!msi_code.empty()) {
            std::wstring args = L"/x " + msi_code;
            ShellExecuteW(ui_hwnd_, L"open", L"msiexec.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
            toast_key("removal.running");
            PostQuitMessage(0);
            return;
        }
        // 2. Installed via TypeCount-Setup-x64.exe -> launch registered UninstallString
        std::wstring cmd = find_registered_uninstall_command();
        if (!cmd.empty()) {
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            std::wstring mutable_cmd = cmd;
            if (CreateProcessW(nullptr, &mutable_cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                toast_key("removal.running");
                PostQuitMessage(0);
                return;
            }
        }
        // 3. Fallback: disable autostart and open data/app folder so user can delete portable binary
        set_autostart_enabled(false);
        toast_key("removal.portable_hint");
        ShellExecuteW(nullptr, L"open", exe_dir_wide().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void handle_ipc_message(const std::string& raw) {
        JsonValue msg;
        if (!JsonValue::parse(raw, msg) || !msg.is_object()) return;
        const std::string action = msg.get("action").as_string();

        if (action == "ready") {
            send_view_state();
            send_snapshot();
            send_update_state();
        } else if (action == "pause") {
            toggle_pause();
        } else if (action == "drag") {
            if (ui_hwnd_ && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                POINT pt{};
                GetCursorPos(&pt);
                ReleaseCapture();
                SendMessageW(ui_hwnd_, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
            }
        } else if (action == "minimize") {
            if (ui_hwnd_) ShowWindow(ui_hwnd_, SW_MINIMIZE);
        } else if (action == "maximize") {
            toggle_maximize();
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
            std::string lang = msg.get("language").as_string_or("en");
            prefs_.language = Preferences::is_valid_language(lang) ? lang : "en";
            if (!demo_mode_ && db_.get_meta("jalali_user_set").empty()) {
                prefs_.jalali_calendar = (effective_locale() == "fa");
            }
            save_prefs();
            update_tray_tooltip();
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
            std::string prev = prefs_.wake_hotkey;
            prefs_.wake_hotkey = msg.get("hotkey").as_string_or("Ctrl+Alt+Shift+T");
            if (apply_wake_hotkey()) {
                save_prefs();
                toast_key("message.saved");
            } else {
                prefs_.wake_hotkey = prev;
                apply_wake_hotkey();
                toast_key("message.hotkey_failed");
            }
            send_view_state();
        } else if (action == "daily_goal") {
            prefs_.daily_goal = std::max<int64_t>(500, std::min<int64_t>(500000, msg.get("goal").as_int(8000)));
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "break_interval") {
            prefs_.break_interval_min = static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(240, msg.get("minutes").as_int(45))));
            break_notified_ = false;
            ergo_notified_ = false;
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
            ergo_notified_ = false;
            send_snapshot();
        } else if (action == "save_focus_session") {
            int dur = static_cast<int>(msg.get("duration_sec").as_int(1500));
            int64_t tot = msg.get("total").as_int(0);
            db_.record_focus_session(now_utc_ms() / 1000 - dur, dur, tot, 0, "Focus");
            if (prefs_.sound_alerts_enabled) {
                play_gentle_chime_async();
            }
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
        } else if (action == "typing_test_enabled") {
            prefs_.typing_test_enabled = msg.get("enabled").as_bool(true);
            save_prefs();
            send_view_state();
        } else if (action == "typing_test_count_stats") {
            prefs_.typing_test_count_stats = msg.get("enabled").as_bool(false);
            save_prefs();
            send_view_state();
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
                db_.record_typing_test(now_utc_ms() / 1000, dur, wpm, raw_cpm, acc, errors, chars, lang);
            }
            typing_test_running_ = false;
            send_snapshot();
        } else if (action == "ergo_insights_enabled") {
            prefs_.ergo_insights_enabled = msg.get("enabled").as_bool(true);
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "ergo_guard_enabled") {
            prefs_.ergo_guard_enabled = msg.get("enabled").as_bool(true);
            ergo_notified_ = false;
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "smart_alerts_enabled") {
            prefs_.smart_alerts_enabled = msg.get("enabled").as_bool(true);
            save_prefs();
            send_view_state();
        } else if (action == "sound_alerts_enabled") {
            prefs_.sound_alerts_enabled = msg.get("enabled").as_bool(true);
            save_prefs();
            if (prefs_.sound_alerts_enabled) play_gentle_chime_async();
            send_view_state();
        } else if (action == "app_exclusions_enabled") {
            prefs_.app_exclusions_enabled = msg.get("enabled").as_bool(true);
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "pause_in_excluded_apps") {
            prefs_.pause_in_excluded_apps = msg.get("enabled").as_bool(false);
            save_prefs();
            send_view_state();
        } else if (action == "exclude_app") {
            std::string app = msg.get("app").as_string();
            if (!app.empty() && !is_excluded_app(app)) {
                prefs_.excluded_apps.push_back(app);
                save_prefs();
                toast_key("apps.excluded_toast");
            }
            send_view_state();
            send_snapshot();
        } else if (action == "unexclude_app") {
            std::string app = msg.get("app").as_string();
            if (!app.empty()) {
                prefs_.excluded_apps.erase(
                    std::remove_if(prefs_.excluded_apps.begin(), prefs_.excluded_apps.end(),
                        [&](const std::string& x) { return _stricmp(x.c_str(), app.c_str()) == 0; }),
                    prefs_.excluded_apps.end());
                save_prefs();
            }
            send_view_state();
            send_snapshot();
        } else if (action == "idle_detection_enabled") {
            prefs_.idle_detection_enabled = msg.get("enabled").as_bool(true);
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "idle_timeout_sec") {
            prefs_.idle_timeout_sec = static_cast<int>(std::max<int64_t>(30, std::min<int64_t>(900, msg.get("seconds").as_int(90))));
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "jalali_calendar") {
            prefs_.jalali_calendar = msg.get("enabled").as_bool(true);
            if (!demo_mode_) db_.set_meta("jalali_user_set", "1");
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "automatic_updates") {
            prefs_.auto_check_updates = msg.get("enabled").as_bool(true);
            save_prefs();
            send_view_state();
            send_update_state();
            if (prefs_.auto_check_updates && !demo_mode_) {
                start_update_check(false);
            }
        } else if (action == "check_updates") {
            start_update_check(true);
        } else if (action == "update_release" || action == "install_update") {
            std::string url = (action == "install_update" && !available_download_url_.empty())
                ? available_download_url_
                : available_release_url_;
            if (url.empty()) url = TC_RELEASES_URL;
            ShellExecuteW(nullptr, L"open", to_wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else if (action == "session_state") {
            session_state_ = msg.get("state");
            if (session_state_.get("page").as_string() == "share") {
                send_snapshot();
            }
        } else if (action == "history_hours") {
            int64_t req_id = msg.get("id").as_int(0);
            std::string st = msg.get("start").as_string();
            std::string en = msg.get("end").as_string();
            int tz = timezone_offset_sec_for(prefs_.timezone);
            JsonValue rows = demo_mode_
                ? db_.query_history_hours_demo(st, en)
                : db_.query_history_hours(st, en, tz);
            webview_.eval_js("window.receiveHistoryHours(" + std::to_string(req_id) + "," + rows.dump() + ");");
        } else if (action == "autostart") {
            bool cur = is_autostart_enabled();
            if (!set_autostart_enabled(!cur)) {
                toast_key("message.autostart_failed");
            } else {
                toast_key(!cur ? "message.autostart_enabled" : "message.autostart_disabled");
            }
            send_view_state();
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
                    // Open existing database cleanly instead of failing silently
                    prefs_.custom_db_dir = to_utf8(chosen);
                    save_prefs();
                    db_.open(to_utf8(dst_db));
                    db_mode_ = "custom";
                    toast_key("storage.opened_existing");
                    send_view_state();
                    send_snapshot();
                    return;
                }
                std::string err;
                if (db_.backup_to(to_utf8(dst_db), &err)) {
                    prefs_.custom_db_dir = to_utf8(chosen);
                    save_prefs();
                    db_.open(to_utf8(dst_db));
                    db_mode_ = "custom";
                    toast_key("storage.moved");
                    send_view_state();
                    send_snapshot();
                } else {
                    toast_key("storage.failed");
                }
            }
        } else if (action == "reset_storage_directory") {
            if (!prefs_.custom_db_dir.empty()) {
                engine_.flush();
                std::wstring def_db = data_dir_ + L"\\typecount.sqlite3";
                prefs_.custom_db_dir.clear();
                save_prefs();
                db_.open(to_utf8(def_db));
                db_mode_ = "default";
                toast_key("storage.reset_done");
                send_view_state();
                send_snapshot();
            }
        } else if (action == "uninstall") {
            launch_uninstall();
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
            std::string today = Db::format_day(now_utc_ms() / 1000, timezone_offset_sec_for(prefs_.timezone));
            std::wstring def = to_wide("TypeCount-" + today + ".csv");
            std::wstring out = save_file_dialog(ui_hwnd_, L"CSV Files (*.csv)\0*.csv\0", L"csv", def);
            if (!out.empty()) {
                engine_.flush();
                if (db_.export_csv(to_utf8(out), timezone_offset_sec_for(prefs_.timezone))) {
                    toast_key("export.saved", to_utf8(out));
                } else {
                    toast_key("export.failed");
                }
            }
        } else if (action == "export_json") {
            std::string today = Db::format_day(now_utc_ms() / 1000, timezone_offset_sec_for(prefs_.timezone));
            std::wstring def = to_wide("TypeCount-" + today + ".json");
            std::wstring out = save_file_dialog(ui_hwnd_, L"JSON Files (*.json)\0*.json\0", L"json", def);
            if (!out.empty()) {
                engine_.flush();
                if (db_.export_json(to_utf8(out), timezone_offset_sec_for(prefs_.timezone))) {
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
        int tz = timezone_offset_sec_for(prefs_.timezone);
        std::string today = Db::format_day(ms / 1000, tz);
        bool fa = (effective_locale() == "fa");

        // Auto daily SQLite rotating backup
        if (prefs_.auto_backup && !demo_mode_ && last_backup_day_ != today) {
            db_.create_rotating_backup(to_utf8(data_dir_ + L"\\backups"), 5);
            last_backup_day_ = today;
        }

        int idle_s = prefs_.idle_detection_enabled ? prefs_.idle_timeout_sec : 180;
        auto live = engine_.get_live_metrics(ms, prefs_.break_interval_min, idle_s);

        // Break reminder notification + optional gentle chime
        if (live.break_due && !break_notified_) {
            break_notified_ = true;
            if (prefs_.sound_alerts_enabled) {
                play_gentle_chime_async();
            }
            if (!prefs_.stealth_mode && tray_added_) {
                show_tray_balloon(
                    fa ? L"TypeCount · یادآور استراحت" : L"TypeCount · Break Reminder",
                    fa ? L"۲۰ ثانیه به دوردست نگاه کنید یا مچ دست خود را استراحت دهید."
                       : L"Look 20 ft away for 20 seconds or stretch your wrists.");
            }
        } else if (!live.break_due) {
            break_notified_ = false;
        }

        // Ergonomic RSI strain guard alert
        if (prefs_.ergo_guard_enabled && live.ergo_strain_level >= 2 && !ergo_notified_) {
            ergo_notified_ = true;
            if (prefs_.sound_alerts_enabled) play_gentle_chime_async();
            if (!prefs_.stealth_mode && tray_added_) {
                show_tray_balloon(
                    fa ? L"TypeCount · هشدار ارگونومی (RSI)" : L"TypeCount · Ergonomic Strain Guard",
                    fa ? L"فشار تایپ/کلیک در ۱۰ دقیقه اخیر بالا بوده است؛ چند لحظه دست‌ها را استراحت دهید."
                       : L"High 10-minute typing/click intensity detected. Take a 30-second wrist stretch.");
            }
            if (ui_visible_ && webview_.is_ready()) toast_key("ergo.strain_alert");
        } else if (live.ergo_strain_level < 2) {
            ergo_notified_ = false;
        }

        // Smart milestones (Daily Goal & New Peak WPM) evaluated cleanly via snapshot
        if (prefs_.smart_alerts_enabled && !demo_mode_) {
            JsonValue snap = engine_.snapshot(make_query());
            int64_t today_total = snap.get("today").get("keyboard").as_int(0) + snap.get("today").get("mouse").as_int(0);
            if (prefs_.daily_goal > 0 && today_total >= prefs_.daily_goal && db_.get_meta("goal_notified_day") != today) {
                db_.set_meta("goal_notified_day", today);
                if (prefs_.sound_alerts_enabled) play_gentle_chime_async();
                if (!prefs_.stealth_mode && tray_added_) {
                    show_tray_balloon(
                        fa ? L"TypeCount · هدف روزانه کامل شد!" : L"TypeCount · Daily Goal Reached!",
                        fa ? L"آفرین! به هدف روزانه فعالیت خود رسیدید." : L"Great work! You reached your daily activity target.");
                }
                if (ui_visible_ && webview_.is_ready()) toast_key("smart.goal_reached");
            }
            double peak_wpm = snap.get("today_peak_wpm").as_double(0.0);
            double prev_best = 0.0;
            std::string prev_best_s = db_.get_meta("best_wpm_record");
            if (!prev_best_s.empty()) {
                try { prev_best = std::stod(prev_best_s); } catch (...) {}
            }
            if (peak_wpm >= 35.0 && (prev_best <= 0.0 || peak_wpm >= prev_best + 2.0)) {
                db_.set_meta("best_wpm_record", std::to_string(static_cast<int>(std::round(peak_wpm))));
                if (prev_best > 0.0) {
                    if (prefs_.sound_alerts_enabled) play_gentle_chime_async();
                    if (!prefs_.stealth_mode && tray_added_) {
                        show_tray_balloon(
                            fa ? L"TypeCount · رکورد جدید سرعت تایپ!" : L"TypeCount · New Peak Speed Record!",
                            (fa ? L"رکورد جدید سرعت تایپ: " : L"New peak typing speed: ") +
                                std::to_wstring(static_cast<int>(std::round(peak_wpm))) + L" WPM");
                    }
                    if (ui_visible_ && webview_.is_ready()) toast_key("smart.new_record");
                }
            }
            if (ui_visible_ && webview_.is_ready()) {
                if (prefs_.compact) {
                    webview_.eval_js("window.receiveMini(" + snap.dump() + ");");
                } else {
                    webview_.eval_js("window.receive(" + snap.dump() + ");");
                }
                return;
            }
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
            case WM_APP_REPLACE_FOR_UPGRADE:
                engine_.flush();
                save_prefs();
                PostQuitMessage(0);
                return 0;
            case WM_APP_IPC:
                drain_ipc_queue();
                return 0;
            case WM_APP_UPDATE_RESULT:
                on_update_result();
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
                else if (wp == TIMER_AUTO_UPDATE) {
                    KillTimer(hwnd, TIMER_AUTO_UPDATE);
                    if (prefs_.auto_check_updates && !demo_mode_) {
                        int64_t now_s = now_utc_ms() / 1000;
                        if (prefs_.last_update_check <= 0 || (now_s - prefs_.last_update_check) >= 21600) {
                            start_update_check(false);
                        }
                    }
                }
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
                HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{};
                mi.cbSize = sizeof(mi);
                if (GetMonitorInfoW(mon, &mi)) {
                    mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                    mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                    mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                    mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
                }
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
            case WM_SYSCOMMAND:
                if ((wp & 0xFFF0) == SC_KEYMENU && lp == 0) {
                    return 0;
                }
                break;
            case WM_NCLBUTTONDBLCLK:
                if (wp == HTCAPTION && !prefs_.compact) {
                    toggle_maximize();
                    return 0;
                }
                break;
            case WM_SIZE: {
                if (wp == SIZE_MINIMIZED) {
                    webview_.set_visible(false);
                } else {
                    if (ui_visible_) webview_.set_visible(true);
                    webview_.resize_to_parent();
                    update_dwm_corners();
                    if (!prefs_.compact) {
                        full_maximized_ = (wp == SIZE_MAXIMIZED);
                        if (wp == SIZE_RESTORED) {
                            RECT rc{};
                            GetWindowRect(hwnd, &rc);
                            UINT dpi = window_dpi(hwnd);
                            prefs_.window_width = MulDiv(rc.right - rc.left, 96, static_cast<int>(dpi));
                            prefs_.window_height = MulDiv(rc.bottom - rc.top, 96, static_cast<int>(dpi));
                            prefs_.window_x = rc.left;
                            prefs_.window_y = rc.top;
                        }
                    }
                    if (webview_.is_ready()) send_view_state();
                }
                return 0;
            }
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
