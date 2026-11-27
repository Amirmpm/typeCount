#include "db.hpp"
#include "sqlite3.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace tc {

namespace {

bool ensure_dir_exists(const std::string& dir) {
    if (dir.empty()) return false;
#ifdef _WIN32
    std::string cur;
    for (size_t i = 0; i < dir.size(); ++i) {
        char c = dir[i];
        cur.push_back(c);
        if (c == '\\' || c == '/') {
            if (cur.size() > 3) _mkdir(cur.c_str());
        }
    }
    _mkdir(dir.c_str());
    return true;
#else
    std::string cur;
    for (size_t i = 0; i < dir.size(); ++i) {
        char c = dir[i];
        cur.push_back(c);
        if (c == '/' && cur.size() > 1) {
            mkdir(cur.c_str(), 0755);
        }
    }
    mkdir(dir.c_str(), 0755);
    return true;
#endif
}

std::vector<std::string> list_backup_files(const std::string& dir) {
    std::vector<std::string> files;
#ifdef _WIN32
    std::string pattern = dir + "\\typecount-backup-*.sqlite3";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                files.push_back(dir + "\\" + fd.cFileName);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR* d = opendir(dir.c_str());
    if (d) {
        while (auto* ent = readdir(d)) {
            std::string name = ent->d_name;
            if (name.rfind("typecount-backup-", 0) == 0 && name.size() > 8 &&
                name.substr(name.size() - 8) == ".sqlite3") {
                files.push_back(dir + "/" + name);
            }
        }
        closedir(d);
    }
#endif
    std::sort(files.begin(), files.end());
    return files;
}

std::string csv_escape(const std::string& s) {
    bool needs = s.find_first_of(",\"\r\n") != std::string::npos;
    if (!needs) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out.push_back(c);
    }
    out.push_back('"');
    return out;
}

void time_to_tm_utc(int64_t sec, std::tm& out) {
    std::time_t tt = static_cast<std::time_t>(sec);
#ifdef _WIN32
    gmtime_s(&out, &tt);
#else
    gmtime_r(&tt, &out);
#endif
}

int64_t parse_day_epoch_utc(const std::string& day) {
    if (day.size() < 10) return 0;
    std::tm tm{};
    tm.tm_year = std::atoi(day.substr(0, 4).c_str()) - 1900;
    tm.tm_mon = std::atoi(day.substr(5, 2).c_str()) - 1;
    tm.tm_mday = std::atoi(day.substr(8, 2).c_str());
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
#ifdef _WIN32
    return static_cast<int64_t>(_mkgmtime(&tm));
#else
    return static_cast<int64_t>(timegm(&tm));
#endif
}

} // namespace

std::string Db::format_day(int64_t epoch_sec, int tz_offset_sec) {
    std::tm tm{};
    time_to_tm_utc(epoch_sec + tz_offset_sec, tm);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

std::string Db::format_iso_local(int64_t epoch_sec, int tz_offset_sec) {
    std::tm tm{};
    time_to_tm_utc(epoch_sec + tz_offset_sec, tm);
    int sign = tz_offset_sec >= 0 ? 1 : -1;
    int abs_off = tz_offset_sec * sign;
    int oh = abs_off / 3600;
    int om = (abs_off % 3600) / 60;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  sign >= 0 ? '+' : '-', oh, om);
    return buf;
}

std::string Db::shift_day(const std::string& day, int delta_days) {
    int64_t sec = parse_day_epoch_utc(day) + static_cast<int64_t>(delta_days) * 86400;
    return format_day(sec, 0);
}

Db::~Db() {
    close();
}

bool Db::open(const std::string& path, std::string* err_out) {
    std::lock_guard<std::mutex> lk(mu_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    path_ = path;
    int rc = sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        if (err_out) *err_out = db_ ? sqlite3_errmsg(db_) : "Failed to open SQLite database";
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }
    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA synchronous=NORMAL;");
    exec("PRAGMA temp_store=MEMORY;");
    return init_schema(err_out);
}

void Db::close() {
    std::lock_guard<std::mutex> lk(mu_);
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

bool Db::exec(const char* sql, std::string* err_out) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        if (err_out) *err_out = errmsg ? errmsg : sqlite3_errmsg(db_);
        if (errmsg) sqlite3_free(errmsg);
        return false;
    }
    return true;
}

bool Db::init_schema(std::string* err_out) {
    const char* ddl = R"SQL(
        CREATE TABLE IF NOT EXISTS minute_stats (
            minute_ts INTEGER PRIMARY KEY,
            keyboard INTEGER NOT NULL DEFAULT 0,
            mouse INTEGER NOT NULL DEFAULT 0,
            scroll INTEGER NOT NULL DEFAULT 0,
            distance_px INTEGER NOT NULL DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS key_daily (
            day TEXT NOT NULL,
            code INTEGER NOT NULL,
            count INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (day, code)
        );
        CREATE TABLE IF NOT EXISTS mouse_btn_daily (
            day TEXT NOT NULL,
            button INTEGER NOT NULL,
            count INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (day, button)
        );
        CREATE TABLE IF NOT EXISTS shortcut_daily (
            day TEXT NOT NULL,
            modifiers INTEGER NOT NULL,
            key INTEGER NOT NULL,
            count INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (day, modifiers, key)
        );
        CREATE TABLE IF NOT EXISTS app_daily (
            day TEXT NOT NULL,
            app_name TEXT NOT NULL,
            keyboard INTEGER NOT NULL DEFAULT 0,
            mouse INTEGER NOT NULL DEFAULT 0,
            scroll INTEGER NOT NULL DEFAULT 0,
            active_sec INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (day, app_name)
        );
        CREATE TABLE IF NOT EXISTS wpm_daily (
            day TEXT PRIMARY KEY,
            peak_wpm REAL NOT NULL DEFAULT 0,
            sum_wpm REAL NOT NULL DEFAULT 0,
            samples INTEGER NOT NULL DEFAULT 0,
            peak_cps REAL NOT NULL DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS focus_sessions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            started_ts INTEGER NOT NULL,
            duration_sec INTEGER NOT NULL,
            keyboard INTEGER NOT NULL DEFAULT 0,
            mouse INTEGER NOT NULL DEFAULT 0,
            label TEXT NOT NULL DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS meta (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
        INSERT OR IGNORE INTO meta(key, value) VALUES('schema_version', '1');
    )SQL";
    return exec(ddl, err_out);
}

bool Db::apply_batch(const BatchDelta& d) {
    if (d.empty()) return true;
    std::lock_guard<std::mutex> lk(mu_);
    if (!db_) return false;

    if (!exec("BEGIN IMMEDIATE;")) return false;
    bool ok = true;

    if (d.keyboard > 0 || d.mouse > 0 || d.scroll > 0 || d.distance_px > 0) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
            "INSERT INTO minute_stats(minute_ts, keyboard, mouse, scroll, distance_px) VALUES(?,?,?,?,?) "
            "ON CONFLICT(minute_ts) DO UPDATE SET "
            "keyboard = keyboard + excluded.keyboard, "
            "mouse = mouse + excluded.mouse, "
            "scroll = scroll + excluded.scroll, "
            "distance_px = distance_px + excluded.distance_px;", -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(st, 1, d.minute_ts);
            sqlite3_bind_int64(st, 2, d.keyboard);
            sqlite3_bind_int64(st, 3, d.mouse);
            sqlite3_bind_int64(st, 4, d.scroll);
            sqlite3_bind_int64(st, 5, d.distance_px);
            if (sqlite3_step(st) != SQLITE_DONE) ok = false;
            sqlite3_finalize(st);
        } else ok = false;
    }

    if (ok && !d.keys.empty()) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
            "INSERT INTO key_daily(day, code, count) VALUES(?,?,?) "
            "ON CONFLICT(day, code) DO UPDATE SET count = count + excluded.count;", -1, &st, nullptr) == SQLITE_OK) {
            for (const auto& kv : d.keys) {
                if (kv.second <= 0) continue;
                sqlite3_reset(st);
                sqlite3_bind_text(st, 1, d.local_day.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int64(st, 2, static_cast<int64_t>(kv.first));
                sqlite3_bind_int64(st, 3, kv.second);
                if (sqlite3_step(st) != SQLITE_DONE) { ok = false; break; }
            }
            sqlite3_finalize(st);
        } else ok = false;
    }

    if (ok && !d.mouse_buttons.empty()) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
            "INSERT INTO mouse_btn_daily(day, button, count) VALUES(?,?,?) "
            "ON CONFLICT(day, button) DO UPDATE SET count = count + excluded.count;", -1, &st, nullptr) == SQLITE_OK) {
            for (const auto& kv : d.mouse_buttons) {
                if (kv.second <= 0) continue;
                sqlite3_reset(st);
                sqlite3_bind_text(st, 1, d.local_day.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int64(st, 2, static_cast<int64_t>(kv.first));
                sqlite3_bind_int64(st, 3, kv.second);
                if (sqlite3_step(st) != SQLITE_DONE) { ok = false; break; }
            }
            sqlite3_finalize(st);
        } else ok = false;
    }

    if (ok && !d.shortcuts.empty()) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
            "INSERT INTO shortcut_daily(day, modifiers, key, count) VALUES(?,?,?,?) "
            "ON CONFLICT(day, modifiers, key) DO UPDATE SET count = count + excluded.count;", -1, &st, nullptr) == SQLITE_OK) {
            for (const auto& kv : d.shortcuts) {
                if (kv.second <= 0) continue;
                sqlite3_reset(st);
                sqlite3_bind_text(st, 1, d.local_day.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int(st, 2, kv.first.first);
                sqlite3_bind_int64(st, 3, static_cast<int64_t>(kv.first.second));
                sqlite3_bind_int64(st, 4, kv.second);
                if (sqlite3_step(st) != SQLITE_DONE) { ok = false; break; }
            }
            sqlite3_finalize(st);
        } else ok = false;
    }

    if (ok && !d.apps.empty()) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
            "INSERT INTO app_daily(day, app_name, keyboard, mouse, scroll, active_sec) VALUES(?,?,?,?,?,?) "
            "ON CONFLICT(day, app_name) DO UPDATE SET "
            "keyboard = keyboard + excluded.keyboard, "
            "mouse = mouse + excluded.mouse, "
            "scroll = scroll + excluded.scroll, "
            "active_sec = active_sec + excluded.active_sec;", -1, &st, nullptr) == SQLITE_OK) {
            for (const auto& kv : d.apps) {
                sqlite3_reset(st);
                sqlite3_bind_text(st, 1, d.local_day.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(st, 2, kv.first.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int64(st, 3, kv.second.keyboard);
                sqlite3_bind_int64(st, 4, kv.second.mouse);
                sqlite3_bind_int64(st, 5, kv.second.scroll);
                sqlite3_bind_int64(st, 6, kv.second.active_sec);
                if (sqlite3_step(st) != SQLITE_DONE) { ok = false; break; }
            }
            sqlite3_finalize(st);
        } else ok = false;
    }

    if (ok && d.has_wpm_sample && d.sample_wpm > 0.0) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
            "INSERT INTO wpm_daily(day, peak_wpm, sum_wpm, samples, peak_cps) VALUES(?,?,?,1,?) "
            "ON CONFLICT(day) DO UPDATE SET "
            "peak_wpm = MAX(peak_wpm, excluded.peak_wpm), "
            "sum_wpm = sum_wpm + excluded.sum_wpm, "
            "samples = samples + 1, "
            "peak_cps = MAX(peak_cps, excluded.peak_cps);", -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, d.local_day.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_double(st, 2, d.sample_wpm);
            sqlite3_bind_double(st, 3, d.sample_wpm);
            sqlite3_bind_double(st, 4, d.sample_cps);
            if (sqlite3_step(st) != SQLITE_DONE) ok = false;
            sqlite3_finalize(st);
        } else ok = false;
    }

    if (ok) {
        ok = exec("COMMIT;");
    } else {
        exec("ROLLBACK;");
    }
    return ok;
}

bool Db::record_focus_session(int64_t started_ts, int duration_sec, int64_t keyboard, int64_t mouse, const std::string& label) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!db_) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
        "INSERT INTO focus_sessions(started_ts, duration_sec, keyboard, mouse, label) VALUES(?,?,?,?,?);",
        -1, &st, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(st, 1, started_ts);
    sqlite3_bind_int(st, 2, duration_sec);
    sqlite3_bind_int64(st, 3, keyboard);
    sqlite3_bind_int64(st, 4, mouse);
    sqlite3_bind_text(st, 5, label.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    return ok;
}

JsonValue Db::build_snapshot(const SnapshotQuery& q) {
    if (q.demo) return build_demo_snapshot(q);

    std::lock_guard<std::mutex> lk(mu_);
    int64_t now_sec = q.now_ms / 1000;
    int64_t now_min = now_sec / 60;
    std::string today = format_day(now_sec, q.tz_offset_sec);
    int64_t today_start_sec = parse_day_epoch_utc(today) - q.tz_offset_sec;
    int64_t today_start_min = today_start_sec / 60;
    int64_t week_start_min = (parse_day_epoch_utc(shift_day(today, -6)) - q.tz_offset_sec) / 60;

    auto sum_window = [&](int64_t min_start, int64_t min_end, int64_t& kb, int64_t& ms, int64_t& sc, int64_t& dist) {
        kb = ms = sc = dist = 0;
        if (!db_) return;
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
            "SELECT COALESCE(SUM(keyboard),0), COALESCE(SUM(mouse),0), COALESCE(SUM(scroll),0), COALESCE(SUM(distance_px),0) "
            "FROM minute_stats WHERE minute_ts >= ? AND minute_ts <= ?;", -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(st, 1, min_start);
            sqlite3_bind_int64(st, 2, min_end);
            if (sqlite3_step(st) == SQLITE_ROW) {
                kb = sqlite3_column_int64(st, 0);
                ms = sqlite3_column_int64(st, 1);
                sc = sqlite3_column_int64(st, 2);
                dist = sqlite3_column_int64(st, 3);
            }
            sqlite3_finalize(st);
        }
    };

    int64_t t_kb = 0, t_ms = 0, t_sc = 0, t_dist = 0;
    sum_window(today_start_min, now_min + 1, t_kb, t_ms, t_sc, t_dist);

    int64_t w_kb = 0, w_ms = 0, w_sc = 0, w_dist = 0;
    sum_window(week_start_min, now_min + 1, w_kb, w_ms, w_sc, w_dist);

    int64_t l_kb = 0, l_ms = 0, l_sc = 0, l_dist = 0;
    if (db_) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
            "SELECT COALESCE(SUM(keyboard),0), COALESCE(SUM(mouse),0), COALESCE(SUM(scroll),0), COALESCE(SUM(distance_px),0) FROM minute_stats;",
            -1, &st, nullptr) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) {
                l_kb = sqlite3_column_int64(st, 0);
                l_ms = sqlite3_column_int64(st, 1);
                l_sc = sqlite3_column_int64(st, 2);
                l_dist = sqlite3_column_int64(st, 3);
            }
            sqlite3_finalize(st);
        }
    }

    // Rolling windows
    int64_t k10 = 0, m10 = 0, s_u = 0, d_u = 0;
    int64_t k1h = 0, m1h = 0, k24 = 0, m24 = 0;
    sum_window(now_min - 9, now_min + 1, k10, m10, s_u, d_u);
    sum_window(now_min - 59, now_min + 1, k1h, m1h, s_u, d_u);
    sum_window(now_min - 1439, now_min + 1, k24, m24, s_u, d_u);

    auto make_counts = [](int64_t k, int64_t m) {
        JsonValue c = JsonValue::object();
        c.set("keyboard", k);
        c.set("mouse", m);
        return c;
    };

    JsonValue windows = JsonValue::array();
    {
        JsonValue w1 = JsonValue::object();
        w1.set("seconds", 60);
        w1.set("approximate", false);
        w1.set("counts", make_counts(q.live.keys_1m, q.live.clicks_1m));
        windows.push(std::move(w1));

        JsonValue w2 = JsonValue::object();
        w2.set("seconds", 600);
        w2.set("approximate", true);
        w2.set("counts", make_counts(k10, m10));
        windows.push(std::move(w2));

        JsonValue w3 = JsonValue::object();
        w3.set("seconds", 3600);
        w3.set("approximate", true);
        w3.set("counts", make_counts(k1h, m1h));
        windows.push(std::move(w3));

        JsonValue w4 = JsonValue::object();
        w4.set("seconds", 86400);
        w4.set("approximate", true);
        w4.set("counts", make_counts(k24, m24));
        windows.push(std::move(w4));
    }

    // Last 24 hourly buckets (for Overview 24h chart)
    int64_t cur_hour_start_sec = (now_sec / 3600) * 3600;
    JsonValue hourly = JsonValue::array();
    std::map<int64_t, std::pair<int64_t, int64_t>> rolling_hour_map;
    if (db_) {
        int64_t min_from = (cur_hour_start_sec - 23 * 3600) / 60;
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
            "SELECT ((minute_ts * 60) / 3600) * 3600 AS h_ts, SUM(keyboard), SUM(mouse) "
            "FROM minute_stats WHERE minute_ts >= ? GROUP BY h_ts;", -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(st, 1, min_from);
            while (sqlite3_step(st) == SQLITE_ROW) {
                rolling_hour_map[sqlite3_column_int64(st, 0)] = {
                    sqlite3_column_int64(st, 1), sqlite3_column_int64(st, 2)
                };
            }
            sqlite3_finalize(st);
        }
    }
    for (int i = 23; i >= 0; --i) {
        int64_t h_start = cur_hour_start_sec - i * 3600;
        int64_t h_end = (i == 0) ? (now_sec + 1) : (h_start + 3600);
        auto it = rolling_hour_map.find(h_start);
        int64_t hk = it != rolling_hour_map.end() ? it->second.first : 0;
        int64_t hm = it != rolling_hour_map.end() ? it->second.second : 0;
        JsonValue slot = JsonValue::object();
        slot.set("start_ms", h_start * 1000);
        slot.set("end_ms", h_end * 1000);
        slot.set("approximate", false);
        slot.set("counts", make_counts(hk, hm));
        hourly.push(std::move(slot));
    }

    // Today's 24 clock hours (local calendar day)
    std::vector<std::pair<int64_t, int64_t>> today_hours(24, {0, 0});
    if (db_) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
            "SELECT CAST(strftime('%H', minute_ts*60 + ?, 'unixepoch') AS INTEGER) AS hr, "
            "SUM(keyboard), SUM(mouse) FROM minute_stats "
            "WHERE minute_ts >= ? AND minute_ts < ? GROUP BY hr;", -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, q.tz_offset_sec);
            sqlite3_bind_int64(st, 2, today_start_min);
            sqlite3_bind_int64(st, 3, today_start_min + 1440);
            while (sqlite3_step(st) == SQLITE_ROW) {
                int hr = sqlite3_column_int(st, 0);
                if (hr >= 0 && hr < 24) {
                    today_hours[hr] = {sqlite3_column_int64(st, 1), sqlite3_column_int64(st, 2)};
                }
            }
            sqlite3_finalize(st);
        }
    }
    JsonValue hours_arr = JsonValue::array();
    for (int h = 0; h < 24; ++h) {
        hours_arr.push(make_counts(today_hours[h].first, today_hours[h].second));
    }

    // All daily aggregates
    struct DayAgg { int64_t k = 0, m = 0, sc = 0, dist = 0; };
    std::map<std::string, DayAgg> all_days;
    if (db_) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
            "SELECT strftime('%Y-%m-%d', minute_ts*60 + ?, 'unixepoch') AS d, "
            "SUM(keyboard), SUM(mouse), SUM(scroll), SUM(distance_px) "
            "FROM minute_stats GROUP BY d ORDER BY d ASC;", -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, q.tz_offset_sec);
            while (sqlite3_step(st) == SQLITE_ROW) {
                const char* ds = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
                if (ds) {
                    all_days[ds] = {
                        sqlite3_column_int64(st, 1),
                        sqlite3_column_int64(st, 2),
                        sqlite3_column_int64(st, 3),
                        sqlite3_column_int64(st, 4)
                    };
                }
            }
            sqlite3_finalize(st);
        }
    }

    JsonValue history_days = JsonValue::array();
    for (const auto& kv : all_days) {
        JsonValue row = JsonValue::object();
        row.set("day", kv.first);
        row.set("counts", make_counts(kv.second.k, kv.second.m));
        row.set("scroll", kv.second.sc);
        row.set("distance_px", kv.second.dist);
        history_days.push(std::move(row));
    }

    // Continuous 90-day array for Overview Trend chart
    JsonValue days90 = JsonValue::array();
    for (int offset = -89; offset <= 0; ++offset) {
        std::string d = shift_day(today, offset);
        auto it = all_days.find(d);
        JsonValue row = JsonValue::object();
        row.set("day", d);
        row.set("counts", it != all_days.end() ? make_counts(it->second.k, it->second.m) : make_counts(0, 0));
        days90.push(std::move(row));
    }

    // Streak calculation (consecutive days meeting >= 1 event or goal)
    int streak_days = 0;
    std::string check_day = today;
    auto check_active = [&](const std::string& d) {
        auto it = all_days.find(d);
        return it != all_days.end() && (it->second.k + it->second.m) > 0;
    };
    if (!check_active(check_day)) {
        check_day = shift_day(today, -1);
    }
    while (check_active(check_day) && streak_days < 3650) {
        ++streak_days;
        check_day = shift_day(check_day, -1);
    }

    // Keyboard physical key counts (lifetime)
    JsonValue physical_keys = JsonValue::array();
    if (db_) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT code, SUM(count) FROM key_daily GROUP BY code;", -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                JsonValue row = JsonValue::object();
                row.set("code", sqlite3_column_int64(st, 0));
                row.set("count", sqlite3_column_int64(st, 1));
                physical_keys.push(std::move(row));
            }
            sqlite3_finalize(st);
        }
    }
    JsonValue kb_stats = JsonValue::object();
    kb_stats.set("history_available", false);
    kb_stats.set("physical", std::move(physical_keys));

    // Mouse buttons lifetime
    int64_t btn[6] = {0, 0, 0, 0, 0, 0};
    if (db_) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT button, SUM(count) FROM mouse_btn_daily GROUP BY button;", -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                int b = sqlite3_column_int(st, 0);
                int64_t c = sqlite3_column_int64(st, 1);
                if (b >= 1 && b <= 5) btn[b] = c;
                else btn[0] += c;
            }
            sqlite3_finalize(st);
        }
    }
    JsonValue mouse_btns = JsonValue::object();
    mouse_btns.set("left", btn[1]);
    mouse_btns.set("right", btn[2]);
    mouse_btns.set("middle", btn[3]);
    mouse_btns.set("side_back", btn[4]);
    mouse_btns.set("side_forward", btn[5]);
    mouse_btns.set("other", btn[0]);

    // Shortcuts lifetime
    JsonValue shortcuts = JsonValue::array();
    if (db_ && q.include_shortcuts) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
            "SELECT modifiers, key, SUM(count) AS c FROM shortcut_daily "
            "GROUP BY modifiers, key ORDER BY c DESC LIMIT 2000;", -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                JsonValue row = JsonValue::object();
                row.set("platform", "windows");
                row.set("modifiers", sqlite3_column_int(st, 0));
                row.set("key", sqlite3_column_int64(st, 1));
                row.set("count", sqlite3_column_int64(st, 2));
                shortcuts.push(std::move(row));
            }
            sqlite3_finalize(st);
        }
    }

    // Per-app breakdown (today & lifetime)
    auto query_apps = [&](const char* sql, const char* bind_day) {
        JsonValue arr = JsonValue::array();
        if (!db_ || !q.include_apps) return arr;
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) == SQLITE_OK) {
            if (bind_day) sqlite3_bind_text(st, 1, bind_day, -1, SQLITE_STATIC);
            while (sqlite3_step(st) == SQLITE_ROW) {
                const char* name = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
                JsonValue row = JsonValue::object();
                row.set("app", name ? name : "unknown");
                row.set("keyboard", sqlite3_column_int64(st, 1));
                row.set("mouse", sqlite3_column_int64(st, 2));
                row.set("scroll", sqlite3_column_int64(st, 3));
                row.set("active_sec", sqlite3_column_int64(st, 4));
                arr.push(std::move(row));
            }
            sqlite3_finalize(st);
        }
        return arr;
    };
    JsonValue apps_today = query_apps(
        "SELECT app_name, keyboard, mouse, scroll, active_sec FROM app_daily "
        "WHERE day = ? ORDER BY (keyboard + mouse) DESC LIMIT 40;", today.c_str());
    JsonValue apps_lifetime = query_apps(
        "SELECT app_name, SUM(keyboard) AS k, SUM(mouse) AS m, SUM(scroll), SUM(active_sec) "
        "FROM app_daily GROUP BY app_name ORDER BY (k + m) DESC LIMIT 50;", nullptr);

    // WPM stats today & peak
    double today_peak_wpm = 0.0, today_avg_wpm = 0.0, all_peak_wpm = 0.0;
    if (db_) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
            "SELECT peak_wpm, CASE WHEN samples>0 THEN sum_wpm/samples ELSE 0 END FROM wpm_daily WHERE day = ?;",
            -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, today.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(st) == SQLITE_ROW) {
                today_peak_wpm = sqlite3_column_double(st, 0);
                today_avg_wpm = sqlite3_column_double(st, 1);
            }
            sqlite3_finalize(st);
        }
        if (sqlite3_prepare_v2(db_, "SELECT COALESCE(MAX(peak_wpm),0) FROM wpm_daily;", -1, &st, nullptr) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) all_peak_wpm = sqlite3_column_double(st, 0);
            sqlite3_finalize(st);
        }
    }

    // Focus sessions recent
    JsonValue focus_arr = JsonValue::array();
    if (db_) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
            "SELECT started_ts, duration_sec, keyboard, mouse, label FROM focus_sessions ORDER BY id DESC LIMIT 20;",
            -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                JsonValue row = JsonValue::object();
                row.set("started_ts", sqlite3_column_int64(st, 0));
                row.set("duration_sec", sqlite3_column_int(st, 1));
                row.set("keyboard", sqlite3_column_int64(st, 2));
                row.set("mouse", sqlite3_column_int64(st, 3));
                const char* lbl = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
                row.set("label", lbl ? lbl : "");
                focus_arr.push(std::move(row));
            }
            sqlite3_finalize(st);
        }
    }

    // Optional Report detail
    JsonValue report_obj(nullptr);
    if (!q.report_period.empty() && !q.report_day.empty() && db_) {
        bool weekly = (q.report_period == "weekly");
        std::string r_start = weekly ? (!q.report_start.empty() ? q.report_start : shift_day(q.report_day, -6)) : q.report_day;
        int64_t r_min_start = (parse_day_epoch_utc(r_start) - q.tz_offset_sec) / 60;
        int64_t r_min_end = (parse_day_epoch_utc(shift_day(q.report_day, 1)) - q.tz_offset_sec) / 60;

        JsonValue r_hours = JsonValue::array();
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_,
            "SELECT strftime('%Y-%m-%d', minute_ts*60 + ?, 'unixepoch') AS d, "
            "CAST(strftime('%H', minute_ts*60 + ?, 'unixepoch') AS INTEGER) AS hr, "
            "SUM(keyboard), SUM(mouse) FROM minute_stats "
            "WHERE minute_ts >= ? AND minute_ts < ? GROUP BY d, hr ORDER BY d ASC, hr ASC;",
            -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, q.tz_offset_sec);
            sqlite3_bind_int(st, 2, q.tz_offset_sec);
            sqlite3_bind_int64(st, 3, r_min_start);
            sqlite3_bind_int64(st, 4, r_min_end);
            while (sqlite3_step(st) == SQLITE_ROW) {
                const char* d = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
                if (!d) continue;
                JsonValue row = JsonValue::array();
                row.push(std::string(d));
                row.push(sqlite3_column_int(st, 1));
                row.push(sqlite3_column_int64(st, 2));
                row.push(sqlite3_column_int64(st, 3));
                r_hours.push(std::move(row));
            }
            sqlite3_finalize(st);
        }

        JsonValue r_ranking = JsonValue::array();
        if (sqlite3_prepare_v2(db_,
            "SELECT modifiers, key, SUM(count) AS c FROM shortcut_daily "
            "WHERE day >= ? AND day <= ? GROUP BY modifiers, key ORDER BY c DESC LIMIT 10;",
            -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, r_start.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 2, q.report_day.c_str(), -1, SQLITE_STATIC);
            while (sqlite3_step(st) == SQLITE_ROW) {
                JsonValue row = JsonValue::object();
                row.set("platform", "windows");
                row.set("modifiers", sqlite3_column_int(st, 0));
                row.set("key", sqlite3_column_int64(st, 1));
                row.set("count", sqlite3_column_int64(st, 2));
                r_ranking.push(std::move(row));
            }
            sqlite3_finalize(st);
        }

        report_obj = JsonValue::object();
        report_obj.set("day", q.report_day);
        report_obj.set("start", r_start);
        report_obj.set("period", q.report_period);
        report_obj.set("timezone", q.prefs.timezone);
        report_obj.set("hours", std::move(r_hours));
        report_obj.set("ranking", std::move(r_ranking));
    }

    // Distance meters conversion
    double ppi = q.prefs.dpi_ppi > 0 ? static_cast<double>(q.prefs.dpi_ppi) : 96.0;
    double px_to_meters = 0.0254 / ppi;

    JsonValue cap = JsonValue::object();
    cap.set("listening", q.capture.listening);
    cap.set("permission_needed", false);
    cap.set("error", q.capture.error ? JsonValue(q.capture.error_message) : JsonValue(nullptr));
    cap.set("error_code", q.capture.error_code.empty() ? JsonValue(nullptr) : JsonValue(q.capture.error_code));

    JsonValue snap = JsonValue::object();
    snap.set("now", format_iso_local(now_sec, q.tz_offset_sec));
    snap.set("timezone", q.prefs.timezone);
    snap.set("today_period", q.prefs.today_period);
    snap.set("paused", q.capture.paused);
    snap.set("demo", false);
    snap.set("source_mode", false);
    snap.set("setup_pending", false);
    snap.set("auto_start", q.auto_start);
    snap.set("auto_start_available", q.auto_start_available);
    snap.set("database_path", path_);
    snap.set("database_mode", q.database_mode);
    snap.set("capture", std::move(cap));
    snap.set("today", make_counts(t_kb, t_ms));
    snap.set("week", make_counts(w_kb, w_ms));
    snap.set("lifetime", make_counts(l_kb, l_ms));
    snap.set("windows", std::move(windows));
    snap.set("hourly", std::move(hourly));
    snap.set("hours", std::move(hours_arr));
    snap.set("days", std::move(days90));
    snap.set("history_days", std::move(history_days));
    snap.set("keyboard_stats", std::move(kb_stats));
    snap.set("mouse_buttons", std::move(mouse_btns));
    snap.set("shortcuts", std::move(shortcuts));
    snap.set("report", std::move(report_obj));

    // TypeCount extensions
    snap.set("live_wpm", std::round(q.live.live_wpm * 10.0) / 10.0);
    snap.set("live_cpm", std::round(q.live.live_cpm * 10.0) / 10.0);
    snap.set("live_cps", std::round(q.live.live_cps * 10.0) / 10.0);
    snap.set("today_peak_wpm", std::round(today_peak_wpm * 10.0) / 10.0);
    snap.set("today_avg_wpm", std::round(today_avg_wpm * 10.0) / 10.0);
    snap.set("all_peak_wpm", std::round(all_peak_wpm * 10.0) / 10.0);
    snap.set("today_scroll", t_sc);
    snap.set("lifetime_scroll", l_sc);
    snap.set("today_distance_m", std::round(t_dist * px_to_meters * 10.0) / 10.0);
    snap.set("lifetime_distance_m", std::round(l_dist * px_to_meters * 10.0) / 10.0);
    snap.set("streak_days", streak_days);
    snap.set("goal_target", q.prefs.daily_goal);
    int64_t today_total = (q.prefs.today_period == "rolling_24h") ? (k24 + m24) : (t_kb + t_ms);
    double goal_pct = q.prefs.daily_goal > 0 ? std::min(100.0, 100.0 * today_total / static_cast<double>(q.prefs.daily_goal)) : 0.0;
    snap.set("goal_progress", std::round(goal_pct * 10.0) / 10.0);
    snap.set("break_elapsed_sec", q.live.break_elapsed_sec);
    snap.set("break_interval_sec", static_cast<int64_t>(q.prefs.break_interval_min) * 60);
    snap.set("break_due", q.live.break_due);
    snap.set("apps_today", std::move(apps_today));
    snap.set("apps_lifetime", std::move(apps_lifetime));
    snap.set("focus_sessions", std::move(focus_arr));

    return snap;
}

JsonValue Db::query_history_hours(const std::string& start_day, const std::string& end_day, int tz_offset_sec) {
    std::lock_guard<std::mutex> lk(mu_);
    JsonValue rows = JsonValue::array();
    if (!db_) return rows;

    int64_t min_start = (parse_day_epoch_utc(start_day) - tz_offset_sec) / 60;
    int64_t min_end = (parse_day_epoch_utc(shift_day(end_day, 1)) - tz_offset_sec) / 60;

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT strftime('%Y-%m-%d', minute_ts*60 + ?, 'unixepoch') AS d, "
        "CAST(strftime('%H', minute_ts*60 + ?, 'unixepoch') AS INTEGER) AS hr, "
        "SUM(keyboard), SUM(mouse) FROM minute_stats "
        "WHERE minute_ts >= ? AND minute_ts < ? GROUP BY d, hr ORDER BY d ASC, hr ASC;",
        -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, tz_offset_sec);
        sqlite3_bind_int(st, 2, tz_offset_sec);
        sqlite3_bind_int64(st, 3, min_start);
        sqlite3_bind_int64(st, 4, min_end);
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char* d = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
            if (!d) continue;
            JsonValue row = JsonValue::array();
            row.push(std::string(d));
            row.push(sqlite3_column_int(st, 1));
            row.push(sqlite3_column_int64(st, 2));
            row.push(sqlite3_column_int64(st, 3));
            rows.push(std::move(row));
        }
        sqlite3_finalize(st);
    }
    return rows;
}

JsonValue Db::build_demo_snapshot(const SnapshotQuery& q) const {
    int64_t now_sec = q.now_ms / 1000;
    std::string today = format_day(now_sec, q.tz_offset_sec);
    auto make_counts = [](int64_t k, int64_t m) {
        JsonValue c = JsonValue::object();
        c.set("keyboard", k);
        c.set("mouse", m);
        return c;
    };

    JsonValue history_days = JsonValue::array();
    JsonValue days90 = JsonValue::array();
    int64_t t_kb = 0, t_ms = 0, w_kb = 0, w_ms = 0, l_kb = 0, l_ms = 0;

    for (int i = 179; i >= 0; --i) {
        std::string d = shift_day(today, -i);
        double wave = 0.65 + 0.35 * std::sin(i * 0.42);
        bool weekend = (i % 7 == 1 || i % 7 == 2);
        int64_t k = static_cast<int64_t>((weekend ? 2400 : 7200) * wave + (i * 137) % 1800);
        int64_t m = static_cast<int64_t>((weekend ? 1400 : 3200) * wave + (i * 89) % 900);
        if (i == 47 || i == 93) { k = 0; m = 0; }
        l_kb += k; l_ms += m;
        if (i < 7) { w_kb += k; w_ms += m; }
        if (i == 0) { t_kb = k; t_ms = m; }

        JsonValue hd = JsonValue::object();
        hd.set("day", d);
        hd.set("counts", make_counts(k, m));
        history_days.push(hd);
        if (i < 90) days90.push(std::move(hd));
    }

    JsonValue hours = JsonValue::array();
    JsonValue hourly = JsonValue::array();
    int64_t cur_h = (now_sec / 3600) * 3600;
    for (int h = 0; h < 24; ++h) {
        int64_t hk = (h >= 8 && h <= 22) ? (220 + ((h * 97) % 520)) : 25;
        int64_t hm = (h >= 8 && h <= 22) ? (110 + ((h * 53) % 240)) : 12;
        hours.push(make_counts(hk, hm));

        JsonValue slot = JsonValue::object();
        int64_t hs = cur_h - (23 - h) * 3600;
        slot.set("start_ms", hs * 1000);
        slot.set("end_ms", (h == 23 ? now_sec + 1 : hs + 3600) * 1000);
        slot.set("approximate", false);
        slot.set("counts", make_counts(hk, hm));
        hourly.push(std::move(slot));
    }

    JsonValue windows = JsonValue::array();
    for (auto [sec, k, m] : std::vector<std::tuple<int, int64_t, int64_t>>{
        {60, 142, 48}, {600, 1180, 410}, {3600, 4920, 1840}, {86400, t_kb, t_ms}
    }) {
        JsonValue w = JsonValue::object();
        w.set("seconds", sec);
        w.set("approximate", false);
        w.set("counts", make_counts(k, m));
        windows.push(std::move(w));
    }

    const std::vector<std::pair<uint32_t, int64_t>> demo_keys = {
        {49, 148200}, {14, 92400}, {0, 68100}, {17, 64200}, {34, 58900}, {31, 56300},
        {45, 54100}, {1, 51200}, {15, 49800}, {37, 36400}, {2, 34200}, {8, 31000},
        {51, 44200}, {36, 29400}, {59, 41500}, {56, 38200}, {48, 19800}, {13, 21400},
        {9, 18900}, {46, 22400}, {35, 19300}, {4, 24100}, {5, 17200}, {3, 18800},
        {53, 8400}, {123, 9800}, {124, 11200}, {125, 12100}, {126, 9400}
    };
    JsonValue phys = JsonValue::array();
    for (auto [code, cnt] : demo_keys) {
        JsonValue r = JsonValue::object();
        r.set("code", static_cast<int64_t>(code));
        r.set("count", cnt);
        phys.push(std::move(r));
    }
    JsonValue kb_stats = JsonValue::object();
    kb_stats.set("history_available", false);
    kb_stats.set("physical", std::move(phys));

    JsonValue mbtns = JsonValue::object();
    mbtns.set("left", 312400);
    mbtns.set("right", 48900);
    mbtns.set("middle", 14200);
    mbtns.set("side_back", 19800);
    mbtns.set("side_forward", 7400);
    mbtns.set("other", 0);

    JsonValue shortcuts = JsonValue::array();
    for (auto [mods, key, cnt] : std::vector<std::tuple<int, int, int64_t>>{
        {1, 8, 8420}, {1, 9, 6910}, {1, 1, 5340}, {1, 6, 3890},
        {1, 3, 2760}, {1, 17, 2190}, {1, 13, 1840}, {2, 48, 4120}
    }) {
        JsonValue s = JsonValue::object();
        s.set("platform", "windows");
        s.set("modifiers", mods);
        s.set("key", key);
        s.set("count", cnt);
        shortcuts.push(std::move(s));
    }

    JsonValue apps = JsonValue::array();
    for (auto [name, k, m, sc, sec] : std::vector<std::tuple<const char*, int64_t, int64_t, int64_t, int64_t>>{
        {"Code.exe", 4420, 980, 620, 7840},
        {"chrome.exe", 1840, 1520, 1140, 5420},
        {"WindowsTerminal.exe", 1290, 210, 180, 2310},
        {"Telegram.exe", 860, 340, 290, 1620},
        {"explorer.exe", 210, 480, 90, 890}
    }) {
        JsonValue a = JsonValue::object();
        a.set("app", name);
        a.set("keyboard", k);
        a.set("mouse", m);
        a.set("scroll", sc);
        a.set("active_sec", sec);
        apps.push(std::move(a));
    }

    JsonValue cap = JsonValue::object();
    cap.set("listening", true);
    cap.set("permission_needed", false);
    cap.set("error", nullptr);
    cap.set("error_code", nullptr);

    JsonValue snap = JsonValue::object();
    snap.set("now", format_iso_local(now_sec, q.tz_offset_sec));
    snap.set("timezone", q.prefs.timezone);
    snap.set("today_period", q.prefs.today_period);
    snap.set("paused", q.capture.paused);
    snap.set("demo", true);
    snap.set("source_mode", false);
    snap.set("setup_pending", false);
    snap.set("auto_start", q.auto_start);
    snap.set("auto_start_available", true);
    snap.set("database_path", "Demo mode");
    snap.set("database_mode", "demo");
    snap.set("capture", std::move(cap));
    snap.set("today", make_counts(t_kb, t_ms));
    snap.set("week", make_counts(w_kb, w_ms));
    snap.set("lifetime", make_counts(l_kb, l_ms));
    snap.set("windows", std::move(windows));
    snap.set("hourly", std::move(hourly));
    snap.set("hours", std::move(hours));
    snap.set("days", std::move(days90));
    snap.set("history_days", std::move(history_days));
    snap.set("keyboard_stats", std::move(kb_stats));
    snap.set("mouse_buttons", std::move(mbtns));
    snap.set("shortcuts", std::move(shortcuts));
    snap.set("report", nullptr);
    snap.set("live_wpm", 68.4);
    snap.set("live_cpm", 342.0);
    snap.set("live_cps", 4.6);
    snap.set("today_peak_wpm", 96.2);
    snap.set("today_avg_wpm", 61.5);
    snap.set("all_peak_wpm", 114.8);
    snap.set("today_scroll", 2320);
    snap.set("lifetime_scroll", 184200);
    snap.set("today_distance_m", 148.6);
    snap.set("lifetime_distance_m", 19420.5);
    snap.set("streak_days", 14);
    snap.set("goal_target", q.prefs.daily_goal);
    snap.set("goal_progress", 94.5);
    snap.set("break_elapsed_sec", 1420);
    snap.set("break_interval_sec", static_cast<int64_t>(q.prefs.break_interval_min) * 60);
    snap.set("break_due", false);
    snap.set("apps_today", apps);
    snap.set("apps_lifetime", std::move(apps));
    snap.set("focus_sessions", JsonValue::array());
    return snap;
}

bool Db::export_csv(const std::string& out_path, int tz_offset_sec, std::string* err_out) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!db_) {
        if (err_out) *err_out = "Database not open";
        return false;
    }
    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (err_out) *err_out = "Cannot open export file for writing";
        return false;
    }
    // UTF-8 BOM for seamless Excel display
    f << "\xEF\xBB\xBF";
    f << "date,keyboard,mouse,total,scroll_events,distance_meters,peak_wpm,avg_wpm\n";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT strftime('%Y-%m-%d', m.minute_ts*60 + ?, 'unixepoch') AS d, "
        "SUM(m.keyboard), SUM(m.mouse), SUM(m.scroll), SUM(m.distance_px), "
        "COALESCE(w.peak_wpm, 0), CASE WHEN COALESCE(w.samples,0)>0 THEN w.sum_wpm/w.samples ELSE 0 END "
        "FROM minute_stats m LEFT JOIN wpm_daily w ON w.day = strftime('%Y-%m-%d', m.minute_ts*60 + ?, 'unixepoch') "
        "GROUP BY d ORDER BY d DESC;", -1, &st, nullptr) != SQLITE_OK) {
        if (err_out) *err_out = sqlite3_errmsg(db_);
        return false;
    }
    sqlite3_bind_int(st, 1, tz_offset_sec);
    sqlite3_bind_int(st, 2, tz_offset_sec);
    double px_to_m = 0.0254 / 96.0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char* d = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        int64_t kb = sqlite3_column_int64(st, 1);
        int64_t ms = sqlite3_column_int64(st, 2);
        int64_t sc = sqlite3_column_int64(st, 3);
        int64_t dist = sqlite3_column_int64(st, 4);
        double pw = sqlite3_column_double(st, 5);
        double aw = sqlite3_column_double(st, 6);
        f << csv_escape(d ? d : "") << "," << kb << "," << ms << "," << (kb + ms) << ","
          << sc << "," << std::fixed << std::setprecision(2) << (dist * px_to_m) << ","
          << std::setprecision(1) << pw << "," << aw << "\n";
    }
    sqlite3_finalize(st);
    return f.good();
}

bool Db::export_json(const std::string& out_path, int tz_offset_sec, std::string* err_out) {
    SnapshotQuery q;
    q.now_ms = static_cast<int64_t>(std::time(nullptr)) * 1000;
    q.tz_offset_sec = tz_offset_sec;
    q.include_shortcuts = true;
    q.include_apps = true;
    JsonValue snap = build_snapshot(q);

    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (err_out) *err_out = "Cannot open JSON file for writing";
        return false;
    }
    std::string body = snap.dump(2);
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
    return f.good();
}

bool Db::backup_to(const std::string& dest_path, std::string* err_out) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!db_) {
        if (err_out) *err_out = "Database not open";
        return false;
    }
    sqlite3* dst = nullptr;
    if (sqlite3_open_v2(dest_path.c_str(), &dst, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        if (err_out) *err_out = dst ? sqlite3_errmsg(dst) : "Failed to create backup db";
        if (dst) sqlite3_close(dst);
        return false;
    }
    sqlite3_backup* bk = sqlite3_backup_init(dst, "main", db_, "main");
    if (!bk) {
        if (err_out) *err_out = sqlite3_errmsg(dst);
        sqlite3_close(dst);
        return false;
    }
    int rc = sqlite3_backup_step(bk, -1);
    sqlite3_backup_finish(bk);
    sqlite3_close(dst);
    if (rc != SQLITE_DONE) {
        if (err_out) *err_out = "Backup step did not complete";
        return false;
    }
    return true;
}

bool Db::create_rotating_backup(const std::string& backup_dir, int keep_count, std::string* created_path, std::string* err_out) {
    ensure_dir_exists(backup_dir);
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    time_to_tm_utc(now, tm);
    char name[96];
    std::snprintf(name, sizeof(name), "typecount-backup-%04d%02d%02d-%02d%02d%02d.sqlite3",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
#ifdef _WIN32
    std::string full = backup_dir + "\\" + name;
#else
    std::string full = backup_dir + "/" + name;
#endif
    if (!backup_to(full, err_out)) return false;
    if (created_path) *created_path = full;

    auto existing = list_backup_files(backup_dir);
    if (static_cast<int>(existing.size()) > keep_count && keep_count > 0) {
        int remove_n = static_cast<int>(existing.size()) - keep_count;
        for (int i = 0; i < remove_n; ++i) {
            std::remove(existing[i].c_str());
        }
    }
    return true;
}

ImportReport Db::import_database(const std::string& source_path, const std::string& backup_dir) {
    ImportReport rep;
    std::string b_err;
    if (!create_rotating_backup(backup_dir, 5, &rep.backup_path, &b_err)) {
        rep.error = "Safety backup failed: " + b_err;
        return rep;
    }

    std::lock_guard<std::mutex> lk(mu_);
    if (!db_) {
        rep.error = "Target database not open";
        return rep;
    }

    sqlite3* src = nullptr;
    if (sqlite3_open_v2(source_path.c_str(), &src, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        rep.error = src ? sqlite3_errmsg(src) : "Cannot open source database";
        if (src) sqlite3_close(src);
        return rep;
    }

    auto has_table = [&](const char* name) {
        sqlite3_stmt* st = nullptr;
        bool found = false;
        if (sqlite3_prepare_v2(src, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?;", -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
            found = (sqlite3_step(st) == SQLITE_ROW);
            sqlite3_finalize(st);
        }
        return found;
    };

    if (!exec("BEGIN IMMEDIATE;")) {
        sqlite3_close(src);
        rep.error = "Cannot start transaction";
        return rep;
    }

    bool ok = true;
    auto merge_minutes = [&](const char* query) {
        sqlite3_stmt* sst = nullptr;
        sqlite3_stmt* chk = nullptr;
        sqlite3_stmt* ins = nullptr;
        if (sqlite3_prepare_v2(src, query, -1, &sst, nullptr) != SQLITE_OK) return false;
        sqlite3_prepare_v2(db_, "SELECT keyboard, mouse, scroll, distance_px FROM minute_stats WHERE minute_ts=?;", -1, &chk, nullptr);
        sqlite3_prepare_v2(db_,
            "INSERT INTO minute_stats(minute_ts, keyboard, mouse, scroll, distance_px) VALUES(?,?,?,?,?) "
            "ON CONFLICT(minute_ts) DO UPDATE SET "
            "keyboard = MAX(keyboard, excluded.keyboard), "
            "mouse = MAX(mouse, excluded.mouse), "
            "scroll = MAX(scroll, excluded.scroll), "
            "distance_px = MAX(distance_px, excluded.distance_px);", -1, &ins, nullptr);

        while (sqlite3_step(sst) == SQLITE_ROW) {
            int64_t m = sqlite3_column_int64(sst, 0);
            int64_t k = sqlite3_column_int64(sst, 1);
            int64_t ms = sqlite3_column_int64(sst, 2);
            int64_t sc = sqlite3_column_int64(sst, 3);
            int64_t dist = sqlite3_column_int64(sst, 4);

            int64_t cur_k = 0, cur_m = 0;
            sqlite3_reset(chk);
            sqlite3_bind_int64(chk, 1, m);
            if (sqlite3_step(chk) == SQLITE_ROW) {
                cur_k = sqlite3_column_int64(chk, 0);
                cur_m = sqlite3_column_int64(chk, 1);
                if (cur_k > 0 || cur_m > 0) rep.overlapped_minutes++;
            }
            int64_t nk = std::max(cur_k, k);
            int64_t nm = std::max(cur_m, ms);
            rep.added_ops += (nk - cur_k) + (nm - cur_m);

            sqlite3_reset(ins);
            sqlite3_bind_int64(ins, 1, m);
            sqlite3_bind_int64(ins, 2, nk);
            sqlite3_bind_int64(ins, 3, nm);
            sqlite3_bind_int64(ins, 4, sc);
            sqlite3_bind_int64(ins, 5, dist);
            if (sqlite3_step(ins) != SQLITE_DONE) { ok = false; break; }
        }
        sqlite3_finalize(sst);
        sqlite3_finalize(chk);
        sqlite3_finalize(ins);
        return ok;
    };

    if (has_table("minute_stats")) {
        ok = merge_minutes("SELECT minute_ts, keyboard, mouse, scroll, distance_px FROM minute_stats;");
    } else if (has_table("minute_counts")) {
        // Support importing ClickPulse minute_counts tables if schema matches
        ok = merge_minutes("SELECT minute, keyboard, mouse, 0, 0 FROM minute_counts;");
    } else {
        ok = false;
        rep.error = "Selected file is not a compatible TypeCount/ClickPulse database.";
    }

    if (ok && has_table("key_daily")) {
        sqlite3_stmt* sst = nullptr;
        sqlite3_stmt* ins = nullptr;
        if (sqlite3_prepare_v2(src, "SELECT day, code, count FROM key_daily;", -1, &sst, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db_, "INSERT INTO key_daily(day,code,count) VALUES(?,?,?) ON CONFLICT(day,code) DO UPDATE SET count=MAX(count, excluded.count);", -1, &ins, nullptr) == SQLITE_OK) {
            while (sqlite3_step(sst) == SQLITE_ROW) {
                sqlite3_reset(ins);
                sqlite3_bind_text(ins, 1, reinterpret_cast<const char*>(sqlite3_column_text(sst, 0)), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(ins, 2, sqlite3_column_int64(sst, 1));
                sqlite3_bind_int64(ins, 3, sqlite3_column_int64(sst, 2));
                sqlite3_step(ins);
            }
        }
        if (sst) sqlite3_finalize(sst);
        if (ins) sqlite3_finalize(ins);
    }

    if (ok && has_table("mouse_btn_daily")) {
        sqlite3_stmt* sst = nullptr;
        sqlite3_stmt* ins = nullptr;
        if (sqlite3_prepare_v2(src, "SELECT day, button, count FROM mouse_btn_daily;", -1, &sst, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db_, "INSERT INTO mouse_btn_daily(day,button,count) VALUES(?,?,?) ON CONFLICT(day,button) DO UPDATE SET count=MAX(count, excluded.count);", -1, &ins, nullptr) == SQLITE_OK) {
            while (sqlite3_step(sst) == SQLITE_ROW) {
                sqlite3_reset(ins);
                sqlite3_bind_text(ins, 1, reinterpret_cast<const char*>(sqlite3_column_text(sst, 0)), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(ins, 2, sqlite3_column_int(sst, 1));
                sqlite3_bind_int64(ins, 3, sqlite3_column_int64(sst, 2));
                sqlite3_step(ins);
            }
        }
        if (sst) sqlite3_finalize(sst);
        if (ins) sqlite3_finalize(ins);
    }

    if (ok && has_table("shortcut_daily")) {
        sqlite3_stmt* sst = nullptr;
        sqlite3_stmt* ins = nullptr;
        if (sqlite3_prepare_v2(src, "SELECT day, modifiers, key, count FROM shortcut_daily;", -1, &sst, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db_, "INSERT INTO shortcut_daily(day,modifiers,key,count) VALUES(?,?,?,?) ON CONFLICT(day,modifiers,key) DO UPDATE SET count=MAX(count, excluded.count);", -1, &ins, nullptr) == SQLITE_OK) {
            while (sqlite3_step(sst) == SQLITE_ROW) {
                sqlite3_reset(ins);
                sqlite3_bind_text(ins, 1, reinterpret_cast<const char*>(sqlite3_column_text(sst, 0)), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(ins, 2, sqlite3_column_int(sst, 1));
                sqlite3_bind_int64(ins, 3, sqlite3_column_int64(sst, 2));
                sqlite3_bind_int64(ins, 4, sqlite3_column_int64(sst, 3));
                sqlite3_step(ins);
            }
        }
        if (sst) sqlite3_finalize(sst);
        if (ins) sqlite3_finalize(ins);
    }

    if (ok && has_table("app_daily")) {
        sqlite3_stmt* sst = nullptr;
        sqlite3_stmt* ins = nullptr;
        if (sqlite3_prepare_v2(src, "SELECT day, app_name, keyboard, mouse, scroll, active_sec FROM app_daily;", -1, &sst, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db_,
                "INSERT INTO app_daily(day,app_name,keyboard,mouse,scroll,active_sec) VALUES(?,?,?,?,?,?) "
                "ON CONFLICT(day,app_name) DO UPDATE SET "
                "keyboard=MAX(keyboard,excluded.keyboard), mouse=MAX(mouse,excluded.mouse), "
                "scroll=MAX(scroll,excluded.scroll), active_sec=MAX(active_sec,excluded.active_sec);", -1, &ins, nullptr) == SQLITE_OK) {
            while (sqlite3_step(sst) == SQLITE_ROW) {
                sqlite3_reset(ins);
                sqlite3_bind_text(ins, 1, reinterpret_cast<const char*>(sqlite3_column_text(sst, 0)), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins, 2, reinterpret_cast<const char*>(sqlite3_column_text(sst, 1)), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(ins, 3, sqlite3_column_int64(sst, 2));
                sqlite3_bind_int64(ins, 4, sqlite3_column_int64(sst, 3));
                sqlite3_bind_int64(ins, 5, sqlite3_column_int64(sst, 4));
                sqlite3_bind_int64(ins, 6, sqlite3_column_int64(sst, 5));
                sqlite3_step(ins);
            }
        }
        if (sst) sqlite3_finalize(sst);
        if (ins) sqlite3_finalize(ins);
    }

    if (ok) {
        ok = exec("COMMIT;");
    } else {
        exec("ROLLBACK;");
    }
    sqlite3_close(src);
    rep.ok = ok;
    return rep;
}

} // namespace tc
