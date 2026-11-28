#include "../src/core/db.hpp"
#include "../src/core/engine.hpp"
#include "../src/core/json.hpp"
#include "../src/core/key_mapper.hpp"
#ifndef _WIN32
#include "../src/core/posix_host_util.hpp"
#endif
#include "../src/core/preferences.hpp"
#include "../src/core/sound_synth.hpp"
#include "../src/core/version.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

void test_json() {
    tc::JsonValue v;
    assert(tc::JsonValue::parse(R"({"hello":"world","n":42,"arr":[true,null,-3.5]})", v));
    assert(v.is_object());
    assert(v.get("hello").as_string() == "world");
    assert(v.get("n").as_int() == 42);
    assert(v.get("arr").arr.size() == 3);
    assert(v.get("arr").arr[0].as_bool() == true);
    assert(v.get("arr").arr[1].is_null());
    assert(std::abs(v.get("arr").arr[2].as_double() + 3.5) < 1e-9);

    // UTF-8 & escapes roundtrip
    tc::JsonValue u = tc::JsonValue::object();
    u.set("fa", std::string("تایپ‌کانت — فارسی"));
    u.set("quote", std::string("a\"b\\c\nd"));
    tc::JsonValue u2;
    assert(tc::JsonValue::parse(u.dump(), u2));
    assert(u2.get("fa").as_string() == "تایپ‌کانت — فارسی");
    assert(u2.get("quote").as_string() == "a\"b\\c\nd");
    std::cout << "[PASS] test_json\n";
}

void test_key_mapper() {
    assert(tc::canonical_code(0x1e) == 0);      // A
    assert(tc::canonical_code(0x1d) == 59);     // LCtrl
    assert(tc::canonical_code(0x11d) == 62);    // RCtrl
    assert(tc::canonical_code(0x138) == 61);    // RAlt / AltGr
    assert(tc::canonical_code(0x137) == 0x10137); // PrtSc
    assert(tc::canonical_code(0x245) == 0x10245); // Pause

    tc::KeyTracker kt;
    // Key down + auto-repeat suppression
    auto k1 = kt.process_event(100, 0x1e, 0, 'A', 1000, false);
    assert(k1.has_value() && *k1 == 0);
    auto k_rep = kt.process_event(100, 0x1e, 0, 'A', 1050, false);
    assert(!k_rep.has_value()); // repeat suppressed
    auto k_up = kt.process_event(100, 0x1e, tc::TC_RI_KEY_BREAK, 'A', 1100, false);
    assert(!k_up.has_value());
    auto k2 = kt.process_event(100, 0x1e, 0, 'A', 1200, false);
    assert(k2.has_value() && *k2 == 0);

    // Software injection filtering
    auto sw_ignored = kt.process_event(0, 0x1f, 0, 'S', 1250, true);
    assert(!sw_ignored.has_value());
    auto sw_kept = kt.process_event(0, 0x1f, 0, 'S', 1260, false);
    assert(sw_kept.has_value() && *sw_kept == 1);
    kt.process_event(0, 0x1f, tc::TC_RI_KEY_BREAK, 'S', 1270, false);

    // Shortcut tracking (Ctrl+C)
    auto ctrl_dn = kt.process_event(100, 0x1d, 0, 0xA2, 2000, false);
    assert(ctrl_dn.has_value() && *ctrl_dn == 59);
    auto c_dn = kt.process_event(100, 0x2e, 0, 'C', 2020, false);
    assert(c_dn.has_value() && *c_dn == 8);
    auto chord = kt.shortcut_for(100, *c_dn, 2020, [](uint16_t) { return true; });
    assert(chord.has_value());
    assert(chord->modifiers == tc::MOD_CTRL && chord->key == 8);

    // AltGr exclusion
    kt.reset();
    kt.process_event(100, 0x38, tc::TC_RI_KEY_E0, 0xA5, 3000, false); // RAlt
    auto q_dn = kt.process_event(100, 0x10, 0, 'Q', 3010, false);
    assert(q_dn.has_value());
    auto altgr_chord = kt.shortcut_for(100, *q_dn, 3010, [](uint16_t) { return true; });
    assert(!altgr_chord.has_value());

    // Mouse buttons bitmask
    auto btns = tc::extract_mouse_buttons(1 | 4 | 64);
    assert(btns.size() == 3 && btns[0] == 1 && btns[1] == 2 && btns[2] == 4);

    // Linux evdev -> PS/2 scan -> canonical keycode & macOS virtual keycode
    assert(tc::canonical_code(tc::evdev_to_scan(30)) == 0);        // KEY_A -> A
    assert(tc::canonical_code(tc::evdev_to_scan(29)) == 59);       // KEY_LEFTCTRL
    assert(tc::canonical_code(tc::evdev_to_scan(97)) == 62);       // KEY_RIGHTCTRL
    assert(tc::canonical_code(tc::evdev_to_scan(125)) == 55);      // KEY_LEFTMETA -> Super
    assert(tc::canonical_code(tc::evdev_to_scan(99)) == 0x10137);  // KEY_SYSRQ
    assert(tc::canonical_code(tc::evdev_to_scan(119)) == 0x10245); // KEY_PAUSE
    assert(tc::mac_keycode_to_canonical(0) == 0);

#ifndef _WIN32
    tc::Preferences ex_p;
    ex_p.app_exclusions_enabled = true;
    ex_p.excluded_apps = {"KeePassXC"};
    assert(tc::posix_util::is_excluded_app(ex_p, "keepassxc"));
    assert(!tc::posix_util::is_excluded_app(ex_p, "firefox"));
    auto dec = tc::posix_util::decode_base64_data_url("data:image/png;base64,aGVsbG8=");
    assert(dec.size() == 5 && std::memcmp(dec.data(), "hello", 5) == 0);
    (void)tc::posix_util::timezone_offset_sec_for("UTC");
#endif
    std::cout << "[PASS] test_key_mapper\n";
}

void test_preferences() {
    fs::path tmp = fs::temp_directory_path() / "tc_prefs_test.json";
    fs::remove(tmp);

    tc::Preferences p;
    p.language = "fa";
    p.theme = "teal";
    p.color_mode = "dark";
    p.dpi_auto = false;
    p.dpi_ppi = 144;
    p.notification_sound = "lavender_harp";
    p.stealth_mode = true;
    p.wake_hotkey = "Ctrl+Alt+K";
    p.daily_goal = 12000;
    p.excluded_apps = {"keepass.exe"};
    p.typing_test_enabled = false;
    p.typing_test_count_stats = true;
    p.ergo_insights_enabled = false;
    p.ergo_guard_enabled = false;
    p.smart_alerts_enabled = false;
    p.sound_alerts_enabled = false;
    p.app_exclusions_enabled = false;
    p.pause_in_excluded_apps = true;
    p.idle_detection_enabled = false;
    p.idle_timeout_sec = 180;
    p.jalali_calendar = true;
    p.auto_check_updates = false;
    p.last_update_check = 1764244800LL;
    assert(p.save_file(tmp.string()));

    bool ok = false;
    auto p2 = tc::Preferences::load_file(tmp.string(), &ok);
    assert(ok);
    assert(p2.language == "fa");
    assert(p2.theme == "teal");
    assert(p2.color_mode == "dark");
    assert(p2.dpi_auto == false);
    assert(p2.dpi_ppi == 144);
    assert(p2.notification_sound == "lavender_harp");
    assert(p2.stealth_mode == true);
    assert(p2.wake_hotkey == "Ctrl+Alt+K");
    assert(p2.daily_goal == 12000);
    assert(p2.excluded_apps.size() == 1 && p2.excluded_apps[0] == "keepass.exe");
    assert(p2.typing_test_enabled == false);
    assert(p2.typing_test_count_stats == true);
    assert(p2.ergo_insights_enabled == false);
    assert(p2.ergo_guard_enabled == false);
    assert(p2.smart_alerts_enabled == false);
    assert(p2.sound_alerts_enabled == false);
    assert(p2.app_exclusions_enabled == false);
    assert(p2.pause_in_excluded_apps == true);
    assert(p2.idle_detection_enabled == false);
    assert(p2.idle_timeout_sec == 180);
    assert(p2.jalali_calendar == true);
    assert(p2.auto_check_updates == false);
    assert(p2.last_update_check == 1764244800LL);

    // Ensure all 10 locales survive roundtrip
    for (const char* loc : {"en", "fa", "system", "zh-CN", "zh-TW", "ja", "ko", "fr", "de", "es"}) {
        tc::Preferences lp;
        lp.language = loc;
        assert(lp.save_file(tmp.string()));
        auto loaded = tc::Preferences::load_file(tmp.string(), &ok);
        assert(ok && loaded.language == loc);
    }

    // Verify synthesized soft pastel WAV headers and silence preset
    for (const char* snd : {"pastel_pearl", "mist_chime", "peach_marimba", "lavender_harp", "mint_dew", "silk_breeze"}) {
        const auto& wav = tc::sound::get_cached_pastel_wav(snd);
        assert(wav.size() > 1000 && std::memcmp(wav.data(), "RIFF", 4) == 0 && std::memcmp(wav.data() + 8, "WAVE", 4) == 0);
    }
    assert(tc::sound::get_cached_pastel_wav("none").empty());

    fs::remove(tmp);
    assert(std::string(TC_VERSION_STR) == "1.2.0");
    assert(TC_VERSION_NUM == 10200);
    std::cout << "[PASS] test_preferences\n";
}

void test_db_and_engine() {
    fs::path dir = fs::temp_directory_path() / "tc_db_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::path db_path = dir / "typecount.sqlite3";
    fs::path db_path2 = dir / "other.sqlite3";
    fs::path csv_path = dir / "export.csv";
    fs::path json_path = dir / "export.json";
    fs::path backup_dir = dir / "backups";

    tc::Db db;
    std::string err;
    assert(db.open(db_path.string(), &err));

    tc::StatsEngine eng(db);
    int64_t base_ms = 1764244800000LL; // 2025-11-27 12:00:00Z
    // Simulate fast typing burst (12 keys over 3 seconds => ~48 WPM)
    for (int i = 0; i < 12; ++i) {
        eng.on_key_press(base_ms + i * 250, 0, i % 10, false, std::nullopt, "Code.exe", true);
    }
    // Simulate Ctrl+C shortcut
    eng.on_key_press(base_ms + 3200, 0, 8, false, tc::ShortcutChord{tc::MOD_CTRL, 8}, "Code.exe", true);
    // Simulate mouse clicks + scroll + move
    eng.on_mouse_click(base_ms + 3300, 0, 1, "Code.exe", true);
    eng.on_mouse_click(base_ms + 3450, 0, 2, "chrome.exe", true);
    eng.on_mouse_scroll(base_ms + 3600, 0, 4, "chrome.exe", true);
    eng.on_mouse_move(base_ms + 3700, 0, 9600, "chrome.exe", true);

    tc::SnapshotQuery q;
    q.now_ms = base_ms + 4000;
    q.tz_offset_sec = 0;
    q.prefs.daily_goal = 100;
    q.prefs.dpi_ppi = 96;
    q.report_period = "daily";
    q.report_day = tc::Db::format_day(q.now_ms / 1000, 0);

    tc::JsonValue snap = eng.snapshot(q);
    assert(snap.get("today").get("keyboard").as_int() == 13);
    assert(snap.get("today").get("mouse").as_int() == 2);
    assert(snap.get("today_scroll").as_int() == 4);
    assert(std::abs(snap.get("today_distance_m").as_double() - 2.5) < 0.2); // 9600px / 96ppi = 100 in = 2.54m
    assert(snap.get("live_wpm").as_double() > 30.0);
    assert(snap.get("apps_today").arr.size() == 2);
    assert(snap.get("apps_today").arr[0].get("app").as_string() == "Code.exe");
    assert(snap.get("shortcuts").arr.size() == 1);
    assert(snap.get("report").is_object());
    assert(snap.get("streak_days").as_int() == 1);
    assert(snap.get("today_active_sec").as_int() >= 1);
    assert(snap.get("focus_score").as_int() >= 0 && snap.get("focus_score").as_int() <= 100);
    assert(snap.get("peak_hour").as_int() == 12);
    assert(snap.get("ergo_10m_ops").as_int() >= 15);

    // Meta & typing tests persistence
    assert(db.set_meta("goal_notified_day", q.report_day));
    assert(db.get_meta("goal_notified_day") == q.report_day);
    assert(db.record_typing_test(q.now_ms / 1000, 30, 84.5, 430.0, 97.2, 2, 215, "en"));
    tc::JsonValue snap2 = eng.snapshot(q);
    assert(std::abs(snap2.get("typing_test_best_wpm").as_double() - 84.5) < 0.01);
    assert(snap2.get("typing_tests").arr.size() == 1);

    // CSV & JSON export (includes active_seconds column)
    assert(db.export_csv(csv_path.string(), 0, &err));
    assert(fs::exists(csv_path) && fs::file_size(csv_path) > 40);
    assert(db.export_json(json_path.string(), 0, &err));
    assert(fs::exists(json_path) && fs::file_size(json_path) > 100);

    // Rotating backup
    std::string bpath;
    assert(db.create_rotating_backup(backup_dir.string(), 3, &bpath, &err));
    assert(fs::exists(bpath));

    // Create another DB and test non-double-counting MAX merge import
    {
        tc::Db other;
        assert(other.open(db_path2.string()));
        tc::BatchDelta b;
        b.minute_ts = (base_ms / 1000) / 60;
        b.local_day = q.report_day;
        b.keyboard = 20; // > 13 in target -> MAX yields 20 (net +7)
        b.mouse = 1;     // < 2 in target -> MAX yields 2 (net +0)
        assert(other.apply_batch(b));
    }
    auto rep = db.import_database(db_path2.string(), backup_dir.string());
    assert(rep.ok);
    assert(rep.added_ops == 7);
    assert(rep.overlapped_minutes == 1);

    // Repeat import adds 0 ops
    auto rep2 = db.import_database(db_path2.string(), backup_dir.string());
    assert(rep2.ok);
    assert(rep2.added_ops == 0);

    // Demo snapshot
    q.demo = true;
    tc::JsonValue demo = eng.snapshot(q);
    assert(demo.get("demo").as_bool() == true);
    assert(demo.get("history_days").arr.size() == 180);
    assert(demo.get("apps_today").arr.size() == 5);
    assert(demo.get("typing_test_best_wpm").as_double() > 0.0);
    assert(demo.get("focus_score").as_int() > 0);

    db.close();
    fs::remove_all(dir);
    std::cout << "[PASS] test_db_and_engine\n";
}

int main() {
    test_json();
    test_key_mapper();
    test_preferences();
    test_db_and_engine();
    std::cout << "All C++ core tests passed!\n";
    return 0;
}
