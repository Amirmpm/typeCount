#pragma once

#include "db.hpp"
#include "key_mapper.hpp"
#include "preferences.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>
#include <string>

namespace tc {

class StatsEngine {
public:
    explicit StatsEngine(Db& db) : db_(db) {}

    void on_key_press(int64_t now_ms, int tz_offset_sec, uint32_t canonical_key,
                      bool is_modifier, const std::optional<ShortcutChord>& shortcut,
                      const std::string& app_name, bool track_apps) {
        std::lock_guard<std::mutex> lk(mu_);
        ensure_bucket(now_ms, tz_offset_sec);

        pending_.keyboard++;
        pending_.keys[canonical_key]++;
        recent_keys_ms_.push_back(now_ms);
        prune_recent_locked(now_ms);

        if (shortcut.has_value()) {
            pending_.shortcuts[{shortcut->modifiers, shortcut->key}]++;
        }

        if (track_apps && !app_name.empty()) {
            auto& app = pending_.apps[app_name];
            app.keyboard++;
            bump_app_active_locked(now_ms, app_name, app);
        }

        // Real-time WPM/CPM from non-modifier keys (no shortcut chords, no key content stored)
        if (!is_modifier && !shortcut.has_value()) {
            if (!typing_burst_.empty() && (now_ms - typing_burst_.back() > 2500)) {
                typing_burst_.clear();
            }
            typing_burst_.push_back(now_ms);
            while (!typing_burst_.empty() && (now_ms - typing_burst_.front() > 6000)) {
                typing_burst_.pop_front();
            }
            if (typing_burst_.size() >= 8) {
                double span_s = std::max(2.0, (now_ms - typing_burst_.front()) / 1000.0);
                double wpm = (static_cast<double>(typing_burst_.size()) / span_s) * 12.0; // (N / 5 chars) * (60 / span_s)
                if (wpm <= 260.0) {
                    pending_.has_wpm_sample = true;
                    pending_.sample_wpm = std::max(pending_.sample_wpm, wpm);
                }
            }
        }

        touch_break_locked(now_ms);
        dirty_ = true;
    }

    void on_mouse_click(int64_t now_ms, int tz_offset_sec, uint32_t button,
                        const std::string& app_name, bool track_apps) {
        std::lock_guard<std::mutex> lk(mu_);
        ensure_bucket(now_ms, tz_offset_sec);

        pending_.mouse++;
        pending_.mouse_buttons[button]++;
        recent_clicks_ms_.push_back(now_ms);
        prune_recent_locked(now_ms);

        double cps = count_since(recent_clicks_ms_, now_ms - 1000);
        if (cps > 0.0) {
            pending_.has_wpm_sample = true;
            pending_.sample_cps = std::max(pending_.sample_cps, cps);
        }

        if (track_apps && !app_name.empty()) {
            auto& app = pending_.apps[app_name];
            app.mouse++;
            bump_app_active_locked(now_ms, app_name, app);
        }

        touch_break_locked(now_ms);
        dirty_ = true;
    }

    void on_mouse_scroll(int64_t now_ms, int tz_offset_sec, int delta_steps,
                         const std::string& app_name, bool track_apps) {
        if (delta_steps <= 0) delta_steps = 1;
        std::lock_guard<std::mutex> lk(mu_);
        ensure_bucket(now_ms, tz_offset_sec);

        pending_.scroll += delta_steps;
        if (track_apps && !app_name.empty()) {
            auto& app = pending_.apps[app_name];
            app.scroll += delta_steps;
            bump_app_active_locked(now_ms, app_name, app);
        }
        touch_break_locked(now_ms);
        dirty_ = true;
    }

    void on_mouse_move(int64_t now_ms, int tz_offset_sec, int64_t distance_px,
                       const std::string& app_name, bool track_apps) {
        if (distance_px <= 0) return;
        std::lock_guard<std::mutex> lk(mu_);
        ensure_bucket(now_ms, tz_offset_sec);

        pending_.distance_px += distance_px;
        if (track_apps && !app_name.empty()) {
            auto& app = pending_.apps[app_name];
            bump_app_active_locked(now_ms, app_name, app);
        }
        touch_break_locked(now_ms);
        dirty_ = true;
    }

    void reset_break_timer(int64_t now_ms) {
        std::lock_guard<std::mutex> lk(mu_);
        break_start_ms_ = now_ms;
        last_activity_ms_ = now_ms;
    }

    bool flush() {
        BatchDelta copy;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (!dirty_ || pending_.empty()) {
                dirty_ = false;
                return true;
            }
            copy = std::move(pending_);
            pending_ = BatchDelta{};
            dirty_ = false;
        }
        return db_.apply_batch(copy);
    }

    LiveMetrics get_live_metrics(int64_t now_ms, int break_interval_min) {
        std::lock_guard<std::mutex> lk(mu_);
        prune_recent_locked(now_ms);

        LiveMetrics m;
        m.keys_1m = static_cast<int64_t>(recent_keys_ms_.size());
        m.clicks_1m = static_cast<int64_t>(recent_clicks_ms_.size());

        // Live WPM/CPM fades smoothly after 3s of no typing
        while (!typing_burst_.empty() && (now_ms - typing_burst_.front() > 6000)) {
            typing_burst_.pop_front();
        }
        if (!typing_burst_.empty() && (now_ms - typing_burst_.back() <= 3000)) {
            double span_s = std::max(1.5, (now_ms - typing_burst_.front()) / 1000.0);
            if (typing_burst_.size() >= 2) {
                m.live_cpm = std::min(1300.0, (static_cast<double>(typing_burst_.size()) / span_s) * 60.0);
                m.live_wpm = m.live_cpm / 5.0;
            }
        }
        m.live_cps = count_since(recent_clicks_ms_, now_ms - 1000);

        // If user stepped away for >= 90s, reset continuous active streak
        if (last_activity_ms_ > 0 && (now_ms - last_activity_ms_ >= 90000)) {
            break_start_ms_ = 0;
        }
        if (break_start_ms_ > 0) {
            m.break_elapsed_sec = std::max<int64_t>(0, (now_ms - break_start_ms_) / 1000);
            if (break_interval_min > 0 && m.break_elapsed_sec >= static_cast<int64_t>(break_interval_min) * 60) {
                m.break_due = true;
            }
        }
        return m;
    }

    JsonValue snapshot(SnapshotQuery q) {
        flush();
        q.live = get_live_metrics(q.now_ms, q.prefs.break_interval_min);
        return db_.build_snapshot(q);
    }

private:
    void ensure_bucket(int64_t now_ms, int tz_offset_sec) {
        int64_t min_ts = (now_ms / 1000) / 60;
        std::string day = Db::format_day(now_ms / 1000, tz_offset_sec);
        if (dirty_ && (pending_.minute_ts != min_ts || pending_.local_day != day)) {
            BatchDelta copy = std::move(pending_);
            pending_ = BatchDelta{};
            dirty_ = false;
            // Apply previous minute synchronously; tiny single transaction
            db_.apply_batch(copy);
        }
        pending_.minute_ts = min_ts;
        pending_.local_day = day;
    }

    void bump_app_active_locked(int64_t now_ms, const std::string& app_name, AppDelta& app) {
        int64_t cur_sec = now_ms / 1000;
        if (last_app_sec_ != cur_sec || last_app_name_ != app_name) {
            app.active_sec += 1;
            last_app_sec_ = cur_sec;
            last_app_name_ = app_name;
        }
    }

    void touch_break_locked(int64_t now_ms) {
        if (break_start_ms_ == 0 || (last_activity_ms_ > 0 && now_ms - last_activity_ms_ >= 90000)) {
            break_start_ms_ = now_ms;
        }
        last_activity_ms_ = now_ms;
    }

    void prune_recent_locked(int64_t now_ms) {
        int64_t cutoff = now_ms - 60000;
        while (!recent_keys_ms_.empty() && recent_keys_ms_.front() < cutoff) recent_keys_ms_.pop_front();
        while (!recent_clicks_ms_.empty() && recent_clicks_ms_.front() < cutoff) recent_clicks_ms_.pop_front();
    }

    static double count_since(const std::deque<int64_t>& dq, int64_t cutoff) {
        size_t n = 0;
        for (auto it = dq.rbegin(); it != dq.rend(); ++it) {
            if (*it >= cutoff) ++n;
            else break;
        }
        return static_cast<double>(n);
    }

    Db& db_;
    std::mutex mu_;
    BatchDelta pending_;
    bool dirty_ = false;

    std::deque<int64_t> recent_keys_ms_;
    std::deque<int64_t> recent_clicks_ms_;
    std::deque<int64_t> typing_burst_;
    int64_t last_app_sec_ = -1;
    std::string last_app_name_;
    int64_t break_start_ms_ = 0;
    int64_t last_activity_ms_ = 0;
};

} // namespace tc
