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

#include <algorithm>
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

inline std::string foreground_process_name() {
    static HWND cached_hwnd = nullptr;
    static std::string cached_name;
    HWND fg = GetForegroundWindow();
    if (!fg) return {};
    if (fg == cached_hwnd && !cached_name.empty()) return cached_name;

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

} // namespace win
} // namespace tc
