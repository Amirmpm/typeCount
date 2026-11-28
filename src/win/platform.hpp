#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <psapi.h>
#include <dwmapi.h>
#include <winhttp.h>
#include <mmsystem.h>
#include <msi.h>

#include "../core/json.hpp"
#include "../core/sound_synth.hpp"
#include "../core/version.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace tc {
namespace win {

inline std::wstring to_wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &out[0], len);
    return out;
}

inline std::string to_utf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), &out[0], len, nullptr, nullptr);
    return out;
}

inline std::wstring exe_path_wide() {
    std::wstring buf(1024, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, &buf[0], static_cast<DWORD>(buf.size()));
    buf.resize(n);
    return buf;
}

inline std::wstring exe_dir_wide() {
    std::filesystem::path p(exe_path_wide());
    return p.parent_path().wstring();
}

inline std::wstring default_data_dir_wide() {
    PWSTR local = nullptr;
    std::wstring base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) && local) {
        base = local;
        CoTaskMemFree(local);
    } else {
        wchar_t tmp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tmp);
        base = tmp;
    }
    std::wstring dir = base + L"\\TypeCount";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

inline void ensure_dir_wide(const std::wstring& dir) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(dir), ec);
}

inline int current_tz_offset_sec() {
    TIME_ZONE_INFORMATION tzi{};
    DWORD mode = GetTimeZoneInformation(&tzi);
    LONG bias_min = tzi.Bias;
    if (mode == TIME_ZONE_ID_DAYLIGHT) bias_min += tzi.DaylightBias;
    else if (mode == TIME_ZONE_ID_STANDARD) bias_min += tzi.StandardBias;
    // Windows Bias = UTC - Local, so Local - UTC = -Bias
    return -static_cast<int>(bias_min) * 60;
}

inline int64_t now_utc_ms() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ui{};
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    // 100-ns intervals since 1601-01-01 to 1970-01-01 = 116444736000000000ULL
    return static_cast<int64_t>((ui.QuadPart - 116444736000000000ULL) / 10000ULL);
}

constexpr const wchar_t* RUN_REG_SUBKEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* RUN_REG_VALUE = L"TypeCount";

inline bool is_autostart_enabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_REG_SUBKEY, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0, size = 0;
    LONG rc = RegQueryValueExW(key, RUN_REG_VALUE, nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && size > 2;
}

inline bool set_autostart_enabled(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_REG_SUBKEY, 0, nullptr, 0, KEY_SET_VALUE | KEY_READ, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    bool ok = false;
    if (enabled) {
        std::wstring cmd = L"\"" + exe_path_wide() + L"\" --background";
        DWORD bytes = static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t));
        ok = (RegSetValueExW(key, RUN_REG_VALUE, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), bytes) == ERROR_SUCCESS);
    } else {
        LONG rc = RegDeleteValueW(key, RUN_REG_VALUE);
        ok = (rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(key);
    return ok;
}

inline void sync_autostart_path_if_enabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_REG_SUBKEY, 0, KEY_READ | KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return;
    }
    wchar_t cur_buf[2048]{};
    DWORD type = 0, size = sizeof(cur_buf);
    if (RegQueryValueExW(key, RUN_REG_VALUE, nullptr, &type, reinterpret_cast<BYTE*>(cur_buf), &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && size > 2) {
        std::wstring desired = L"\"" + exe_path_wide() + L"\" --background";
        if (_wcsicmp(cur_buf, desired.c_str()) != 0) {
            DWORD bytes = static_cast<DWORD>((desired.size() + 1) * sizeof(wchar_t));
            RegSetValueExW(key, RUN_REG_VALUE, 0, REG_SZ, reinterpret_cast<const BYTE*>(desired.c_str()), bytes);
        }
    }
    RegCloseKey(key);
}

inline std::string foreground_process_name() {
    static HWND cached_hwnd = nullptr;
    static std::string cached_name;
    static int64_t cached_ms = 0;
    HWND fg = GetForegroundWindow();
    if (!fg) return {};
    int64_t now = now_utc_ms();
    if (fg == cached_hwnd && !cached_name.empty() && (now - cached_ms) < 5000) return cached_name;

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == 0) return {};

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return {};
    wchar_t path[1024]{};
    DWORD len = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
    std::string result;
    if (QueryFullProcessImageNameW(proc, 0, path, &len) && len > 0) {
        std::wstring w(path, len);
        size_t pos = w.find_last_of(L"\\/");
        std::wstring fname = (pos != std::wstring::npos) ? w.substr(pos + 1) : w;
        result = to_utf8(fname);
    }
    CloseHandle(proc);
    if (!result.empty()) {
        cached_hwnd = fg;
        cached_name = result;
        cached_ms = now;
    }
    return result;
}

inline bool parse_wake_hotkey(const std::string& spec, UINT& mods, UINT& vk) {
    mods = 0;
    vk = 0;
    if (spec.empty() || spec == "None" || spec == "Off") return false;
    if (spec.find("Ctrl") != std::string::npos) mods |= MOD_CONTROL;
    if (spec.find("Alt") != std::string::npos) mods |= MOD_ALT;
    if (spec.find("Shift") != std::string::npos) mods |= MOD_SHIFT;
    if (spec.find("Win") != std::string::npos) mods |= MOD_WIN;
    mods |= MOD_NOREPEAT;

    size_t plus = spec.find_last_of('+');
    std::string k = (plus != std::string::npos) ? spec.substr(plus + 1) : spec;
    if (k.size() == 1 && ((k[0] >= 'A' && k[0] <= 'Z') || (k[0] >= '0' && k[0] <= '9'))) {
        vk = static_cast<UINT>(k[0]);
        return true;
    }
    if (k == "F12") { vk = VK_F12; return true; }
    if (k == "F11") { vk = VK_F11; return true; }
    if (k == "F10") { vk = VK_F10; return true; }
    if (k == "F9") { vk = VK_F9; return true; }
    vk = 'T';
    if (mods == MOD_NOREPEAT) mods |= MOD_CONTROL | MOD_ALT | MOD_SHIFT;
    return true;
}

inline std::vector<uint8_t> decode_base64_data_url(const std::string& input) {
    size_t comma = input.find(',');
    std::string b64 = (comma != std::string::npos) ? input.substr(comma + 1) : input;
    static int8_t dec[256];
    static bool init = false;
    if (!init) {
        std::fill(std::begin(dec), std::end(dec), static_cast<int8_t>(-1));
        for (int i = 0; i < 26; ++i) dec['A' + i] = static_cast<int8_t>(i);
        for (int i = 0; i < 26; ++i) dec['a' + i] = static_cast<int8_t>(26 + i);
        for (int i = 0; i < 10; ++i) dec['0' + i] = static_cast<int8_t>(52 + i);
        dec[static_cast<unsigned char>('+')] = 62;
        dec[static_cast<unsigned char>('/')] = 63;
        init = true;
    }
    std::vector<uint8_t> out;
    out.reserve((b64.size() * 3) / 4);
    int val = 0, valb = -8;
    for (unsigned char c : b64) {
        if (dec[c] == -1) continue;
        val = (val << 6) + dec[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

inline bool write_bytes_wide(const std::wstring& path, const uint8_t* data, size_t size) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, data, static_cast<DWORD>(size), &written, nullptr);
    CloseHandle(h);
    return ok && written == size;
}

inline bool copy_png_to_clipboard(HWND owner, const std::vector<uint8_t>& png) {
    if (png.empty() || !OpenClipboard(owner)) return false;
    EmptyClipboard();
    UINT fmt = RegisterClipboardFormatW(L"PNG");
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, png.size());
    bool ok = false;
    if (hg) {
        void* dst = GlobalLock(hg);
        if (dst) {
            std::copy(png.begin(), png.end(), static_cast<uint8_t*>(dst));
            GlobalUnlock(hg);
            ok = (SetClipboardData(fmt, hg) != nullptr);
        }
        if (!ok) GlobalFree(hg);
    }
    CloseClipboard();
    return ok;
}

inline std::wstring save_file_dialog(HWND owner, const wchar_t* filter, const wchar_t* def_ext, const std::wstring& default_name) {
    wchar_t buf[1024]{};
    wcsncpy(buf, default_name.c_str(), 1023);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 1024;
    ofn.lpstrDefExt = def_ext;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&ofn)) return std::wstring(buf);
    return {};
}

inline std::wstring open_sqlite_dialog(HWND owner) {
    wchar_t buf[1024]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"SQLite Database (*.sqlite3;*.db)\0*.sqlite3;*.db\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 1024;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return std::wstring(buf);
    return {};
}

inline std::wstring pick_folder_dialog(HWND owner, const std::wstring& title) {
    IFileDialog* pfd = nullptr;
    std::wstring res;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))) && pfd) {
        DWORD opts = 0;
        pfd->GetOptions(&opts);
        pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
        if (!title.empty()) pfd->SetTitle(title.c_str());
        if (SUCCEEDED(pfd->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    res = path;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        pfd->Release();
    }
    return res;
}

inline void init_per_monitor_dpi() {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        using FnCtx = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto fn = reinterpret_cast<FnCtx>(GetProcAddress(u32, "SetProcessDpiAwarenessContext"));
        if (fn && fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
    SetProcessDPIAware();
}

inline UINT window_dpi(HWND hwnd) {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32 && hwnd) {
        using FnDpi = UINT(WINAPI*)(HWND);
        auto fn = reinterpret_cast<FnDpi>(GetProcAddress(u32, "GetDpiForWindow"));
        if (fn) {
            UINT d = fn(hwnd);
            if (d > 0) return d;
        }
    }
    return 96;
}

inline int timezone_offset_sec_for(const std::string& tz_pref) {
    if (tz_pref.empty() || tz_pref == "system") return current_tz_offset_sec();
    if (tz_pref == "UTC") return 0;

    struct WinTzEntry {
        const char* iana;
        const wchar_t* win_name;
        int fallback_sec;
    };
    static const WinTzEntry kTable[] = {
        {"Asia/Tehran",        L"Iran Standard Time",             12600},
        {"Europe/Amsterdam",   L"W. Europe Standard Time",         3600},
        {"Europe/London",      L"GMT Standard Time",                  0},
        {"Europe/Berlin",      L"W. Europe Standard Time",         3600},
        {"Europe/Paris",       L"Romance Standard Time",           3600},
        {"Europe/Moscow",      L"Russian Standard Time",          10800},
        {"America/New_York",   L"Eastern Standard Time",         -18000},
        {"America/Chicago",    L"Central Standard Time",         -21600},
        {"America/Denver",     L"Mountain Standard Time",        -25200},
        {"America/Los_Angeles",L"Pacific Standard Time",         -28800},
        {"America/Toronto",    L"Eastern Standard Time",         -18000},
        {"America/Sao_Paulo",  L"E. South America Standard Time",-10800},
        {"Asia/Dubai",         L"Arabian Standard Time",          14400},
        {"Asia/Istanbul",      L"Turkey Standard Time",           10800},
        {"Asia/Riyadh",        L"Arab Standard Time",             10800},
        {"Asia/Kolkata",       L"India Standard Time",            19800},
        {"Asia/Bangkok",       L"SE Asia Standard Time",          25200},
        {"Asia/Singapore",     L"Singapore Standard Time",        28800},
        {"Asia/Shanghai",      L"China Standard Time",            28800},
        {"Asia/Tokyo",         L"Tokyo Standard Time",            32400},
        {"Asia/Seoul",         L"Korea Standard Time",            32400},
        {"Australia/Sydney",   L"AUS Eastern Standard Time",      36000},
        {"Pacific/Auckland",   L"New Zealand Standard Time",      43200},
    };

    for (const auto& e : kTable) {
        if (tz_pref != e.iana) continue;
        std::wstring sub = std::wstring(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\") + e.win_name;
        HKEY k = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub.c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
            struct REG_TZI_FORMAT {
                LONG Bias;
                LONG StandardBias;
                LONG DaylightBias;
                SYSTEMTIME StandardDate;
                SYSTEMTIME DaylightDate;
            } reg_tzi{};
            DWORD sz = sizeof(reg_tzi);
            if (RegQueryValueExW(k, L"TZI", nullptr, nullptr, reinterpret_cast<BYTE*>(&reg_tzi), &sz) == ERROR_SUCCESS &&
                sz == sizeof(reg_tzi)) {
                TIME_ZONE_INFORMATION tzi{};
                tzi.Bias = reg_tzi.Bias;
                tzi.StandardBias = reg_tzi.StandardBias;
                tzi.DaylightBias = reg_tzi.DaylightBias;
                tzi.StandardDate = reg_tzi.StandardDate;
                tzi.DaylightDate = reg_tzi.DaylightDate;
                SYSTEMTIME utc_now{}, tz_local{};
                GetSystemTime(&utc_now);
                if (SystemTimeToTzSpecificLocalTime(&tzi, &utc_now, &tz_local)) {
                    FILETIME ft_u{}, ft_l{};
                    SystemTimeToFileTime(&utc_now, &ft_u);
                    SystemTimeToFileTime(&tz_local, &ft_l);
                    ULARGE_INTEGER u{}, l{};
                    u.LowPart = ft_u.dwLowDateTime; u.HighPart = ft_u.dwHighDateTime;
                    l.LowPart = ft_l.dwLowDateTime; l.HighPart = ft_l.dwHighDateTime;
                    RegCloseKey(k);
                    return static_cast<int>((static_cast<int64_t>(l.QuadPart) - static_cast<int64_t>(u.QuadPart)) / 10000000LL);
                }
            }
            RegCloseKey(k);
        }
        return e.fallback_sec;
    }
    return current_tz_offset_sec();
}

inline std::vector<std::string> system_timezones_list() {
    // Curated comprehensive IANA list supported cleanly by Chromium/Edge Intl
    return {
        "UTC", "Asia/Tehran", "Europe/Amsterdam", "Europe/London", "Europe/Berlin",
        "Europe/Paris", "Europe/Moscow", "America/New_York", "America/Chicago",
        "America/Denver", "America/Los_Angeles", "America/Toronto", "America/Sao_Paulo",
        "Asia/Dubai", "Asia/Istanbul", "Asia/Riyadh", "Asia/Kolkata", "Asia/Bangkok",
        "Asia/Singapore", "Asia/Shanghai", "Asia/Tokyo", "Asia/Seoul",
        "Australia/Sydney", "Pacific/Auckland"
    };
}

inline std::string resolve_system_locale() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0) {
        std::string utf8 = to_utf8(name);
        for (auto& c : utf8) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (utf8.rfind("fa", 0) == 0 || utf8.rfind("pes", 0) == 0) return "fa";
        if (utf8.rfind("zh", 0) == 0) {
            if (utf8.find("hant") != std::string::npos || utf8.find("-tw") != std::string::npos ||
                utf8.find("-hk") != std::string::npos || utf8.find("-mo") != std::string::npos) {
                return "zh-TW";
            }
            return "zh-CN";
        }
        if (utf8.rfind("ja", 0) == 0) return "ja";
        if (utf8.rfind("ko", 0) == 0) return "ko";
        if (utf8.rfind("fr", 0) == 0) return "fr";
        if (utf8.rfind("de", 0) == 0) return "de";
        if (utf8.rfind("es", 0) == 0) return "es";
    }
    LANGID lid = PRIMARYLANGID(GetUserDefaultUILanguage());
    if (lid == LANG_PERSIAN) return "fa";
    if (lid == LANG_JAPANESE) return "ja";
    if (lid == LANG_KOREAN) return "ko";
    if (lid == LANG_FRENCH) return "fr";
    if (lid == LANG_GERMAN) return "de";
    if (lid == LANG_SPANISH) return "es";
    return "en";
}

inline uint64_t idle_elapsed_ms() {
    LASTINPUTINFO lii{};
    lii.cbSize = sizeof(lii);
    if (!GetLastInputInfo(&lii)) return 0;
    // Modular 32-bit unsigned difference handles GetTickCount() 49.7-day wraparound cleanly
    uint32_t delta = static_cast<uint32_t>(GetTickCount()) - static_cast<uint32_t>(lii.dwTime);
    return static_cast<uint64_t>(delta);
}

inline bool is_windows_dark_theme() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD val = 1, sz = sizeof(val);
        LONG rc = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &sz);
        RegCloseKey(key);
        if (rc == ERROR_SUCCESS) return val == 0;
    }
    return false;
}

inline void set_window_immersive_dark_mode(HWND hwnd, bool dark) {
    if (!hwnd) return;
    BOOL val = dark ? TRUE : FALSE;
    // Attribute 20 on Windows 11 / Win10 20H1+, fallback 19 on older Win10
    if (FAILED(DwmSetWindowAttribute(hwnd, static_cast<DWMWINDOWATTRIBUTE>(20), &val, sizeof(val)))) {
        DwmSetWindowAttribute(hwnd, static_cast<DWMWINDOWATTRIBUTE>(19), &val, sizeof(val));
    }
}

inline int detect_monitor_ppi(HWND hwnd = nullptr) {
    HMONITOR mon = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
                        : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (mon && GetMonitorInfoW(mon, &mi)) {
        HDC hdc = CreateDCW(L"DISPLAY", mi.szDevice, nullptr, nullptr);
        if (hdc) {
            int px_w = GetDeviceCaps(hdc, DESKTOPHORZRES);
            int px_h = GetDeviceCaps(hdc, DESKTOPVERTRES);
            if (px_w <= 0) px_w = GetDeviceCaps(hdc, HORZRES);
            if (px_h <= 0) px_h = GetDeviceCaps(hdc, VERTRES);
            int mm_w = GetDeviceCaps(hdc, HORZSIZE);
            int mm_h = GetDeviceCaps(hdc, VERTSIZE);
            DeleteDC(hdc);
            if (px_w >= 640 && px_h >= 480 && mm_w >= 120 && mm_w <= 2500 && mm_h >= 80 && mm_h <= 1600) {
                double diag_px = std::hypot(static_cast<double>(px_w), static_cast<double>(px_h));
                double diag_in = std::hypot(static_cast<double>(mm_w) / 25.4, static_cast<double>(mm_h) / 25.4);
                if (diag_in > 2.0) {
                    int ppi = static_cast<int>(std::round(diag_px / diag_in));
                    return std::max(72, std::min(360, ppi));
                }
            }
        }
    }
    // Fallback to Windows logical monitor DPI
    if (hwnd) {
        UINT d = window_dpi(hwnd);
        if (d >= 72) return std::max(72, std::min(360, static_cast<int>(d)));
    }
    HDC screen = GetDC(nullptr);
    int log_dpi = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if (screen) ReleaseDC(nullptr, screen);
    return std::max(72, std::min(360, log_dpi > 0 ? log_dpi : 96));
}

inline void play_notification_sound_async(const std::string& preset) {
    if (preset == "none") return;
    const auto& wav = tc::sound::get_cached_pastel_wav(preset);
    if (wav.empty()) return;
    PlaySoundW(reinterpret_cast<LPCWSTR>(wav.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

inline void play_gentle_chime_async() {
    play_notification_sound_async("pastel_pearl");
}

inline int read_file_version_num(const std::wstring& exe_path) {
    DWORD handle = 0;
    DWORD sz = GetFileVersionInfoSizeW(exe_path.c_str(), &handle);
    if (sz == 0) return 0;
    std::vector<uint8_t> buf(sz);
    if (!GetFileVersionInfoW(exe_path.c_str(), 0, sz, buf.data())) return 0;
    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<void**>(&ffi), &len) || !ffi) return 0;
    int maj = HIWORD(ffi->dwFileVersionMS);
    int min = LOWORD(ffi->dwFileVersionMS);
    int pat = HIWORD(ffi->dwFileVersionLS);
    return (maj * 10000) + (min * 100) + pat;
}

inline std::wstring find_installed_msi_product_code() {
    wchar_t prod[64]{};
    if (MsiEnumRelatedProductsW(TC_UPGRADE_GUID_W, 0, 0, prod) == ERROR_SUCCESS && prod[0] != L'\0') {
        return std::wstring(prod);
    }
    return {};
}

inline std::wstring find_registered_uninstall_command() {
    const HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    const wchar_t* subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TypeCount";
    for (HKEY root : roots) {
        HKEY k = nullptr;
        if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &k) == ERROR_SUCCESS) {
            wchar_t cmd[2048]{};
            DWORD type = 0, sz = sizeof(cmd);
            if (RegQueryValueExW(k, L"UninstallString", nullptr, &type, reinterpret_cast<BYTE*>(cmd), &sz) == ERROR_SUCCESS &&
                (type == REG_SZ || type == REG_EXPAND_SZ) && cmd[0] != L'\0') {
                RegCloseKey(k);
                return std::wstring(cmd);
            }
            RegCloseKey(k);
        }
    }
    return {};
}

inline std::array<int, 3> parse_semver(const std::string& raw) {
    std::string s = raw;
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(s.begin());
    std::array<int, 3> out{0, 0, 0};
    size_t pos = 0;
    for (int i = 0; i < 3 && pos < s.size(); ++i) {
        int val = 0;
        bool any = false;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
            val = val * 10 + (s[pos] - '0');
            any = true;
            ++pos;
        }
        if (any) out[i] = val;
        if (pos < s.size() && s[pos] == '.') ++pos;
        else break;
    }
    return out;
}

inline int compare_semver(const std::string& a, const std::string& b) {
    auto pa = parse_semver(a);
    auto pb = parse_semver(b);
    for (int i = 0; i < 3; ++i) {
        if (pa[i] < pb[i]) return -1;
        if (pa[i] > pb[i]) return 1;
    }
    return 0;
}

struct ReleaseCheckResult {
    bool ok = false;
    bool rate_limited = false;
    std::string latest_version;
    std::string release_url = TC_RELEASES_URL;
    std::string download_url;
    std::string error;
};

inline ReleaseCheckResult fetch_latest_github_release() {
    ReleaseCheckResult res;
    HINTERNET ses = WinHttpOpen(L"TypeCount/" TC_VERSION_WSTR,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { res.error = "WinHttpOpen failed"; return res; }
    WinHttpSetTimeouts(ses, 5000, 6000, 6000, 8000);

    HINTERNET con = WinHttpConnect(ses, TC_LATEST_RELEASE_API_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { WinHttpCloseHandle(ses); res.error = "WinHttpConnect failed"; return res; }

    HINTERNET req = WinHttpOpenRequest(con, L"GET", TC_LATEST_RELEASE_API_PATH,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) { WinHttpCloseHandle(con); WinHttpCloseHandle(ses); res.error = "WinHttpOpenRequest failed"; return res; }

    WinHttpAddRequestHeaders(req, L"Accept: application/vnd.github+json\r\n", -1L, WINHTTP_ADDREQ_FLAG_ADD);
    BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(req, nullptr);
    if (!ok) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        res.error = "Network request failed";
        return res;
    }

    DWORD status = 0, ssz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &ssz, WINHTTP_NO_HEADER_INDEX);
    if (status == 403 || status == 429) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        res.rate_limited = true;
        res.error = "GitHub rate limit";
        return res;
    }
    if (status < 200 || status >= 300) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        res.error = "HTTP " + std::to_string(status);
        return res;
    }

    std::string body;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req, &chunk[0], avail, &read) || read == 0) break;
        chunk.resize(read);
        body += chunk;
        if (body.size() > 512 * 1024) break;
    }
    WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);

    JsonValue doc;
    if (!JsonValue::parse(body, doc) || !doc.is_object()) {
        res.error = "Invalid JSON response";
        return res;
    }
    std::string tag = doc.get("tag_name").as_string();
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(tag.begin());
    res.latest_version = tag;
    std::string html_url = doc.get("html_url").as_string();
    if (!html_url.empty()) res.release_url = html_url;

    std::string msi_url, setup_exe_url, portable_exe_url;
    const auto& assets = doc.get("assets");
    if (assets.is_array()) {
        for (const auto& a : assets.arr) {
            std::string name = a.get("name").as_string();
            std::string dl = a.get("browser_download_url").as_string();
            if (name.size() >= 4 && name.substr(name.size() - 4) == ".msi") msi_url = dl;
            else if (name.size() >= 4 && name.substr(name.size() - 4) == ".exe") {
                if (name.find("setup") != std::string::npos || name.find("Setup") != std::string::npos) setup_exe_url = dl;
                else portable_exe_url = dl;
            }
        }
    }
    res.download_url = !msi_url.empty() ? msi_url
                     : (!setup_exe_url.empty() ? setup_exe_url
                     : (!portable_exe_url.empty() ? portable_exe_url : res.release_url));
    res.ok = !res.latest_version.empty();
    return res;
}

} // namespace win
} // namespace tc
