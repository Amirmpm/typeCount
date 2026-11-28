#pragma once

#include "platform.hpp"
#include "input_capture.hpp"

#include "../core/db.hpp"
#include "../core/engine.hpp"
#include "../core/json.hpp"
#include "../core/posix_host_util.hpp"
#include "../core/preferences.hpp"
#include "../core/sound_synth.hpp"
#include "../core/version.hpp"
#include "../win/ui_bundle.hpp"

#include <webkit2/webkit2.h>
#include <jsc/jsc.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tc::linux_host {

class TypeCountApp {
public:
    TypeCountApp() : engine_(db_) {}

    int run(int argc, char** argv) {
        bool start_hidden = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg(argv[i]);
            if (arg == "--background" || arg == "--stealth") start_hidden = true;
            else if (arg == "--demo") demo_mode_ = true;
        }

        // Single-instance lock
        lock_fd_ = open(lock_file_path().c_str(), O_CREAT | O_RDWR, 0644);
        if (lock_fd_ >= 0 && flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
            if (!start_hidden) signal_existing_instance();
            close(lock_fd_);
            return 0;
        }

        default_dir_ = default_data_dir();
        data_dir_ = resolve_active_data_dir();
        prefs_path_ = (std::filesystem::path(default_dir_) / "preferences.json").string();
        if (!std::filesystem::exists(prefs_path_)) {
            std::string alt_prefs = (std::filesystem::path(data_dir_) / "preferences.json").string();
            if (std::filesystem::exists(alt_prefs)) prefs_path_ = alt_prefs;
        }

        bool loaded = false;
        prefs_ = tc::Preferences::load_file(prefs_path_, &loaded);
        if (!loaded) prefs_.save_file(prefs_path_);

        std::string db_dir = prefs_.custom_db_dir.empty() ? data_dir_ : prefs_.custom_db_dir;
        std::error_code ec;
        std::filesystem::create_directories(db_dir, ec);
        db_path_ = (std::filesystem::path(db_dir) / "typecount.sqlite3").string();
        db_mode_ = prefs_.custom_db_dir.empty() ? "default" : "custom";

        if (!db_.open(db_path_)) return 1;

        if (!demo_mode_ && db_.get_meta("jalali_user_set").empty()) {
            prefs_.jalali_calendar = (effective_locale() == "fa");
        }
        engine_.set_idle_timeout_sec(prefs_.idle_detection_enabled ? prefs_.idle_timeout_sec : 180);
        if (prefs_.autostart) set_autostart_enabled(true);

        gtk_init(&argc, &argv);

        system_dark_ = detect_os_dark_mode();
        create_main_window();

        detected_dpi_ = detect_monitor_ppi(window_);
        if (prefs_.dpi_auto && detected_dpi_ != prefs_.dpi_ppi) {
            prefs_.dpi_ppi = detected_dpi_;
            prefs_.save_file(prefs_path_);
        }

        setup_webview();
        apply_gtk_color_scheme();
        setup_tray();
        start_ipc_socket();

        if (!start_hidden && !prefs_.stealth_mode && prefs_.startup_mode != "stealth") {
            gtk_widget_show_all(window_);
            window_visible_ = true;
        } else {
            window_visible_ = false;
            if (prefs_.startup_mode == "stealth" || prefs_.stealth_mode) {
                prefs_.stealth_mode = true;
                set_tray_visible(false);
            }
        }

        start_input_capture();

        g_timeout_add(1000, on_tick_1s, this);
        sport_timer_id_ = 0;
        update_sport_timer();

        if (prefs_.auto_check_updates && !demo_mode_) {
            g_timeout_add(6000, +[](gpointer ud) -> gboolean {
                auto* self = static_cast<TypeCountApp*>(ud);
                int64_t now_s = tc::posix_util::now_utc_ms() / 1000;
                if (self->prefs_.last_update_check <= 0 || (now_s - self->prefs_.last_update_check) >= 21600) {
                    self->start_update_check(false);
                }
                return G_SOURCE_REMOVE;
            }, this);
        }

        gtk_main();

        stop_ipc_socket();
        capture_.stop();
        engine_.flush();
        db_.close();
        if (lock_fd_ >= 0) {
            flock(lock_fd_, LOCK_UN);
            close(lock_fd_);
        }
        return 0;
    }

private:
    std::string effective_locale() const {
        if (prefs_.language == "system" || prefs_.language.empty()) return detect_system_locale();
        return prefs_.language;
    }

    bool effective_dark() const {
        if (prefs_.color_mode == "dark") return true;
        if (prefs_.color_mode == "light") return false;
        return system_dark_;
    }

    void save_prefs() {
        prefs_.save_file(prefs_path_);
    }

    void apply_gtk_color_scheme() {
        bool dark = effective_dark();
        if (GtkSettings* st = gtk_settings_get_default()) {
            g_object_set(st, "gtk-application-prefer-dark-theme", dark ? TRUE : FALSE, nullptr);
        }
        if (webview_) {
            GdkRGBA bg = dark ? GdkRGBA{0.0706, 0.0824, 0.1059, 1.0}
                              : GdkRGBA{0.9608, 0.9686, 0.9804, 1.0};
            webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(webview_), &bg);
        }
    }

    GdkPixbuf* load_embedded_icon() {
        const auto* asset = tc::find_ui_asset("typecount-mark.png");
        if (!asset) return nullptr;
        GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
        if (!loader) return nullptr;
        gdk_pixbuf_loader_write(loader, asset->data, asset->size, nullptr);
        gdk_pixbuf_loader_close(loader, nullptr);
        GdkPixbuf* pb = gdk_pixbuf_loader_get_pixbuf(loader);
        if (pb) g_object_ref(pb);
        g_object_unref(loader);
        return pb;
    }

    void create_main_window() {
        window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(window_), "TypeCount");
        gtk_window_set_default_size(GTK_WINDOW(window_), 1040, 720);
        gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER);
        gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);

        GdkGeometry geom{};
        geom.min_width = 300;
        geom.min_height = 68;
        gtk_window_set_geometry_hints(GTK_WINDOW(window_), nullptr, &geom, GDK_HINT_MIN_SIZE);

        if (GdkPixbuf* icon = load_embedded_icon()) {
            gtk_window_set_icon(GTK_WINDOW(window_), icon);
            g_object_unref(icon);
        }

        g_signal_connect(window_, "delete-event", G_CALLBACK(on_delete_event), this);
        g_signal_connect(window_, "window-state-event", G_CALLBACK(on_window_state), this);
        g_signal_connect(window_, "configure-event", G_CALLBACK(on_configure_event), this);

        if (GtkSettings* st = gtk_settings_get_default()) {
            g_signal_connect(st, "notify::gtk-theme-name", G_CALLBACK(on_gtk_theme_changed), this);
        }
    }

    void setup_webview() {
        WebKitWebContext* ctx = webkit_web_context_get_default();
        webkit_web_context_register_uri_scheme(ctx, "typecount", on_uri_scheme, this, nullptr);

        WebKitUserContentManager* ucm = webkit_user_content_manager_new();
        webkit_user_content_manager_register_script_message_handler(ucm, "ipc");
        g_signal_connect(ucm, "script-message-received::ipc", G_CALLBACK(on_ipc_received), this);

        std::string loc = effective_locale();
        std::string boot =
            "window.clickpulsePlatform='linux';"
            "window.typeCountVersion=" + tc::JsonValue::escape_string(TC_VERSION_STR) + ";"
            "window.clickpulseLocale=" + tc::JsonValue::escape_string(loc) + ";"
            "window.clickpulseTheme=" + tc::JsonValue::escape_string(prefs_.theme) + ";"
            "window.clickpulseColorMode=" + tc::JsonValue::escape_string(prefs_.color_mode) + ";"
            "window.clickpulseSystemDark=" + std::string(system_dark_ ? "true" : "false") + ";";

        WebKitUserScript* script = webkit_user_script_new(
            boot.c_str(),
            WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
            nullptr, nullptr);
        webkit_user_content_manager_add_script(ucm, script);
        webkit_user_script_unref(script);

        webview_ = webkit_web_view_new_with_user_content_manager(ucm);
        g_object_unref(ucm);

        WebKitSettings* ws = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(webview_));
        webkit_settings_set_enable_javascript(ws, TRUE);
        webkit_settings_set_enable_developer_extras(ws, FALSE);

        g_signal_connect(webview_, "load-changed", G_CALLBACK(on_load_changed), this);
        g_signal_connect(webview_, "context-menu", G_CALLBACK(+[](WebKitWebView*, WebKitContextMenu*, GdkEvent*, WebKitHitTestResult*, gpointer) -> gboolean {
            return TRUE;
        }), nullptr);

        gtk_container_add(GTK_CONTAINER(window_), webview_);
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(webview_), "typecount://app/index.html");
    }

    void set_tray_visible(bool vis) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        if (tray_icon_) gtk_status_icon_set_visible(tray_icon_, vis ? TRUE : FALSE);
#pragma GCC diagnostic pop
    }

    void setup_tray() {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        GdkPixbuf* pb = load_embedded_icon();
        if (pb) {
            tray_icon_ = gtk_status_icon_new_from_pixbuf(pb);
            g_object_unref(pb);
        } else {
            tray_icon_ = gtk_status_icon_new_from_icon_name("input-keyboard");
        }
        if (tray_icon_) {
            gtk_status_icon_set_tooltip_text(tray_icon_, "TypeCount");
            gtk_status_icon_set_visible(tray_icon_, prefs_.stealth_mode ? FALSE : TRUE);
            g_signal_connect(tray_icon_, "activate", G_CALLBACK(+[](GtkStatusIcon*, gpointer ud) {
                static_cast<TypeCountApp*>(ud)->toggle_or_show_window();
            }), this);
            g_signal_connect(tray_icon_, "popup-menu", G_CALLBACK(+[](GtkStatusIcon*, guint button, guint32 act_time, gpointer ud) {
                static_cast<TypeCountApp*>(ud)->show_tray_menu(button, act_time);
            }), this);
        }
#pragma GCC diagnostic pop
    }

    void show_tray_menu(guint button, guint32 act_time) {
        GtkWidget* menu = gtk_menu_new();
        bool fa = (effective_locale() == "fa");

        GtkWidget* item_open = gtk_menu_item_new_with_label(fa ? "باز کردن TypeCount" : "Open TypeCount");
        GtkWidget* item_mini = gtk_menu_item_new_with_label(fa ? "حالت کوچک (Mini)" : "Mini mode");
        GtkWidget* item_stealth = gtk_menu_item_new_with_label(fa ? "حالت مخفی کامل (بدون آیکون)" : "Stealth mode (hide tray icon)");
        GtkWidget* item_sep1 = gtk_separator_menu_item_new();
        GtkWidget* item_pause = gtk_menu_item_new_with_label(
            paused_ ? (fa ? "ادامه ثبت" : "Resume recording") : (fa ? "توقف موقت ثبت" : "Pause recording"));
        GtkWidget* item_sep2 = gtk_separator_menu_item_new();
        GtkWidget* item_quit = gtk_menu_item_new_with_label(fa ? "ذخیره و خروج" : "Save & Quit");

        g_signal_connect(item_open, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer ud) {
            auto* self = static_cast<TypeCountApp*>(ud);
            self->prefs_.compact = false;
            self->save_prefs();
            self->apply_window_geometry_for_mode();
            self->show_main_window();
        }), this);
        g_signal_connect(item_mini, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer ud) {
            auto* self = static_cast<TypeCountApp*>(ud);
            self->prefs_.compact = !self->prefs_.compact;
            self->save_prefs();
            self->apply_window_geometry_for_mode();
            self->show_main_window();
        }), this);
        g_signal_connect(item_stealth, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer ud) {
            static_cast<TypeCountApp*>(ud)->hide_main_window(true);
        }), this);
        g_signal_connect(item_pause, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer ud) {
            static_cast<TypeCountApp*>(ud)->toggle_pause();
        }), this);
        g_signal_connect(item_quit, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer ud) {
            static_cast<TypeCountApp*>(ud)->quit_app();
        }), this);

        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_open);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_mini);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_stealth);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_sep1);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_pause);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_sep2);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_quit);
        gtk_widget_show_all(menu);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        gtk_menu_popup(GTK_MENU(menu), nullptr, nullptr, gtk_status_icon_position_menu, tray_icon_, button, act_time);
#pragma GCC diagnostic pop
    }

    void start_input_capture() {
        InputCallbacks cb;
        cb.on_key = [this](uint32_t code, uint16_t vk, const std::optional<tc::ShortcutChord>& chord) {
            if (paused_ || demo_mode_) return;
            if (typing_test_running_ && !prefs_.typing_test_count_stats) return;
            std::string app = prefs_.track_apps ? active_application_name() : "";
            if (tc::posix_util::is_excluded_app(prefs_, app)) {
                if (prefs_.pause_in_excluded_apps) return;
                app.clear();
            }
            bool is_mod = (tc::modifier_vk(code) != 0);
            int64_t ms = tc::posix_util::now_utc_ms();
            int tz = tc::posix_util::timezone_offset_sec_for(prefs_.timezone);
            engine_.on_key_press(ms, tz, code, is_mod, chord, app, prefs_.track_apps);
            (void)vk;
        };
        cb.on_mouse_click = [this](int btn) {
            if (paused_ || demo_mode_) return;
            std::string app = prefs_.track_apps ? active_application_name() : "";
            if (tc::posix_util::is_excluded_app(prefs_, app)) {
                if (prefs_.pause_in_excluded_apps) return;
                app.clear();
            }
            int64_t ms = tc::posix_util::now_utc_ms();
            int tz = tc::posix_util::timezone_offset_sec_for(prefs_.timezone);
            engine_.on_mouse_click(ms, tz, static_cast<uint32_t>(btn), app, prefs_.track_apps);
        };
        cb.on_mouse_scroll = [this](int delta) {
            if (paused_ || demo_mode_) return;
            std::string app = prefs_.track_apps ? active_application_name() : "";
            if (tc::posix_util::is_excluded_app(prefs_, app)) {
                if (prefs_.pause_in_excluded_apps) return;
                app.clear();
            }
            int64_t ms = tc::posix_util::now_utc_ms();
            int tz = tc::posix_util::timezone_offset_sec_for(prefs_.timezone);
            engine_.on_mouse_scroll(ms, tz, delta, app, prefs_.track_apps);
        };
        cb.on_mouse_move = [this](double px) {
            if (paused_ || demo_mode_) return;
            std::string app = prefs_.track_apps ? active_application_name() : "";
            if (tc::posix_util::is_excluded_app(prefs_, app)) {
                if (prefs_.pause_in_excluded_apps) return;
                app.clear();
            }
            int64_t dist = static_cast<int64_t>(std::llround(px));
            if (dist <= 0) return;
            int64_t ms = tc::posix_util::now_utc_ms();
            int tz = tc::posix_util::timezone_offset_sec_for(prefs_.timezone);
            engine_.on_mouse_move(ms, tz, dist, app, prefs_.track_apps);
        };
        cb.on_wake_hotkey = [this]() {
            g_idle_add(+[](gpointer ud) -> gboolean {
                auto* self = static_cast<TypeCountApp*>(ud);
                self->show_main_window();
                return G_SOURCE_REMOVE;
            }, this);
        };
        capture_.start(std::move(cb), prefs_.wake_hotkey);
        capture_status_.permission_needed = capture_.permission_needed();
        capture_status_.listening = !capture_.permission_needed();
    }

    static void on_uri_scheme(WebKitURISchemeRequest* req, gpointer) {
        const char* uri = webkit_uri_scheme_request_get_uri(req);
        std::string rel = "index.html";
        if (uri) {
            std::string u(uri);
            const std::string prefix = "typecount://app/";
            if (u.rfind(prefix, 0) == 0) rel = u.substr(prefix.size());
            auto q = rel.find('?');
            if (q != std::string::npos) rel.resize(q);
            if (rel.empty() || rel.back() == '/') rel = "index.html";
        }
        const auto* asset = tc::find_ui_asset(rel);
        if (!asset) asset = tc::find_ui_asset("index.html");
        if (!asset) {
            GError* err = g_error_new_literal(g_quark_from_static_string("typecount"), 404, "Not Found");
            webkit_uri_scheme_request_finish_error(req, err);
            g_error_free(err);
            return;
        }
        void* copy = g_malloc(asset->size);
        std::memcpy(copy, asset->data, asset->size);
        GInputStream* stream = g_memory_input_stream_new_from_data(copy, static_cast<gssize>(asset->size), g_free);
        webkit_uri_scheme_request_finish(req, stream, static_cast<gint64>(asset->size), asset->mime);
        g_object_unref(stream);
    }

    static void on_ipc_received(WebKitUserContentManager*, WebKitJavascriptResult* res, gpointer ud) {
        auto* self = static_cast<TypeCountApp*>(ud);
        JSCValue* val = webkit_javascript_result_get_js_value(res);
        if (!val) return;
        gchar* raw = jsc_value_is_string(val) ? jsc_value_to_string(val) : jsc_value_to_json(val, 0);
        if (raw) {
            self->handle_ipc_message(raw);
            g_free(raw);
        }
    }

    static void on_load_changed(WebKitWebView*, WebKitLoadEvent event, gpointer ud) {
        if (event == WEBKIT_LOAD_FINISHED) {
            auto* self = static_cast<TypeCountApp*>(ud);
            self->ui_ready_ = true;
            if (self->session_state_.is_object()) {
                self->eval_js("window.restoreSessionState&&window.restoreSessionState(" + self->session_state_.dump() + ");");
            }
            self->send_view_state();
            self->send_snapshot();
            self->send_update_state();
        }
    }

    static gboolean on_delete_event(GtkWidget*, GdkEvent*, gpointer ud) {
        auto* self = static_cast<TypeCountApp*>(ud);
        if (self->quitting_) return FALSE;
        if (self->prefs_.close_action == "hide") self->hide_main_window(false);
        else if (self->prefs_.close_action == "stealth") self->hide_main_window(true);
        else if (self->prefs_.close_action == "quit") self->quit_app();
        else self->eval_js("window.requestClose&&window.requestClose();");
        return TRUE;
    }

    static gboolean on_window_state(GtkWidget*, GdkEventWindowState* ev, gpointer ud) {
        auto* self = static_cast<TypeCountApp*>(ud);
        self->maximized_ = (ev->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
        self->send_view_state();
        return FALSE;
    }

    static gboolean on_configure_event(GtkWidget* w, GdkEventConfigure*, gpointer ud) {
        auto* self = static_cast<TypeCountApp*>(ud);
        int ppi = detect_monitor_ppi(w);
        if (ppi != self->detected_dpi_) {
            self->detected_dpi_ = ppi;
            if (self->prefs_.dpi_auto && self->prefs_.dpi_ppi != ppi) {
                self->prefs_.dpi_ppi = ppi;
                self->save_prefs();
            }
            if (self->ui_ready_) self->send_view_state();
        }
        return FALSE;
    }

    static void on_gtk_theme_changed(GObject*, GParamSpec*, gpointer ud) {
        auto* self = static_cast<TypeCountApp*>(ud);
        bool dark = detect_os_dark_mode();
        if (dark != self->system_dark_) {
            self->system_dark_ = dark;
            self->apply_gtk_color_scheme();
            self->send_view_state();
        }
    }

    static gboolean on_tick_1s(gpointer ud) {
        static_cast<TypeCountApp*>(ud)->on_flush_timer();
        return G_SOURCE_CONTINUE;
    }

    void on_flush_timer() {
        engine_.flush();
        capture_status_.permission_needed = capture_.permission_needed();
        capture_status_.listening = !capture_.permission_needed();

        bool dark = detect_os_dark_mode();
        if (dark != system_dark_) {
            system_dark_ = dark;
            apply_gtk_color_scheme();
            send_view_state();
        }

        int64_t ms = tc::posix_util::now_utc_ms();
        int tz = tc::posix_util::timezone_offset_sec_for(prefs_.timezone);
        std::string today = tc::Db::format_day(ms / 1000, tz);
        bool fa = (effective_locale() == "fa");

        if (prefs_.auto_backup && !demo_mode_ && last_backup_day_ != today) {
            db_.create_rotating_backup((std::filesystem::path(data_dir_) / "backups").string(), 5);
            last_backup_day_ = today;
        }

        int idle_s = prefs_.idle_detection_enabled ? prefs_.idle_timeout_sec : 180;
        engine_.set_idle_timeout_sec(idle_s);
        auto live = engine_.get_live_metrics(ms, prefs_.break_interval_min, idle_s);

        if (live.break_due && !break_notified_) {
            break_notified_ = true;
            if (prefs_.sound_alerts_enabled) play_pastel_wav(prefs_.notification_sound);
            if (!prefs_.stealth_mode) {
                send_desktop_notification(
                    fa ? "TypeCount · یادآور استراحت" : "TypeCount · Break Reminder",
                    fa ? "۲۰ ثانیه به دوردست نگاه کنید یا مچ دست خود را استراحت دهید."
                       : "Look 20 ft away for 20 seconds or stretch your wrists.");
            }
        } else if (!live.break_due) {
            break_notified_ = false;
        }

        if (prefs_.ergo_guard_enabled && live.ergo_strain_level >= 2 && !ergo_notified_) {
            ergo_notified_ = true;
            if (prefs_.sound_alerts_enabled) play_pastel_wav(prefs_.notification_sound);
            if (!prefs_.stealth_mode) {
                send_desktop_notification(
                    fa ? "TypeCount · هشدار ارگونومی (RSI)" : "TypeCount · Ergonomic Strain Guard",
                    fa ? "فشار تایپ/کلیک در ۱۰ دقیقه اخیر بالا بوده است؛ چند لحظه دست‌ها را استراحت دهید."
                       : "High 10-minute typing/click intensity detected. Take a 30-second wrist stretch.");
            }
            if (ui_visible_ && ui_ready_) toast_key("ergo.strain_alert");
        } else if (live.ergo_strain_level < 2) {
            ergo_notified_ = false;
        }

        if (prefs_.smart_alerts_enabled && !demo_mode_) {
            tc::JsonValue snap = engine_.snapshot(make_query());
            int64_t today_total = snap.get("today").get("keyboard").as_int(0) + snap.get("today").get("mouse").as_int(0);
            if (prefs_.daily_goal > 0 && today_total >= prefs_.daily_goal && db_.get_meta("goal_notified_day") != today) {
                db_.set_meta("goal_notified_day", today);
                if (prefs_.sound_alerts_enabled) play_pastel_wav(prefs_.notification_sound);
                if (!prefs_.stealth_mode) {
                    send_desktop_notification(
                        fa ? "TypeCount · هدف روزانه کامل شد!" : "TypeCount · Daily Goal Reached!",
                        fa ? "آفرین! به هدف روزانه فعالیت خود رسیدید." : "Great work! You reached your daily activity target.");
                }
                if (ui_visible_ && ui_ready_) toast_key("smart.goal_reached");
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
                    if (prefs_.sound_alerts_enabled) play_pastel_wav(prefs_.notification_sound);
                    if (!prefs_.stealth_mode) {
                        send_desktop_notification(
                            fa ? "TypeCount · رکورد جدید سرعت تایپ!" : "TypeCount · New Peak Speed Record!",
                            (fa ? "رکورد جدید سرعت تایپ: " : "New peak typing speed: ") +
                                std::to_string(static_cast<int>(std::round(peak_wpm))) + " WPM");
                    }
                    if (ui_visible_ && ui_ready_) toast_key("smart.new_record");
                }
            }
            if (ui_visible_ && ui_ready_) {
                eval_js((prefs_.compact ? "window.receiveMini(" : "window.receive(") + snap.dump() + ");");
                return;
            }
        }

        if (ui_visible_ && ui_ready_) send_snapshot();
    }

    void update_sport_timer() {
        if (sport_timer_id_ != 0) {
            g_source_remove(sport_timer_id_);
            sport_timer_id_ = 0;
        }
        if (ui_visible_ && prefs_.compact && prefs_.sport_enabled && !paused_) {
            sport_timer_id_ = g_timeout_add(150, +[](gpointer ud) -> gboolean {
                static_cast<TypeCountApp*>(ud)->send_sport_sample();
                return G_SOURCE_CONTINUE;
            }, this);
        }
    }

    void send_sport_sample() {
        if (!ui_ready_ || !ui_visible_ || !prefs_.compact || !prefs_.sport_enabled) return;
        int idle_s = prefs_.idle_detection_enabled ? prefs_.idle_timeout_sec : 180;
        auto live = engine_.get_live_metrics(tc::posix_util::now_utc_ms(), prefs_.break_interval_min, idle_s);
        tc::JsonValue s = tc::JsonValue::object();
        s.set("available", true);
        s.set("paused", paused_);
        s.set("cps", live.live_cps);
        s.set("rate", live.live_cps);
        s.set("wpm", live.live_wpm);
        eval_js("window.receiveSport&&window.receiveSport(" + s.dump() + ");");
    }

    void eval_js(const std::string& js) {
        if (!webview_ || !ui_ready_) return;
#if WEBKIT_CHECK_VERSION(2, 40, 0)
        webkit_web_view_evaluate_javascript(
            WEBKIT_WEB_VIEW(webview_), js.c_str(), static_cast<gssize>(js.size()),
            nullptr, nullptr, nullptr, nullptr, nullptr);
#else
        webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(webview_), js.c_str(), nullptr, nullptr, nullptr);
#endif
    }

    void toast_key(const std::string& key, const std::string& path_val = "") {
        std::string js;
        if (path_val.empty()) {
            js = "window.toast&&window.toast(ClickPulseI18n.t(" + tc::JsonValue::escape_string(key) + "));";
        } else {
            js = "window.toast&&window.toast(ClickPulseI18n.t(" + tc::JsonValue::escape_string(key) +
                 ",{path:" + tc::JsonValue::escape_string(path_val) + "}));";
        }
        eval_js(js);
    }

    void send_view_state() {
        if (!ui_ready_) return;
        tc::JsonValue v = prefs_.to_json();
        v.set("app_version", std::string(TC_VERSION_STR));
        v.set("platform", std::string("linux"));
        v.set("locale", effective_locale());
        v.set("language_preference", prefs_.language);
        v.set("color_mode", prefs_.color_mode);
        v.set("system_dark", system_dark_);
        v.set("effective_dark", effective_dark());
        v.set("detected_dpi", detected_dpi_);
        v.set("visible", ui_visible_);
        v.set("maximized", maximized_);
        v.set("fullscreen", false);
        v.set("auto_check_updates", prefs_.auto_check_updates);
        v.set("last_update_check", prefs_.last_update_check);
        v.set("msi_installed", false);
        v.set("registered_uninstall", false);

        tc::JsonValue tzs = tc::JsonValue::array();
        for (const auto& z : tc::posix_util::system_timezones_list()) tzs.push(z);
        v.set("timezones", std::move(tzs));

        eval_js("window.receiveView&&window.receiveView(" + v.dump() + ");");
    }

    void send_update_state() {
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
        eval_js("window.receiveUpdateState&&window.receiveUpdateState(" + u.dump() + ");");
    }

    tc::SnapshotQuery make_query() {
        tc::SnapshotQuery q;
        q.now_ms = tc::posix_util::now_utc_ms();
        q.tz_offset_sec = tc::posix_util::timezone_offset_sec_for(prefs_.timezone);
        q.include_shortcuts = true;
        q.include_apps = true;
        q.demo = demo_mode_;
        q.prefs = prefs_;
        q.capture = capture_status_;
        q.capture.paused = paused_;
        q.auto_start = is_autostart_enabled();
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

    void send_snapshot() {
        if (!ui_ready_ || !ui_visible_) return;
        tc::JsonValue snap = engine_.snapshot(make_query());
        if (prefs_.compact) {
            eval_js("window.receiveMini&&window.receiveMini(" + snap.dump() + ");");
        } else {
            eval_js("window.receive&&window.receive(" + snap.dump() + ");");
        }
    }

    void apply_window_geometry_for_mode() {
        if (!window_) return;
        if (prefs_.compact) {
            if (maximized_) gtk_window_unmaximize(GTK_WINDOW(window_));
            gtk_window_resize(GTK_WINDOW(window_), 320, 208);
            gtk_window_set_keep_above(GTK_WINDOW(window_), prefs_.pinned ? TRUE : FALSE);
        } else {
            gtk_window_set_keep_above(GTK_WINDOW(window_), FALSE);
            gtk_window_resize(GTK_WINDOW(window_), 1040, 720);
        }
    }

    void show_main_window() {
        if (prefs_.stealth_mode) {
            prefs_.stealth_mode = false;
            save_prefs();
        }
        set_tray_visible(true);
        ui_visible_ = true;
        window_visible_ = true;
        apply_window_geometry_for_mode();
        gtk_widget_show_all(window_);
        gtk_window_deiconify(GTK_WINDOW(window_));
        gtk_window_present(GTK_WINDOW(window_));
        send_view_state();
        send_snapshot();
        update_sport_timer();
    }

    void hide_main_window(bool stealth) {
        prefs_.stealth_mode = stealth;
        save_prefs();
        set_tray_visible(!stealth);
        ui_visible_ = false;
        window_visible_ = false;
        gtk_widget_hide(window_);
        update_sport_timer();
    }

    void toggle_or_show_window() {
        if (ui_visible_ && gtk_widget_get_visible(window_)) hide_main_window(false);
        else show_main_window();
    }

    void toggle_pause() {
        paused_ = !paused_;
        eval_js(std::string("window.receivePaused&&window.receivePaused(") + (paused_ ? "true" : "false") + ");");
        update_sport_timer();
    }

    void quit_app() {
        quitting_ = true;
        engine_.flush();
        save_prefs();
        gtk_main_quit();
    }

    std::string pick_save_file(const char* title, const char* suggested_name, const char* filter_name, const char* pattern) {
        std::string result;
        GtkFileChooserNative* fc = gtk_file_chooser_native_new(
            title, GTK_WINDOW(window_), GTK_FILE_CHOOSER_ACTION_SAVE, "_Save", "_Cancel");
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(fc), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fc), suggested_name);
        GtkFileFilter* ff = gtk_file_filter_new();
        gtk_file_filter_set_name(ff, filter_name);
        gtk_file_filter_add_pattern(ff, pattern);
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), ff);
        if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
            if (char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc))) {
                result = fn;
                g_free(fn);
            }
        }
        g_object_unref(fc);
        return result;
    }

    std::string pick_open_sqlite() {
        std::string result;
        GtkFileChooserNative* fc = gtk_file_chooser_native_new(
            "Import TypeCount History", GTK_WINDOW(window_), GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");
        GtkFileFilter* ff = gtk_file_filter_new();
        gtk_file_filter_set_name(ff, "SQLite Databases (*.sqlite3, *.db)");
        gtk_file_filter_add_pattern(ff, "*.sqlite3");
        gtk_file_filter_add_pattern(ff, "*.db");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), ff);
        if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
            if (char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc))) {
                result = fn;
                g_free(fn);
            }
        }
        g_object_unref(fc);
        return result;
    }

    std::string pick_folder(const char* title) {
        std::string result;
        GtkFileChooserNative* fc = gtk_file_chooser_native_new(
            title, GTK_WINDOW(window_), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Select", "_Cancel");
        if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
            if (char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc))) {
                result = fn;
                g_free(fn);
            }
        }
        g_object_unref(fc);
        return result;
    }

    void start_update_check(bool manual) {
        if (update_checking_.exchange(true)) return;
        update_status_ = "checking";
        send_update_state();
        std::thread([this, manual]() {
            auto res = check_github_release();
            g_idle_add(+[](gpointer ud) -> gboolean {
                auto* payload = static_cast<std::pair<TypeCountApp*, std::pair<bool, UpdateCheckResult>>*>(ud);
                auto* self = payload->first;
                bool is_manual = payload->second.first;
                UpdateCheckResult res = payload->second.second;
                delete payload;

                self->update_checking_.store(false);
                self->prefs_.last_update_check = std::time(nullptr);
                self->save_prefs();

                if (!res.ok) {
                    self->update_status_ = res.rate_limited ? "rate_limited" : "error";
                    if (is_manual) self->toast_key(res.rate_limited ? "update.rate_limited" : "update.failed");
                } else if (res.update_available) {
                    self->update_status_ = "available";
                    self->available_version_ = res.latest_version;
                    self->available_release_url_ = res.release_url;
                    self->available_download_url_ = res.download_url;
                    tc::JsonValue p = tc::JsonValue::object();
                    p.set("version", res.latest_version);
                    p.set("url", res.release_url);
                    p.set("notes", res.notes);
                    self->eval_js("window.receiveUpdateAvailable&&window.receiveUpdateAvailable(" + p.dump() + ");");
                } else {
                    self->update_status_ = "up_to_date";
                    self->available_version_ = res.latest_version;
                    self->available_release_url_ = res.release_url;
                    if (is_manual) {
                        self->eval_js("window.toast&&window.toast(ClickPulseI18n.t('update.up_to_date',{version:" +
                                      tc::JsonValue::escape_string(TC_VERSION_STR) + "}));");
                    }
                }
                self->send_update_state();
                return G_SOURCE_REMOVE;
            }, new std::pair<TypeCountApp*, std::pair<bool, UpdateCheckResult>>(this, {manual, res}));
        }).detach();
    }

    void handle_ipc_message(const std::string& raw) {
        tc::JsonValue msg;
        if (!tc::JsonValue::parse(raw, msg) || !msg.is_object()) return;
        const std::string action = msg.get("action").as_string();
        if (action.empty()) return;

        if (action == "ready") {
            if (session_state_.is_object()) {
                eval_js("window.restoreSessionState&&window.restoreSessionState(" + session_state_.dump() + ");");
            }
            send_view_state();
            send_snapshot();
            send_update_state();
        } else if (action == "pause") {
            toggle_pause();
        } else if (action == "drag") {
            GdkDisplay* dpy = gdk_display_get_default();
            GdkSeat* seat = dpy ? gdk_display_get_default_seat(dpy) : nullptr;
            GdkDevice* ptr = seat ? gdk_seat_get_pointer(seat) : nullptr;
            if (ptr && gtk_widget_get_window(window_)) {
                gint rx = 0, ry = 0;
                gdk_device_get_position(ptr, nullptr, &rx, &ry);
                gtk_window_begin_move_drag(GTK_WINDOW(window_), 1, rx, ry, GDK_CURRENT_TIME);
            }
        } else if (action == "minimize") {
            gtk_window_iconify(GTK_WINDOW(window_));
        } else if (action == "maximize") {
            if (maximized_) gtk_window_unmaximize(GTK_WINDOW(window_));
            else gtk_window_maximize(GTK_WINDOW(window_));
        } else if (action == "close") {
            if (prefs_.close_action == "hide") hide_main_window(false);
            else if (prefs_.close_action == "stealth") hide_main_window(true);
            else if (prefs_.close_action == "quit") quit_app();
            else eval_js("window.requestClose&&window.requestClose();");
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
            if (act == "quit") quit_app();
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
            if (prefs_.compact) gtk_window_set_keep_above(GTK_WINDOW(window_), prefs_.pinned ? TRUE : FALSE);
            send_view_state();
        } else if (action == "language") {
            std::string lang = msg.get("language").as_string_or("en");
            prefs_.language = tc::Preferences::is_valid_language(lang) ? lang : "en";
            if (!demo_mode_ && db_.get_meta("jalali_user_set").empty()) {
                prefs_.jalali_calendar = (effective_locale() == "fa");
            }
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "theme") {
            std::string th = msg.get("theme").as_string_or("blue");
            if (th == "blue" || th == "teal" || th == "violet" || th == "graphite") prefs_.theme = th;
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "color_mode") {
            std::string cm = msg.get("color_mode").as_string_or("system");
            prefs_.color_mode = tc::Preferences::is_valid_color_mode(cm) ? cm : "system";
            save_prefs();
            apply_gtk_color_scheme();
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
            capture_.update_hotkey(prefs_.wake_hotkey);
            save_prefs();
            toast_key("message.saved");
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
        } else if (action == "dpi_auto") {
            prefs_.dpi_auto = msg.get("enabled").as_bool(true);
            if (prefs_.dpi_auto) {
                detected_dpi_ = detect_monitor_ppi(window_);
                prefs_.dpi_ppi = detected_dpi_;
            }
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "detect_dpi") {
            prefs_.dpi_auto = true;
            detected_dpi_ = detect_monitor_ppi(window_);
            prefs_.dpi_ppi = detected_dpi_;
            save_prefs();
            toast_key("message.saved");
            send_view_state();
            send_snapshot();
        } else if (action == "dpi_ppi") {
            prefs_.dpi_auto = false;
            prefs_.dpi_ppi = static_cast<int>(std::max<int64_t>(72, std::min<int64_t>(360, msg.get("ppi").as_int(96))));
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "auto_backup") {
            prefs_.auto_backup = msg.get("enabled").as_bool(true);
            save_prefs();
            send_view_state();
        } else if (action == "reset_break") {
            engine_.reset_break_timer(tc::posix_util::now_utc_ms());
            break_notified_ = false;
            ergo_notified_ = false;
            send_snapshot();
        } else if (action == "save_focus_session") {
            int dur = static_cast<int>(msg.get("duration_sec").as_int(1500));
            int64_t tot = msg.get("total").as_int(0);
            db_.record_focus_session(tc::posix_util::now_utc_ms() / 1000 - dur, dur, tot, 0, "Focus");
            if (prefs_.sound_alerts_enabled) play_pastel_wav(prefs_.notification_sound);
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
                db_.record_typing_test(tc::posix_util::now_utc_ms() / 1000, dur, wpm, raw_cpm, acc, errors, chars, lang);
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
            if (prefs_.sound_alerts_enabled) play_pastel_wav(prefs_.notification_sound);
            send_view_state();
        } else if (action == "notification_sound") {
            std::string snd = msg.get("sound").as_string_or("pastel_pearl");
            prefs_.notification_sound = tc::Preferences::is_valid_sound(snd) ? snd : "pastel_pearl";
            save_prefs();
            if (prefs_.notification_sound != "none") play_pastel_wav(prefs_.notification_sound);
            send_view_state();
        } else if (action == "preview_sound") {
            std::string snd = msg.get("sound").as_string_or(prefs_.notification_sound);
            if (!tc::Preferences::is_valid_sound(snd) || snd == "none") snd = prefs_.notification_sound;
            if (snd != "none") play_pastel_wav(snd);
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
            if (!app.empty() && !tc::posix_util::is_excluded_app(prefs_, app)) {
                prefs_.excluded_apps.push_back(app);
                save_prefs();
                toast_key("apps.excluded_toast");
            }
            send_view_state();
            send_snapshot();
        } else if (action == "unexclude_app") {
            std::string app = msg.get("app").as_string();
            prefs_.excluded_apps.erase(
                std::remove_if(prefs_.excluded_apps.begin(), prefs_.excluded_apps.end(),
                               [&](const std::string& x) { return tc::posix_util::iequals(x, app); }),
                prefs_.excluded_apps.end());
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "idle_detection_enabled") {
            prefs_.idle_detection_enabled = msg.get("enabled").as_bool(true);
            engine_.set_idle_timeout_sec(prefs_.idle_detection_enabled ? prefs_.idle_timeout_sec : 180);
            save_prefs();
            send_view_state();
            send_snapshot();
        } else if (action == "idle_timeout_sec") {
            prefs_.idle_timeout_sec = static_cast<int>(std::max<int64_t>(30, std::min<int64_t>(1800, msg.get("seconds").as_int(180))));
            engine_.set_idle_timeout_sec(prefs_.idle_detection_enabled ? prefs_.idle_timeout_sec : 180);
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
            if (prefs_.auto_check_updates && !demo_mode_) start_update_check(false);
        } else if (action == "check_updates") {
            start_update_check(true);
        } else if (action == "update_release" || action == "install_update") {
            std::string url = (action == "install_update" && !available_download_url_.empty())
                ? available_download_url_ : available_release_url_;
            if (url.empty()) url = TC_RELEASES_URL;
            open_url_or_path(url);
        } else if (action == "session_state") {
            session_state_ = msg.get("state");
            if (session_state_.get("page").as_string() == "share") send_snapshot();
        } else if (action == "history_hours") {
            int64_t req_id = msg.get("id").as_int(0);
            std::string st = msg.get("start").as_string();
            std::string en = msg.get("end").as_string();
            int tz = tc::posix_util::timezone_offset_sec_for(prefs_.timezone);
            tc::JsonValue rows = demo_mode_
                ? db_.query_history_hours_demo(st, en)
                : db_.query_history_hours(st, en, tz);
            eval_js("window.receiveHistoryHours&&window.receiveHistoryHours(" + std::to_string(req_id) + "," + rows.dump() + ");");
        } else if (action == "autostart") {
            bool cur = is_autostart_enabled();
            if (!set_autostart_enabled(!cur)) toast_key("message.autostart_failed");
            else toast_key(!cur ? "message.autostart_enabled" : "message.autostart_disabled");
            send_view_state();
            send_snapshot();
        } else if (action == "reveal") {
            std::string dir = prefs_.custom_db_dir.empty() ? data_dir_ : prefs_.custom_db_dir;
            open_url_or_path(dir);
        } else if (action == "storage_directory") {
            std::string chosen = pick_folder("Choose TypeCount Data Folder");
            if (!chosen.empty()) {
                engine_.flush();
                std::string dst_db = (std::filesystem::path(chosen) / "typecount.sqlite3").string();
                if (std::filesystem::exists(dst_db)) {
                    prefs_.custom_db_dir = chosen;
                    save_prefs();
                    db_.open(dst_db);
                    db_mode_ = "custom";
                    toast_key("storage.opened_existing");
                    send_view_state();
                    send_snapshot();
                    return;
                }
                std::string err;
                if (db_.backup_to(dst_db, &err)) {
                    prefs_.custom_db_dir = chosen;
                    save_prefs();
                    db_.open(dst_db);
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
                std::string def_db = (std::filesystem::path(data_dir_) / "typecount.sqlite3").string();
                prefs_.custom_db_dir.clear();
                save_prefs();
                db_.open(def_db);
                db_mode_ = "default";
                toast_key("storage.reset_done");
                send_view_state();
                send_snapshot();
            }
        } else if (action == "import_history") {
            std::string src = pick_open_sqlite();
            if (!src.empty()) {
                eval_js("window.receiveDataBusy&&window.receiveDataBusy(true);");
                auto rep = db_.import_database(src, (std::filesystem::path(data_dir_) / "backups").string());
                eval_js("window.receiveDataBusy&&window.receiveDataBusy(false);");
                if (rep.ok) {
                    send_snapshot();
                    if (rep.added_ops > 0) {
                        eval_js("window.toast&&window.toast(ClickPulseI18n.t('storage.imported',{count:ClickPulseI18n.number(" +
                                std::to_string(rep.added_ops) + "),overlap:ClickPulseI18n.number(" +
                                std::to_string(rep.overlapped_minutes) + "),path:" +
                                tc::JsonValue::escape_string(rep.backup_path) + "}),7000);");
                    } else {
                        toast_key("storage.no_new_history");
                    }
                } else {
                    toast_key("storage.failed");
                }
            }
        } else if (action == "export_csv") {
            std::string today = tc::Db::format_day(tc::posix_util::now_utc_ms() / 1000, tc::posix_util::timezone_offset_sec_for(prefs_.timezone));
            std::string out = pick_save_file("Export CSV", ("TypeCount-" + today + ".csv").c_str(), "CSV Files (*.csv)", "*.csv");
            if (!out.empty()) {
                engine_.flush();
                if (db_.export_csv(out, tc::posix_util::timezone_offset_sec_for(prefs_.timezone), nullptr, prefs_.dpi_ppi)) {
                    toast_key("export.saved", out);
                } else {
                    toast_key("export.failed");
                }
            }
        } else if (action == "export_json") {
            std::string today = tc::Db::format_day(tc::posix_util::now_utc_ms() / 1000, tc::posix_util::timezone_offset_sec_for(prefs_.timezone));
            std::string out = pick_save_file("Export JSON", ("TypeCount-" + today + ".json").c_str(), "JSON Files (*.json)", "*.json");
            if (!out.empty()) {
                engine_.flush();
                if (db_.export_json(out, tc::posix_util::timezone_offset_sec_for(prefs_.timezone), nullptr, prefs_.dpi_ppi)) {
                    toast_key("export.saved", out);
                } else {
                    toast_key("export.failed");
                }
            }
        } else if (action == "backup_now") {
            engine_.flush();
            std::string created;
            if (db_.create_rotating_backup((std::filesystem::path(data_dir_) / "backups").string(), 5, &created)) {
                toast_key("export.backup_saved", created);
            } else {
                toast_key("export.failed");
            }
        } else if (action == "save_share_card") {
            std::string day = msg.get("day").as_string_or("report");
            std::string period = msg.get("period").as_string_or("");
            std::string fname = "TypeCount-" + (period == "weekly" ? ("Weekly-" + msg.get("start").as_string_or("") + "_") : "") + day + ".png";
            std::string out = pick_save_file("Save Share Card", fname.c_str(), "PNG Image (*.png)", "*.png");
            if (!out.empty()) {
                auto bytes = tc::posix_util::decode_base64_data_url(msg.get("png").as_string());
                std::ofstream f(out, std::ios::binary | std::ios::trunc);
                if (!bytes.empty() && f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())).good()) {
                    toast_key("share.saved");
                } else {
                    toast_key("share.failed");
                }
            }
        } else if (action == "copy_share_card") {
            auto bytes = tc::posix_util::decode_base64_data_url(msg.get("png").as_string());
            bool copied = false;
            if (!bytes.empty()) {
                GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
                if (loader) {
                    if (gdk_pixbuf_loader_write(loader, bytes.data(), bytes.size(), nullptr) &&
                        gdk_pixbuf_loader_close(loader, nullptr)) {
                        if (GdkPixbuf* pb = gdk_pixbuf_loader_get_pixbuf(loader)) {
                            if (GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD)) {
                                gtk_clipboard_set_image(cb, pb);
                                gtk_clipboard_store(cb);
                                copied = true;
                            }
                        }
                    }
                    g_object_unref(loader);
                }
            }
            toast_key(copied ? "share.copied" : "share.copy_failed");
        } else if (action == "project_home" || action == "shortcut_atlas") {
            open_url_or_path("https://github.com/Amirmpm/typeCount");
        } else if (action == "send_feedback" || action == "suggest_shortcut") {
            open_url_or_path("https://github.com/Amirmpm/typeCount/issues/new");
        } else if (action == "permission") {
            start_input_capture();
            send_snapshot();
        }
    }

    void start_ipc_socket() {
        std::string sp = runtime_socket_path();
        unlink(sp.c_str());
        ipc_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (ipc_fd_ < 0) return;
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (sp.size() >= sizeof(addr.sun_path)) {
            close(ipc_fd_);
            ipc_fd_ = -1;
            return;
        }
        std::strcpy(addr.sun_path, sp.c_str());
        if (bind(ipc_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0 ||
            listen(ipc_fd_, 4) != 0) {
            close(ipc_fd_);
            ipc_fd_ = -1;
            return;
        }
        ipc_running_.store(true);
        ipc_thread_ = std::thread([this]() {
            while (ipc_running_.load()) {
                struct pollfd pfd{ipc_fd_, POLLIN, 0};
                int rc = poll(&pfd, 1, 400);
                if (rc > 0 && (pfd.revents & POLLIN)) {
                    int cli = accept(ipc_fd_, nullptr, nullptr);
                    if (cli >= 0) {
                        close(cli);
                        g_idle_add(+[](gpointer ud) -> gboolean {
                            static_cast<TypeCountApp*>(ud)->show_main_window();
                            return G_SOURCE_REMOVE;
                        }, this);
                    }
                }
            }
        });
    }

    void stop_ipc_socket() {
        ipc_running_.store(false);
        if (ipc_fd_ >= 0) {
            shutdown(ipc_fd_, SHUT_RDWR);
            close(ipc_fd_);
            ipc_fd_ = -1;
        }
        if (ipc_thread_.joinable()) ipc_thread_.join();
        unlink(runtime_socket_path().c_str());
    }

    std::string default_dir_;
    std::string data_dir_;
    std::string prefs_path_;
    std::string db_path_;
    std::string db_mode_ = "default";
    std::string last_backup_day_;

    tc::Preferences prefs_;
    tc::Db db_;
    tc::StatsEngine engine_;
    tc::CaptureStatusInfo capture_status_;
    LinuxInputCapture capture_;

    tc::JsonValue session_state_;
    GtkWidget* window_ = nullptr;
    GtkWidget* webview_ = nullptr;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    GtkStatusIcon* tray_icon_ = nullptr;
#pragma GCC diagnostic pop

    int lock_fd_ = -1;
    int ipc_fd_ = -1;
    std::atomic<bool> ipc_running_{false};
    std::thread ipc_thread_;

    bool demo_mode_ = false;
    bool paused_ = false;
    bool typing_test_running_ = false;
    bool system_dark_ = false;
    int detected_dpi_ = 96;
    bool ui_ready_ = false;
    bool ui_visible_ = true;
    bool window_visible_ = true;
    bool maximized_ = false;
    bool quitting_ = false;
    bool break_notified_ = false;
    bool ergo_notified_ = false;
    guint sport_timer_id_ = 0;

    std::atomic<bool> update_checking_{false};
    std::string update_status_ = "idle";
    std::string available_version_;
    std::string available_release_url_;
    std::string available_download_url_;
};

} // namespace tc::linux_host
