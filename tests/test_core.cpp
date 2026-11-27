#include "../src/core/db.hpp"
#include "../src/core/engine.hpp"
#include "../src/core/json.hpp"
#include "../src/core/key_mapper.hpp"
#include "../src/core/preferences.hpp"

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
    std::cout << "[PASS] test_key_mapper\n";
}

void test_preferences() {
    fs::path tmp = fs::temp_directory_path() / "tc_prefs_test.json";
    fs::remove(tmp);

    tc::Preferences p;
    p.language = "fa";
    p.theme = "teal";
    p.stealth_mode = true;
    p.wake_hotkey = "Ctrl+Alt+K";
    p.daily_goal = 12000;
    p.excluded_apps = {"keepass.exe"};
    assert(p.save_file(tmp.string()));

    bool ok = false;
    auto p2 = tc::Preferences::load_file(tmp.string(), &ok);
    assert(ok);
    assert(p2.language == "fa");
    assert(p2.theme == "teal");
    assert(p2.stealth_mode == true);
    assert(p2.wake_hotkey == "Ctrl+Alt+K");
    assert(p2.daily_goal == 12000);
    assert(p2.excluded_apps.size() == 1 && p2.excluded_apps[0] == "keepass.exe");
    fs::remove(tmp);
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

    // CSV & JSON export
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
