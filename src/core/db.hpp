#pragma once

#include "json.hpp"
#include "key_mapper.hpp"
#include "preferences.hpp"
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;

namespace tc {

struct AppDelta {
    int64_t keyboard = 0;
    int64_t mouse = 0;
    int64_t scroll = 0;
    int64_t active_sec = 0;
};

struct BatchDelta {
    int64_t minute_ts = 0; // Unix seconds / 60
    std::string local_day; // YYYY-MM-DD
    int64_t keyboard = 0;
    int64_t mouse = 0;
    int64_t scroll = 0;
    int64_t distance_px = 0;
    int64_t active_sec = 0;
    std::map<uint32_t, int64_t> keys;
    std::map<uint32_t, int64_t> mouse_buttons;
    std::map<std::pair<uint8_t, uint32_t>, int64_t> shortcuts;
    std::map<std::string, AppDelta> apps;
    double sample_wpm = 0.0;
    double sample_cps = 0.0;
    bool has_wpm_sample = false;

    bool empty() const {
        return keyboard == 0 && mouse == 0 && scroll == 0 && distance_px == 0 &&
               active_sec == 0 && keys.empty() && mouse_buttons.empty() && shortcuts.empty() &&
               apps.empty() && !has_wpm_sample;
    }
};

struct LiveMetrics {
    int64_t keys_1m = 0;
    int64_t clicks_1m = 0;
    double live_wpm = 0.0;
    double live_cpm = 0.0;
    double live_cps = 0.0;
    int64_t break_elapsed_sec = 0;
    bool break_due = false;
    int ergo_strain_level = 0; // 0=normal, 1=elevated, 2=high
    int64_t ergo_10m_ops = 0;
};

struct CaptureStatusInfo {
    bool listening = true;
    bool paused = false;
    bool error = false;
    std::string error_code;
    std::string error_message;
};

struct ImportReport {
    bool ok = false;
    int64_t added_ops = 0;
    int64_t overlapped_minutes = 0;
    std::string backup_path;
    std::string error;
};

struct SnapshotQuery {
    int64_t now_ms = 0;
    int tz_offset_sec = 0;
    bool include_shortcuts = true;
    bool include_apps = true;
    bool demo = false;
    std::string report_period;     // "daily" | "weekly" | ""
    std::string report_day;        // YYYY-MM-DD
    std::string report_start;      // YYYY-MM-DD
    Preferences prefs;
    CaptureStatusInfo capture;
    LiveMetrics live;
    bool auto_start = false;
    bool auto_start_available = true;
    std::string database_mode = "default";
};

class Db {
public:
    Db() = default;
    ~Db();

    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    bool open(const std::string& path, std::string* err_out = nullptr);
    void close();
    bool is_open() const { return db_ != nullptr; }
    const std::string& path() const { return path_; }

    bool apply_batch(const BatchDelta& delta);
    bool record_focus_session(int64_t started_ts, int duration_sec, int64_t keyboard, int64_t mouse, const std::string& label);
    bool record_typing_test(int64_t ts, int duration_sec, double wpm, double raw_cpm, double accuracy, int errors, int chars, const std::string& lang);
    std::string get_meta(const std::string& key, const std::string& def = "");
    bool set_meta(const std::string& key, const std::string& val);

    JsonValue build_snapshot(const SnapshotQuery& q);
    JsonValue query_history_hours(const std::string& start_day, const std::string& end_day, int tz_offset_sec);
    JsonValue query_history_hours_demo(const std::string& start_day, const std::string& end_day) const;
    JsonValue build_demo_snapshot(const SnapshotQuery& q) const;

    bool export_csv(const std::string& out_path, int tz_offset_sec, std::string* err_out = nullptr);
    bool export_json(const std::string& out_path, int tz_offset_sec, std::string* err_out = nullptr);
    bool backup_to(const std::string& dest_path, std::string* err_out = nullptr);
    bool create_rotating_backup(const std::string& backup_dir, int keep_count, std::string* created_path = nullptr, std::string* err_out = nullptr);
    ImportReport import_database(const std::string& source_path, const std::string& backup_dir);

    // Date helpers (UTC offset based)
    static std::string format_day(int64_t epoch_sec, int tz_offset_sec);
    static std::string format_iso_local(int64_t epoch_sec, int tz_offset_sec);
    static std::string shift_day(const std::string& day, int delta_days);

private:
    bool init_schema(std::string* err_out);
    bool exec(const char* sql, std::string* err_out = nullptr);

    sqlite3* db_ = nullptr;
    std::string path_;
    std::mutex mu_;
};

} // namespace tc
