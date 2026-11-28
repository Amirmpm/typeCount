#pragma once

#include "../core/json.hpp"
#include "../core/preferences.hpp"
#include "../core/sound_synth.hpp"
#include "../core/version.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#if defined(GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#endif

namespace tc::linux_host {

inline std::string home_dir() {
    if (const char* h = std::getenv("HOME"); h && *h) return h;
    if (struct passwd* pw = getpwuid(getuid()); pw && pw->pw_dir) return pw->pw_dir;
    return "/tmp";
}

inline std::string exe_path() {
    char buf[4096]{};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return std::string(buf);
    }
    return "typecount";
}

inline std::string exe_dir() {
    return std::filesystem::path(exe_path()).parent_path().string();
}

inline std::string default_data_dir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::filesystem::path base = (xdg && *xdg) ? std::filesystem::path(xdg)
                                               : (std::filesystem::path(home_dir()) / ".local" / "share");
    std::filesystem::path dir = base / "TypeCount";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

inline std::string cache_dir() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    std::filesystem::path base = (xdg && *xdg) ? std::filesystem::path(xdg)
                                               : (std::filesystem::path(home_dir()) / ".cache");
    std::filesystem::path dir = base / "TypeCount";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

inline std::string pointer_file_path() {
    return (std::filesystem::path(default_data_dir()) / "data_dir_pointer.txt").string();
}

inline std::string resolve_active_data_dir() {
    std::ifstream in(pointer_file_path(), std::ios::binary);
    if (in) {
        std::string custom;
        std::getline(in, custom);
        while (!custom.empty() && (custom.back() == '\r' || custom.back() == '\n')) custom.pop_back();
        if (!custom.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(custom, ec);
            if (!ec) return custom;
        }
    }
    return default_data_dir();
}

inline bool save_custom_data_dir(const std::string& custom) {
    if (custom.empty()) {
        std::error_code ec;
        std::filesystem::remove(pointer_file_path(), ec);
        return true;
    }
    std::ofstream out(pointer_file_path(), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << custom << "\n";
    return out.good();
}

inline std::string detect_system_locale() {
    const char* vars[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    for (const char* name : vars) {
        const char* val = std::getenv(name);
        if (!val || !*val) continue;
        std::string s(val);
        for (char& c : s) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (c == '_') c = '-';
        }
        auto dot = s.find('.');
        if (dot != std::string::npos) s.resize(dot);
        if (s.rfind("fa", 0) == 0 || s.rfind("pes", 0) == 0) return "fa";
        if (s.rfind("zh", 0) == 0) {
            if (s.find("hant") != std::string::npos || s.find("-tw") != std::string::npos ||
                s.find("-hk") != std::string::npos || s.find("-mo") != std::string::npos) {
                return "zh-TW";
            }
            return "zh-CN";
        }
        if (s.rfind("ja", 0) == 0) return "ja";
        if (s.rfind("ko", 0) == 0) return "ko";
        if (s.rfind("fr", 0) == 0) return "fr";
        if (s.rfind("de", 0) == 0) return "de";
        if (s.rfind("es", 0) == 0) return "es";
        if (s.rfind("en", 0) == 0) return "en";
    }
    return "en";
}

inline std::string autostart_desktop_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::filesystem::path base = (xdg && *xdg) ? std::filesystem::path(xdg)
                                               : (std::filesystem::path(home_dir()) / ".config");
    return (base / "autostart" / "typecount.desktop").string();
}

inline bool is_autostart_enabled() {
    std::error_code ec;
    return std::filesystem::exists(autostart_desktop_path(), ec);
}

inline bool set_autostart_enabled(bool enabled) {
    std::filesystem::path p(autostart_desktop_path());
    std::error_code ec;
    if (!enabled) {
        std::filesystem::remove(p, ec);
        return true;
    }
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Version=1.0\n"
        << "Name=TypeCount\n"
        << "Comment=Keyboard & mouse analytics\n"
        << "Exec=\"" << exe_path() << "\" --background\n"
        << "Icon=typecount\n"
        << "Terminal=false\n"
        << "Categories=Utility;\n"
        << "X-GNOME-Autostart-enabled=true\n";
    return out.good();
}

inline std::string run_cmd_capture(const char* cmd) {
    std::string out;
    FILE* f = popen(cmd, "r");
    if (!f) return out;
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) out += buf;
    pclose(f);
    return out;
}

inline bool detect_os_dark_mode() {
    // 1. Check xdg-desktop-portal / GNOME color-scheme if available
    if (GSettingsSchemaSource* src = g_settings_schema_source_get_default()) {
        if (GSettingsSchema* schema = g_settings_schema_source_lookup(src, "org.gnome.desktop.interface", TRUE)) {
            if (g_settings_schema_has_key(schema, "color-scheme")) {
                GSettings* gs = g_settings_new("org.gnome.desktop.interface");
                if (gs) {
                    char* scheme = g_settings_get_string(gs, "color-scheme");
                    bool dark = scheme && std::strstr(scheme, "dark") != nullptr;
                    bool light = scheme && std::strstr(scheme, "light") != nullptr;
                    g_free(scheme);
                    g_object_unref(gs);
                    g_settings_schema_unref(schema);
                    if (dark) return true;
                    if (light) return false;
                }
            }
            g_settings_schema_unref(schema);
        }
    }

    // 2. Fallback to GTK theme name / application-prefer-dark-theme
    if (GtkSettings* st = gtk_settings_get_default()) {
        gboolean prefer_dark = FALSE;
        gchar* theme_name = nullptr;
        g_object_get(st, "gtk-application-prefer-dark-theme", &prefer_dark,
                     "gtk-theme-name", &theme_name, nullptr);
        bool name_dark = false;
        if (theme_name) {
            std::string lower(theme_name);
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            name_dark = (lower.find("dark") != std::string::npos);
            g_free(theme_name);
        }
        if (prefer_dark || name_dark) return true;
    }

    // 3. Fallback to GTK_THEME environment variable
    if (const char* env_theme = std::getenv("GTK_THEME")) {
        std::string lower(env_theme);
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.find("dark") != std::string::npos) return true;
    }
    return false;
}

inline int detect_monitor_ppi(GtkWidget* window = nullptr) {
    GdkDisplay* display = gdk_display_get_default();
    if (!display) return 96;

    GdkMonitor* monitor = nullptr;
    if (window && gtk_widget_get_window(window)) {
        monitor = gdk_display_get_monitor_at_window(display, gtk_widget_get_window(window));
    }
    if (!monitor) monitor = gdk_display_get_primary_monitor(display);
    if (!monitor && gdk_display_get_n_monitors(display) > 0) {
        monitor = gdk_display_get_monitor(display, 0);
    }
    if (monitor) {
        int w_mm = gdk_monitor_get_width_mm(monitor);
        int h_mm = gdk_monitor_get_height_mm(monitor);
        GdkRectangle geom{};
        gdk_monitor_get_geometry(monitor, &geom);
        int scale = std::max(1, gdk_monitor_get_scale_factor(monitor));
        if (w_mm > 0 && h_mm > 0 && geom.width > 0 && geom.height > 0) {
            double px_diag = std::hypot(static_cast<double>(geom.width) * scale,
                                        static_cast<double>(geom.height) * scale);
            double mm_diag = std::hypot(static_cast<double>(w_mm), static_cast<double>(h_mm));
            int ppi = static_cast<int>(std::lround(px_diag * 25.4 / mm_diag));
            if (ppi >= 72 && ppi <= 360) return ppi;
            return std::clamp(ppi, 72, 360);
        }
    }

#if defined(GDK_WINDOWING_X11)
    if (GDK_IS_X11_DISPLAY(display)) {
        Display* dpy = GDK_DISPLAY_XDISPLAY(display);
        int scr = DefaultScreen(dpy);
        int px_w = DisplayWidth(dpy, scr);
        int px_h = DisplayHeight(dpy, scr);
        int mm_w = DisplayWidthMM(dpy, scr);
        int mm_h = DisplayHeightMM(dpy, scr);
        if (px_w > 0 && px_h > 0 && mm_w > 0 && mm_h > 0) {
            double px_diag = std::hypot(static_cast<double>(px_w), static_cast<double>(px_h));
            double mm_diag = std::hypot(static_cast<double>(mm_w), static_cast<double>(mm_h));
            int ppi = static_cast<int>(std::lround(px_diag * 25.4 / mm_diag));
            return std::clamp(ppi, 72, 360);
        }
    }
#endif
    return 96;
}

inline std::string active_application_name() {
#if defined(GDK_WINDOWING_X11)
    GdkDisplay* gdk_dpy = gdk_display_get_default();
    if (gdk_dpy && GDK_IS_X11_DISPLAY(gdk_dpy)) {
        Display* dpy = GDK_DISPLAY_XDISPLAY(gdk_dpy);
        Atom net_active = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", True);
        Atom net_pid = XInternAtom(dpy, "_NET_WM_PID", True);
        if (net_active) {
            Atom actual_type{};
            int actual_fmt = 0;
            unsigned long nitems = 0, bytes_after = 0;
            unsigned char* prop = nullptr;
            Window root = DefaultRootWindow(dpy);
            if (XGetWindowProperty(dpy, root, net_active, 0, 1, False, XA_WINDOW,
                                   &actual_type, &actual_fmt, &nitems, &bytes_after, &prop) == 0 && prop) {
                Window active = *reinterpret_cast<Window*>(prop);
                XFree(prop);
                if (active) {
                    if (net_pid) {
                        unsigned char* pid_prop = nullptr;
                        unsigned long pid_n = 0, pid_after = 0;
                        if (XGetWindowProperty(dpy, active, net_pid, 0, 1, False, XA_CARDINAL,
                                               &actual_type, &actual_fmt, &pid_n, &pid_after, &pid_prop) == 0 && pid_prop) {
                            unsigned long pid = *reinterpret_cast<unsigned long*>(pid_prop);
                            XFree(pid_prop);
                            if (pid > 0) {
                                std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
                                std::string name;
                                if (std::getline(comm, name) && !name.empty()) {
                                    while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
                                    return name;
                                }
                            }
                        }
                    }
                    XClassHint hint{};
                    if (XGetClassHint(dpy, active, &hint)) {
                        std::string cls = hint.res_class ? hint.res_class : (hint.res_name ? hint.res_name : "");
                        if (hint.res_name) XFree(hint.res_name);
                        if (hint.res_class) XFree(hint.res_class);
                        if (!cls.empty()) return cls;
                    }
                }
            }
        }
    }
#endif
    return "desktop";
}

inline void spawn_detached(const std::vector<std::string>& argv) {
    if (argv.empty()) return;
    pid_t pid = fork();
    if (pid == 0) {
        pid_t pid2 = fork();
        if (pid2 == 0) {
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > STDERR_FILENO) close(devnull);
            }
            std::vector<char*> ptrs;
            ptrs.reserve(argv.size() + 1);
            for (const auto& s : argv) ptrs.push_back(const_cast<char*>(s.c_str()));
            ptrs.push_back(nullptr);
            execvp(ptrs[0], ptrs.data());
            _exit(127);
        }
        _exit(0);
    } else if (pid > 0) {
        int st = 0;
        waitpid(pid, &st, 0);
    }
}

inline void play_pastel_wav(const std::string& preset) {
    if (preset == "none") return;
    const auto& wav = tc::sound::get_cached_pastel_wav(preset);
    if (wav.empty()) return;

    std::filesystem::path snd_dir = std::filesystem::path(cache_dir()) / "sounds";
    std::error_code ec;
    std::filesystem::create_directories(snd_dir, ec);
    std::string safe_preset = (tc::Preferences::is_valid_sound(preset) && preset != "none") ? preset : "pastel_pearl";
    std::filesystem::path wav_path = snd_dir / (safe_preset + ".wav");
    if (!std::filesystem::exists(wav_path, ec) || std::filesystem::file_size(wav_path, ec) != wav.size()) {
        std::ofstream out(wav_path, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(reinterpret_cast<const char*>(wav.data()), static_cast<std::streamsize>(wav.size()));
        out.close();
    }

    auto has_bin = [](const char* name) {
        const char* path_env = std::getenv("PATH");
        if (!path_env) return false;
        std::stringstream ss(path_env);
        std::string item;
        while (std::getline(ss, item, ':')) {
            if (access((item + "/" + name).c_str(), X_OK) == 0) return true;
        }
        return false;
    };

    if (has_bin("pw-play")) {
        spawn_detached({"pw-play", wav_path.string()});
    } else if (has_bin("paplay")) {
        spawn_detached({"paplay", wav_path.string()});
    } else if (has_bin("aplay")) {
        spawn_detached({"aplay", "-q", wav_path.string()});
    }
}

inline void open_url_or_path(const std::string& target) {
    if (target.empty()) return;
    spawn_detached({"xdg-open", target});
}

inline void send_desktop_notification(const std::string& title, const std::string& body) {
    spawn_detached({"notify-send", "-a", "TypeCount", "-i", "typecount", title, body});
}

inline std::string runtime_socket_path() {
    const char* xdg_rt = std::getenv("XDG_RUNTIME_DIR");
    if (xdg_rt && *xdg_rt) return std::string(xdg_rt) + "/typecount-ipc.sock";
    return "/tmp/typecount-ipc-" + std::to_string(getuid()) + ".sock";
}

inline std::string lock_file_path() {
    const char* xdg_rt = std::getenv("XDG_RUNTIME_DIR");
    if (xdg_rt && *xdg_rt) return std::string(xdg_rt) + "/typecount.lock";
    return "/tmp/typecount-" + std::to_string(getuid()) + ".lock";
}

inline bool signal_existing_instance() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string sp = runtime_socket_path();
    if (sp.size() >= sizeof(addr.sun_path)) {
        close(fd);
        return false;
    }
    std::strcpy(addr.sun_path, sp.c_str());
    bool ok = (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
    if (ok) {
        const char msg[] = "SHOW\n";
        if (write(fd, msg, sizeof(msg) - 1) < 0) {}
    }
    close(fd);
    return ok;
}

struct UpdateCheckResult {
    bool ok = false;
    bool rate_limited = false;
    bool update_available = false;
    std::string latest_version;
    int latest_num = 0;
    std::string release_url;
    std::string download_url;
    std::string notes;
};

inline int parse_version_num(const std::string& v) {
    std::string s = v;
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
    int a = 0, b = 0, c = 0;
    if (std::sscanf(s.c_str(), "%d.%d.%d", &a, &b, &c) >= 2) {
        return a * 10000 + b * 100 + c;
    }
    return 0;
}

inline UpdateCheckResult check_github_release() {
    UpdateCheckResult res;
    std::string json_text = run_cmd_capture(
        "curl -sSL --max-time 10 -A 'TypeCount/" TC_VERSION_STR "' "
        "-H 'Accept: application/vnd.github+json' "
        "'https://api.github.com/repos/Amirmpm/typeCount/releases/latest' 2>/dev/null");
    if (json_text.empty()) return res;

    tc::JsonValue root;
    if (!tc::JsonValue::parse(json_text, root) || !root.is_object()) return res;
    if (root.has("message") && !root.has("tag_name")) {
        std::string msg = root.get("message").as_string();
        if (msg.find("rate limit") != std::string::npos) res.rate_limited = true;
        return res;
    }

    std::string tag = root.get("tag_name").as_string();
    res.latest_version = (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) ? tag.substr(1) : tag;
    res.latest_num = parse_version_num(res.latest_version);
    res.release_url = root.get("html_url").as_string_or("https://github.com/Amirmpm/typeCount/releases");
    res.notes = root.get("body").as_string();

    std::string deb_url, tar_url;
    const auto& assets = root.get("assets");
    if (assets.is_array()) {
        for (const auto& a : assets.arr) {
            std::string name = a.get("name").as_string();
            std::string dl = a.get("browser_download_url").as_string();
            if (name.find("linux") != std::string::npos) {
                if (name.size() >= 4 && name.substr(name.size() - 4) == ".deb") deb_url = dl;
                else if (name.size() >= 7 && name.substr(name.size() - 7) == ".tar.gz") tar_url = dl;
            }
        }
    }
    res.download_url = !deb_url.empty() ? deb_url : (!tar_url.empty() ? tar_url : res.release_url);
    res.ok = !res.latest_version.empty();
    res.update_available = res.ok && (res.latest_num > TC_VERSION_NUM);
    return res;
}

} // namespace tc::linux_host
