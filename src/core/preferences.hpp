#pragma once

#include "json.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace tc {

struct Preferences {
    std::string language = "en";               // "en", "fa", "system", ...
    std::string theme = "blue";                // "blue", "teal", "violet", "graphite"
    std::string color_mode = "system";         // "system", "light", "dark"
    std::string ui_scale = "100";              // "80", "90", "100", "115", "130", "150"
    std::string today_period = "calendar_day"; // "calendar_day", "rolling_24h"
    std::string timezone = "system";           // "system" or IANA zone
    std::string keyboard_layout = "compact";   // "compact", "full_size"
    std::string keyboard_view = "layout";      // "layout", "treemap"
    std::string close_action = "ask";          // "ask", "hide", "stealth", "quit"
    std::string startup_mode = "tray";         // "tray", "stealth"
    std::string wake_hotkey = "Ctrl+Alt+Shift+T";
    std::string notification_sound = "pastel_pearl"; // "pastel_pearl", "mist_chime", "peach_marimba", "lavender_harp", "mint_dew", "silk_breeze", "none"
    bool stealth_mode = false;
    bool compact = false;
    bool pinned = true;
    bool sport_enabled = false;
    bool sport_visible = true;
    bool ignore_software_input = false;
    bool track_apps = true;
    bool app_exclusions_enabled = true;
    bool pause_in_excluded_apps = false;
    bool idle_detection_enabled = true;
    int idle_timeout_sec = 180;                // 60, 90, 180, 300
    bool typing_test_enabled = true;
    bool typing_test_count_stats = false;
    bool ergo_insights_enabled = true;
    bool ergo_guard_enabled = true;
    bool smart_alerts_enabled = true;
    bool sound_alerts_enabled = true;
    bool jalali_calendar = false;
    bool auto_check_updates = true;
    int64_t last_update_check = 0;
    bool auto_backup = true;
    bool sidebar_collapsed = false;
    int64_t daily_goal = 8000;
    int break_interval_min = 45;               // 0=off, 20, 45, 60
    bool dpi_auto = true;                      // Automatic physical monitor PPI detection
    int dpi_ppi = 96;
    int window_width = 1160;
    int window_height = 840;
    int window_x = -1;
    int window_y = -1;
    std::string custom_db_dir;
    std::vector<std::string> excluded_apps;

    static bool is_valid_language(const std::string& lang) {
        return lang == "en" || lang == "fa" || lang == "system" ||
               lang == "zh-CN" || lang == "zh-TW" || lang == "ja" ||
               lang == "ko" || lang == "fr" || lang == "de" || lang == "es";
    }

    static bool is_valid_color_mode(const std::string& mode) {
        return mode == "system" || mode == "light" || mode == "dark";
    }

    static bool is_valid_sound(const std::string& snd) {
        return snd == "pastel_pearl" || snd == "mist_chime" || snd == "peach_marimba" ||
               snd == "lavender_harp" || snd == "mint_dew" || snd == "silk_breeze" ||
               snd == "none";
    }

    JsonValue to_json() const {
        JsonValue o = JsonValue::object();
        o.set("language", language);
        o.set("theme", theme);
        o.set("color_mode", color_mode);
        o.set("ui_scale", ui_scale);
        o.set("today_period", today_period);
        o.set("timezone", timezone);
        o.set("keyboard_layout", keyboard_layout);
        o.set("keyboard_view", keyboard_view);
        o.set("close_action", close_action);
        o.set("startup_mode", startup_mode);
        o.set("wake_hotkey", wake_hotkey);
        o.set("notification_sound", notification_sound);
        o.set("stealth_mode", stealth_mode);
        o.set("compact", compact);
        o.set("pinned", pinned);
        o.set("sport_enabled", sport_enabled);
        o.set("sport_visible", sport_visible);
        o.set("ignore_software_input", ignore_software_input);
        o.set("track_apps", track_apps);
        o.set("app_exclusions_enabled", app_exclusions_enabled);
        o.set("pause_in_excluded_apps", pause_in_excluded_apps);
        o.set("idle_detection_enabled", idle_detection_enabled);
        o.set("idle_timeout_sec", idle_timeout_sec);
        o.set("typing_test_enabled", typing_test_enabled);
        o.set("typing_test_count_stats", typing_test_count_stats);
        o.set("ergo_insights_enabled", ergo_insights_enabled);
        o.set("ergo_guard_enabled", ergo_guard_enabled);
        o.set("smart_alerts_enabled", smart_alerts_enabled);
        o.set("sound_alerts_enabled", sound_alerts_enabled);
        o.set("jalali_calendar", jalali_calendar);
        o.set("auto_check_updates", auto_check_updates);
        o.set("last_update_check", last_update_check);
        o.set("auto_backup", auto_backup);
        o.set("sidebar_collapsed", sidebar_collapsed);
        o.set("daily_goal", daily_goal);
        o.set("break_interval_min", break_interval_min);
        o.set("dpi_auto", dpi_auto);
        o.set("dpi_ppi", dpi_ppi);
        o.set("window_width", window_width);
        o.set("window_height", window_height);
        o.set("window_x", window_x);
        o.set("window_y", window_y);
        o.set("custom_db_dir", custom_db_dir);
        JsonValue ex = JsonValue::array();
        for (const auto& a : excluded_apps) ex.push(a);
        o.set("excluded_apps", std::move(ex));
        return o;
    }

    static Preferences from_json(const JsonValue& o) {
        Preferences p;
        if (!o.is_object()) return p;
        p.language = o.get("language").as_string_or("en");
        if (!is_valid_language(p.language)) p.language = "en";

        p.theme = o.get("theme").as_string_or("blue");
        p.color_mode = o.get("color_mode").as_string_or("system");
        if (p.theme == "dark") {
            p.theme = "blue";
            p.color_mode = "dark";
        }
        if (p.theme != "blue" && p.theme != "teal" && p.theme != "violet" && p.theme != "graphite") p.theme = "blue";
        if (!is_valid_color_mode(p.color_mode)) p.color_mode = "system";

        p.ui_scale = o.get("ui_scale").as_string_or("100");
        if (p.ui_scale != "80" && p.ui_scale != "90" && p.ui_scale != "100" &&
            p.ui_scale != "115" && p.ui_scale != "130" && p.ui_scale != "150") {
            p.ui_scale = "100";
        }

        p.today_period = o.get("today_period").as_string_or("calendar_day");
        if (p.today_period != "calendar_day" && p.today_period != "rolling_24h") p.today_period = "calendar_day";

        p.timezone = o.get("timezone").as_string_or("system");
        p.keyboard_layout = o.get("keyboard_layout").as_string_or("compact");
        if (p.keyboard_layout != "compact" && p.keyboard_layout != "full_size") p.keyboard_layout = "compact";

        p.keyboard_view = o.get("keyboard_view").as_string_or("layout");
        if (p.keyboard_view != "layout" && p.keyboard_view != "treemap") p.keyboard_view = "layout";

        p.close_action = o.get("close_action").as_string_or("ask");
        if (p.close_action != "ask" && p.close_action != "hide" &&
            p.close_action != "stealth" && p.close_action != "quit") {
            p.close_action = "ask";
        }

        p.startup_mode = o.get("startup_mode").as_string_or("tray");
        if (p.startup_mode != "tray" && p.startup_mode != "stealth") p.startup_mode = "tray";

        p.wake_hotkey = o.get("wake_hotkey").as_string_or("Ctrl+Alt+Shift+T");
        p.notification_sound = o.get("notification_sound").as_string_or("pastel_pearl");
        if (!is_valid_sound(p.notification_sound)) p.notification_sound = "pastel_pearl";

        p.stealth_mode = o.get("stealth_mode").as_bool(false);
        p.compact = o.get("compact").as_bool(false);
        p.pinned = o.get("pinned").as_bool(true);
        p.sport_enabled = o.get("sport_enabled").as_bool(false);
        p.sport_visible = o.get("sport_visible").as_bool(true);
        p.ignore_software_input = o.get("ignore_software_input").as_bool(false);
        p.track_apps = o.get("track_apps").as_bool(true);
        p.app_exclusions_enabled = o.get("app_exclusions_enabled").as_bool(true);
        p.pause_in_excluded_apps = o.get("pause_in_excluded_apps").as_bool(false);
        p.idle_detection_enabled = o.get("idle_detection_enabled").as_bool(true);
        p.idle_timeout_sec = static_cast<int>(std::max<int64_t>(30, std::min<int64_t>(1800, o.get("idle_timeout_sec").as_int(180))));
        p.typing_test_enabled = o.get("typing_test_enabled").as_bool(true);
        p.typing_test_count_stats = o.get("typing_test_count_stats").as_bool(false);
        p.ergo_insights_enabled = o.get("ergo_insights_enabled").as_bool(true);
        p.ergo_guard_enabled = o.get("ergo_guard_enabled").as_bool(true);
        p.smart_alerts_enabled = o.get("smart_alerts_enabled").as_bool(true);
        p.sound_alerts_enabled = o.get("sound_alerts_enabled").as_bool(true);
        p.jalali_calendar = o.get("jalali_calendar").as_bool(false);
        p.auto_check_updates = o.get("auto_check_updates").as_bool(true);
        p.last_update_check = o.get("last_update_check").as_int(0);
        p.auto_backup = o.get("auto_backup").as_bool(true);
        p.sidebar_collapsed = o.get("sidebar_collapsed").as_bool(false);
        p.daily_goal = std::max<int64_t>(500, std::min<int64_t>(500000, o.get("daily_goal").as_int(8000)));
        p.break_interval_min = static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(240, o.get("break_interval_min").as_int(45))));
        p.dpi_auto = o.get("dpi_auto").as_bool(true);
        p.dpi_ppi = static_cast<int>(std::max<int64_t>(72, std::min<int64_t>(360, o.get("dpi_ppi").as_int(96))));
        p.window_width = static_cast<int>(std::max<int64_t>(720, std::min<int64_t>(3840, o.get("window_width").as_int(1160))));
        p.window_height = static_cast<int>(std::max<int64_t>(540, std::min<int64_t>(2160, o.get("window_height").as_int(840))));
        p.window_x = static_cast<int>(o.get("window_x").as_int(-1));
        p.window_y = static_cast<int>(o.get("window_y").as_int(-1));
        p.custom_db_dir = o.get("custom_db_dir").as_string_or("");
        const auto& ex = o.get("excluded_apps");
        if (ex.is_array()) {
            for (const auto& item : ex.arr) {
                if (item.is_string() && !item.as_string().empty()) {
                    p.excluded_apps.push_back(item.as_string());
                }
            }
        }
        return p;
    }

    static Preferences load_file(const std::string& path, bool* ok = nullptr) {
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            if (ok) *ok = false;
            return Preferences{};
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        JsonValue val;
        if (!JsonValue::parse(ss.str(), val)) {
            if (ok) *ok = false;
            return Preferences{};
        }
        if (ok) *ok = true;
        return from_json(val);
    }

    bool save_file(const std::string& path) const {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        std::string out = to_json().dump(2);
        f.write(out.data(), static_cast<std::streamsize>(out.size()));
        return f.good();
    }
};

} // namespace tc
