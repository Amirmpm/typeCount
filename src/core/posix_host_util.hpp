#pragma once

#include "preferences.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <strings.h>
#include <vector>

namespace tc::posix_util {

inline int64_t now_utc_ms() {
    using namespace std::chrono;
    return static_cast<int64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

inline int timezone_offset_sec_for(const std::string& tz_pref) {
    static std::mutex tz_mu;
    std::lock_guard<std::mutex> lk(tz_mu);

    std::time_t t = std::time(nullptr);
    struct tm local_tm{};
    if (tz_pref.empty() || tz_pref == "system") {
        localtime_r(&t, &local_tm);
        return static_cast<int>(local_tm.tm_gmtoff);
    }

    const char* old_tz = std::getenv("TZ");
    std::string saved_tz = old_tz ? old_tz : "";
    bool had_old = (old_tz != nullptr);

    setenv("TZ", tz_pref.c_str(), 1);
    tzset();
    localtime_r(&t, &local_tm);
    int offset = static_cast<int>(local_tm.tm_gmtoff);

    if (had_old) setenv("TZ", saved_tz.c_str(), 1);
    else unsetenv("TZ");
    tzset();

    return offset;
}

inline std::vector<std::string> system_timezones_list() {
    std::vector<std::string> zones;
    std::filesystem::path root("/usr/share/zoneinfo");
    std::error_code ec;
    if (std::filesystem::exists(root, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string rel = entry.path().lexically_relative(root).generic_string();
            if (rel.empty() || rel.find('/') == std::string::npos) continue;
            if (rel.rfind("posix/", 0) == 0 || rel.rfind("right/", 0) == 0 || rel.rfind("Etc/", 0) == 0) continue;
            if (rel.find('.') != std::string::npos) continue;
            if (std::isupper(static_cast<unsigned char>(rel[0]))) {
                zones.push_back(std::move(rel));
            }
        }
    }
    if (zones.empty()) {
        zones = {
            "UTC", "America/New_York", "America/Chicago", "America/Denver", "America/Los_Angeles",
            "Europe/London", "Europe/Amsterdam", "Europe/Berlin", "Europe/Paris",
            "Asia/Tehran", "Asia/Dubai", "Asia/Tokyo", "Asia/Seoul", "Asia/Shanghai", "Australia/Sydney"
        };
    } else {
        std::sort(zones.begin(), zones.end());
        zones.erase(std::unique(zones.begin(), zones.end()), zones.end());
    }
    return zones;
}

inline bool iequals(const std::string& a, const std::string& b) {
    return a.size() == b.size() && ::strncasecmp(a.c_str(), b.c_str(), a.size()) == 0;
}

inline bool is_excluded_app(const tc::Preferences& prefs, const std::string& app) {
    if (!prefs.app_exclusions_enabled || app.empty()) return false;
    std::string base = std::filesystem::path(app).filename().string();
    for (const auto& ex : prefs.excluded_apps) {
        if (iequals(ex, app) || iequals(ex, base)) return true;
    }
    return false;
}

inline std::vector<uint8_t> decode_base64_data_url(const std::string& url) {
    auto comma = url.find(',');
    std::string b64 = (comma != std::string::npos) ? url.substr(comma + 1) : url;
    static const int8_t T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    std::vector<uint8_t> out;
    out.reserve((b64.size() * 3) / 4);
    uint32_t val = 0;
    int valb = -8;
    for (unsigned char c : b64) {
        if (c == '=') break;
        int d = T[c];
        if (d == -1) continue;
        val = (val << 6) + static_cast<uint32_t>(d);
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

} // namespace tc::posix_util
