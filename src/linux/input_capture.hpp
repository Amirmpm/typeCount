#pragma once

#include "../core/key_mapper.hpp"
#include "../core/preferences.hpp"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <functional>
#include <linux/input.h>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>

#ifdef None
#undef None
#endif
#ifdef Status
#undef Status
#endif
#ifdef Success
#undef Success
#endif

namespace tc::linux_host {

struct InputCallbacks {
    std::function<void(uint32_t code, uint16_t vk, const std::optional<tc::ShortcutChord>& chord)> on_key;
    std::function<void(int button)> on_mouse_click;
    std::function<void(int delta)> on_mouse_scroll;
    std::function<void(double px)> on_mouse_move;
    std::function<void()> on_wake_hotkey;
};

struct ParsedHotkey {
    unsigned int mods = 0;
    KeySym keysym = 0;
    bool valid = false;
};

inline ParsedHotkey parse_x11_hotkey(const std::string& spec) {
    ParsedHotkey hk;
    std::string cur;
    std::vector<std::string> parts;
    for (char ch : spec) {
        if (ch == '+') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else if (!std::isspace(static_cast<unsigned char>(ch))) {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    if (parts.empty()) return hk;

    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        std::string p = parts[i];
        for (char& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (p == "ctrl" || p == "control") hk.mods |= ControlMask;
        else if (p == "alt") hk.mods |= Mod1Mask;
        else if (p == "shift") hk.mods |= ShiftMask;
        else if (p == "win" || p == "super" || p == "meta") hk.mods |= Mod4Mask;
    }

    std::string k = parts.back();
    std::string kl = k;
    for (char& c : kl) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (kl.size() == 1 && kl[0] >= 'a' && kl[0] <= 'z') {
        hk.keysym = static_cast<KeySym>(kl[0]);
        hk.valid = true;
    } else if (kl.size() == 1 && kl[0] >= '0' && kl[0] <= '9') {
        hk.keysym = static_cast<KeySym>(kl[0]);
        hk.valid = true;
    } else if (kl == "space") {
        hk.keysym = XK_space;
        hk.valid = true;
    } else if (kl.size() >= 2 && kl[0] == 'f') {
        int fn = std::atoi(kl.c_str() + 1);
        if (fn >= 1 && fn <= 12) {
            hk.keysym = static_cast<KeySym>(XK_F1 + (fn - 1));
            hk.valid = true;
        }
    }
    return hk;
}

class LinuxInputCapture {
public:
    ~LinuxInputCapture() { stop(); }

    void start(InputCallbacks cb, const std::string& hotkey_spec) {
        stop();
        cb_ = std::move(cb);
        {
            std::lock_guard<std::mutex> lk(mu_);
            hotkey_spec_ = hotkey_spec;
        }
        if (pipe(wake_pipe_) != 0) {
            wake_pipe_[0] = -1;
            wake_pipe_[1] = -1;
        } else {
            fcntl(wake_pipe_[0], F_SETFL, O_NONBLOCK);
            fcntl(wake_pipe_[1], F_SETFL, O_NONBLOCK);
        }
        running_.store(true);
        permission_needed_.store(false);
        thread_ = std::thread(&LinuxInputCapture::run_loop, this);
    }

    void stop() {
        running_.store(false);
        if (wake_pipe_[1] >= 0) {
            char b = 'Q';
            (void)write(wake_pipe_[1], &b, 1);
        }
        if (thread_.joinable()) thread_.join();
        if (wake_pipe_[0] >= 0) { close(wake_pipe_[0]); wake_pipe_[0] = -1; }
        if (wake_pipe_[1] >= 0) { close(wake_pipe_[1]); wake_pipe_[1] = -1; }
    }

    void update_hotkey(const std::string& spec) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            hotkey_spec_ = spec;
            hotkey_dirty_ = true;
        }
        if (wake_pipe_[1] >= 0) {
            char b = 'H';
            (void)write(wake_pipe_[1], &b, 1);
        }
    }

    bool permission_needed() const { return permission_needed_.load(); }
    bool active() const { return active_.load(); }

private:
    static uint64_t now_ms() {
        using namespace std::chrono;
        return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }

    static bool is_x11_key_down(Display* dpy, KeySym sym, const char keymap[32]) {
        KeyCode kc = XKeysymToKeycode(dpy, sym);
        if (!kc || kc >= 256) return false;
        return (keymap[kc / 8] & (1 << (kc % 8))) != 0;
    }

    static uint16_t canonical_to_win_vk(uint32_t code) {
        switch (code) {
            case 59: return 0xa2;
            case 62: return 0xa3;
            case 58: return 0xa4;
            case 61: return 0xa5;
            case 56: return 0xa0;
            case 60: return 0xa1;
            case 55: return 0x5b;
            case 54: return 0x5c;
            case 49: return 0x20;
            case 36: return 0x0d;
            case 48: return 0x09;
            case 51: return 0x08;
            case 53: return 0x1b;
            default: return 0;
        }
    }

    void apply_x11_hotkey(Display* dpy, Window root, KeyCode& grabbed_kc, unsigned int& grabbed_mods) {
        if (grabbed_kc != 0) {
            for (unsigned int extra : {0u, static_cast<unsigned int>(Mod2Mask),
                                      static_cast<unsigned int>(LockMask),
                                      static_cast<unsigned int>(Mod2Mask | LockMask)}) {
                XUngrabKey(dpy, grabbed_kc, grabbed_mods | extra, root);
            }
            grabbed_kc = 0;
            grabbed_mods = 0;
        }
        std::string spec;
        {
            std::lock_guard<std::mutex> lk(mu_);
            spec = hotkey_spec_;
            hotkey_dirty_ = false;
        }
        ParsedHotkey hk = parse_x11_hotkey(spec);
        if (!hk.valid) return;
        KeyCode kc = XKeysymToKeycode(dpy, hk.keysym);
        if (!kc) return;
        grabbed_kc = kc;
        grabbed_mods = hk.mods;
        for (unsigned int extra : {0u, static_cast<unsigned int>(Mod2Mask),
                                  static_cast<unsigned int>(LockMask),
                                  static_cast<unsigned int>(Mod2Mask | LockMask)}) {
            XGrabKey(dpy, grabbed_kc, grabbed_mods | extra, root, 0, GrabModeAsync, GrabModeAsync);
        }
        XFlush(dpy);
    }

    void run_loop() {
        Display* dpy = XOpenDisplay(nullptr);
        if (dpy) {
            int xi_opcode = 0, xi_event = 0, xi_error = 0;
            int major = 2, minor = 0;
            if (XQueryExtension(dpy, "XInputExtension", &xi_opcode, &xi_event, &xi_error) &&
                XIQueryVersion(dpy, &major, &minor) == 0 && major >= 2) {
                run_x11_xinput2(dpy, xi_opcode);
                XCloseDisplay(dpy);
                return;
            }
            XCloseDisplay(dpy);
        }
        run_evdev_fallback();
    }

    void run_x11_xinput2(Display* dpy, int xi_opcode) {
        Window root = DefaultRootWindow(dpy);

        XIEventMask evmask{};
        unsigned char mask_bits[XIMaskLen(XI_LASTEVENT)]{};
        XISetMask(mask_bits, XI_RawKeyPress);
        XISetMask(mask_bits, XI_RawKeyRelease);
        XISetMask(mask_bits, XI_RawButtonPress);
        XISetMask(mask_bits, XI_RawMotion);
        evmask.deviceid = XIAllMasterDevices;
        evmask.mask_len = sizeof(mask_bits);
        evmask.mask = mask_bits;
        XISelectEvents(dpy, root, &evmask, 1);

        KeyCode grabbed_kc = 0;
        unsigned int grabbed_mods = 0;
        apply_x11_hotkey(dpy, root, grabbed_kc, grabbed_mods);
        XSync(dpy, 0);

        active_.store(true);
        tc::KeyTracker tracker;

        int xfd = ConnectionNumber(dpy);
        while (running_.load()) {
            while (XPending(dpy) > 0) {
                XEvent ev{};
                XNextEvent(dpy, &ev);

                if (ev.type == KeyPress && grabbed_kc != 0 &&
                    static_cast<KeyCode>(ev.xkey.keycode) == grabbed_kc) {
                    unsigned int clean = ev.xkey.state & ~(Mod2Mask | LockMask);
                    if (clean == grabbed_mods && cb_.on_wake_hotkey) {
                        cb_.on_wake_hotkey();
                    }
                    continue;
                }

                if (ev.xcookie.type == GenericEvent && ev.xcookie.extension == xi_opcode &&
                    XGetEventData(dpy, &ev.xcookie)) {
                    auto* raw = static_cast<XIRawEvent*>(ev.xcookie.data);
                    uintptr_t dev = static_cast<uintptr_t>(raw->sourceid ? raw->sourceid : raw->deviceid);
                    uint64_t ts = now_ms();

                    if (ev.xcookie.evtype == XI_RawKeyPress || ev.xcookie.evtype == XI_RawKeyRelease) {
                        bool is_break = (ev.xcookie.evtype == XI_RawKeyRelease);
                        uint16_t evcode = raw->detail >= 8 ? static_cast<uint16_t>(raw->detail - 8) : 0;
                        uint16_t scan = tc::evdev_to_scan(evcode);
                        uint16_t flags = is_break ? tc::TC_RI_KEY_BREAK : 0;
                        if (scan > 0xff) flags |= tc::TC_RI_KEY_E0;
                        uint16_t win_vk = canonical_to_win_vk(tc::canonical_code(scan));
                        auto code = tracker.process_event(dev, scan, flags, win_vk, ts, false);
                        if (code.has_value() && cb_.on_key) {
                            char keymap[32]{};
                            XQueryKeymap(dpy, keymap);
                            auto is_down = [&](uint16_t vk) -> bool {
                                switch (vk) {
                                    case 0xa2: return is_x11_key_down(dpy, XK_Control_L, keymap);
                                    case 0xa3: return is_x11_key_down(dpy, XK_Control_R, keymap);
                                    case 0xa4: return is_x11_key_down(dpy, XK_Alt_L, keymap);
                                    case 0xa5: return is_x11_key_down(dpy, XK_Alt_R, keymap);
                                    case 0xa0: return is_x11_key_down(dpy, XK_Shift_L, keymap);
                                    case 0xa1: return is_x11_key_down(dpy, XK_Shift_R, keymap);
                                    case 0x5b: return is_x11_key_down(dpy, XK_Super_L, keymap);
                                    case 0x5c: return is_x11_key_down(dpy, XK_Super_R, keymap);
                                    default: return false;
                                }
                            };
                            auto chord = tracker.shortcut_for(dev, *code, ts, is_down);
                            cb_.on_key(*code, win_vk, chord);
                        }
                    } else if (ev.xcookie.evtype == XI_RawButtonPress) {
                        int b = raw->detail;
                        if (b == 1 && cb_.on_mouse_click) cb_.on_mouse_click(1);
                        else if (b == 3 && cb_.on_mouse_click) cb_.on_mouse_click(2);
                        else if (b == 2 && cb_.on_mouse_click) cb_.on_mouse_click(3);
                        else if (b >= 4 && b <= 7 && cb_.on_mouse_scroll) cb_.on_mouse_scroll(1);
                        else if (b == 8 && cb_.on_mouse_click) cb_.on_mouse_click(4);
                        else if (b == 9 && cb_.on_mouse_click) cb_.on_mouse_click(5);
                    } else if (ev.xcookie.evtype == XI_RawMotion && cb_.on_mouse_move) {
                        double dx = 0.0, dy = 0.0;
                        const double* val = raw->raw_values;
                        int nbits = raw->valuators.mask_len * 8;
                        for (int i = 0; i < nbits; ++i) {
                            if (XIMaskIsSet(raw->valuators.mask, i)) {
                                if (i == 0) dx = *val;
                                else if (i == 1) dy = *val;
                                ++val;
                            }
                        }
                        double dist = std::hypot(dx, dy);
                        if (dist > 0.0 && dist <= 320.0) {
                            cb_.on_mouse_move(dist);
                        }
                    }

                    XFreeEventData(dpy, &ev.xcookie);
                }
            }

            {
                bool dirty = false;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    dirty = hotkey_dirty_;
                }
                if (dirty) apply_x11_hotkey(dpy, root, grabbed_kc, grabbed_mods);
            }

            struct pollfd fds[2]{};
            fds[0].fd = xfd;
            fds[0].events = POLLIN;
            fds[1].fd = wake_pipe_[0];
            fds[1].events = POLLIN;
            int nfds = (wake_pipe_[0] >= 0) ? 2 : 1;
            int rc = poll(fds, static_cast<nfds_t>(nfds), 500);
            if (rc > 0 && nfds == 2 && (fds[1].revents & POLLIN)) {
                char buf[32];
                while (read(wake_pipe_[0], buf, sizeof(buf)) > 0) {}
            }
        }
        active_.store(false);
    }

    void run_evdev_fallback() {
        std::vector<int> fds;
        DIR* dir = opendir("/dev/input");
        if (dir) {
            struct dirent* ent = nullptr;
            while ((ent = readdir(dir)) != nullptr) {
                if (std::strncmp(ent->d_name, "event", 5) != 0) continue;
                std::string p = std::string("/dev/input/") + ent->d_name;
                int fd = open(p.c_str(), O_RDONLY | O_NONBLOCK);
                if (fd >= 0) fds.push_back(fd);
            }
            closedir(dir);
        }

        if (fds.empty()) {
            permission_needed_.store(true);
            active_.store(false);
            return;
        }

        active_.store(true);
        tc::KeyTracker tracker;

        while (running_.load()) {
            std::vector<struct pollfd> pfds;
            for (int fd : fds) pfds.push_back({fd, POLLIN, 0});
            if (wake_pipe_[0] >= 0) pfds.push_back({wake_pipe_[0], POLLIN, 0});
            int rc = poll(pfds.data(), static_cast<nfds_t>(pfds.size()), 500);
            if (rc <= 0) continue;

            for (size_t i = 0; i < fds.size(); ++i) {
                if (!(pfds[i].revents & POLLIN)) continue;
                struct input_event ev{};
                double dx = 0.0, dy = 0.0;
                while (read(fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
                    uint64_t ts = now_ms();
                    if (ev.type == EV_KEY) {
                        if (ev.code == BTN_LEFT && ev.value == 1 && cb_.on_mouse_click) cb_.on_mouse_click(1);
                        else if (ev.code == BTN_RIGHT && ev.value == 1 && cb_.on_mouse_click) cb_.on_mouse_click(2);
                        else if (ev.code == BTN_MIDDLE && ev.value == 1 && cb_.on_mouse_click) cb_.on_mouse_click(3);
                        else if (ev.code == BTN_SIDE && ev.value == 1 && cb_.on_mouse_click) cb_.on_mouse_click(4);
                        else if (ev.code == BTN_EXTRA && ev.value == 1 && cb_.on_mouse_click) cb_.on_mouse_click(5);
                        else if (ev.code < BTN_MISC && (ev.value == 0 || ev.value == 1)) {
                            uint16_t scan = tc::evdev_to_scan(ev.code);
                            uint16_t flags = (ev.value == 0) ? tc::TC_RI_KEY_BREAK : 0;
                            if (scan > 0xff) flags |= tc::TC_RI_KEY_E0;
                            uint32_t canon = tc::canonical_code(scan);
                            uint16_t win_vk = canonical_to_win_vk(canon);
                            auto code = tracker.process_event(static_cast<uintptr_t>(fds[i]), scan, flags, win_vk, ts, false);
                            if (code.has_value() && cb_.on_key) {
                                auto chord = tracker.shortcut_for(static_cast<uintptr_t>(fds[i]), *code, ts,
                                                                  [](uint16_t) { return true; });
                                cb_.on_key(*code, win_vk, chord);
                            }
                        }
                    } else if (ev.type == EV_REL) {
                        if (ev.code == REL_X) dx += ev.value;
                        else if (ev.code == REL_Y) dy += ev.value;
                        else if ((ev.code == REL_WHEEL || ev.code == REL_HWHEEL) && cb_.on_mouse_scroll) {
                            cb_.on_mouse_scroll(1);
                        }
                    } else if (ev.type == EV_SYN && (dx != 0.0 || dy != 0.0) && cb_.on_mouse_move) {
                        double dist = std::hypot(dx, dy);
                        if (dist > 0.0 && dist <= 320.0) cb_.on_mouse_move(dist);
                        dx = 0.0;
                        dy = 0.0;
                    }
                }
            }
            if (wake_pipe_[0] >= 0 && pfds.back().revents & POLLIN) {
                char buf[32];
                while (read(wake_pipe_[0], buf, sizeof(buf)) > 0) {}
            }
        }

        for (int fd : fds) close(fd);
        active_.store(false);
    }

    InputCallbacks cb_;
    std::mutex mu_;
    std::string hotkey_spec_ = "Ctrl+Alt+Shift+T";
    bool hotkey_dirty_ = false;
    int wake_pipe_[2]{-1, -1};
    std::atomic<bool> running_{false};
    std::atomic<bool> active_{false};
    std::atomic<bool> permission_needed_{false};
    std::thread thread_;
};

} // namespace tc::linux_host
