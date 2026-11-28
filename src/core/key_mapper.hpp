#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace tc {

struct ShortcutChord {
    uint8_t modifiers = 0; // 1=Ctrl, 2=Alt, 4=Shift, 8=Win
    uint32_t key = 0;

    bool operator==(const ShortcutChord& o) const {
        return modifiers == o.modifiers && key == o.key;
    }
};

constexpr uint8_t MOD_CTRL = 1;
constexpr uint8_t MOD_ALT = 2;
constexpr uint8_t MOD_SHIFT = 4;
constexpr uint8_t MOD_WIN = 8;

constexpr uint16_t TC_RI_KEY_BREAK = 1;
constexpr uint16_t TC_RI_KEY_E0 = 2;
constexpr uint16_t TC_RI_KEY_E1 = 4;

constexpr uint16_t TC_VK_SHIFT = 0x10;
constexpr uint16_t TC_VK_PAUSE = 0x13;
constexpr uint16_t TC_VK_NUMLOCK = 0x90;

inline uint32_t canonical_code(uint16_t scan) {
    switch (scan) {
        case 0x56: return 10;
        case 0x120: return 74;
        case 0x12e: return 73;
        case 0x130: return 72;
        case 0x01: return 53;
        case 0x02: return 18;
        case 0x03: return 19;
        case 0x04: return 20;
        case 0x05: return 21;
        case 0x06: return 23;
        case 0x07: return 22;
        case 0x08: return 26;
        case 0x09: return 28;
        case 0x0a: return 25;
        case 0x0b: return 29;
        case 0x0c: return 27;
        case 0x0d: return 24;
        case 0x0e: return 51;
        case 0x0f: return 48;
        case 0x10: return 12;
        case 0x11: return 13;
        case 0x12: return 14;
        case 0x13: return 15;
        case 0x14: return 17;
        case 0x15: return 16;
        case 0x16: return 32;
        case 0x17: return 34;
        case 0x18: return 31;
        case 0x19: return 35;
        case 0x1a: return 33;
        case 0x1b: return 30;
        case 0x1c: return 36;
        case 0x1d: return 59;
        case 0x1e: return 0;
        case 0x1f: return 1;
        case 0x20: return 2;
        case 0x21: return 3;
        case 0x22: return 5;
        case 0x23: return 4;
        case 0x24: return 38;
        case 0x25: return 40;
        case 0x26: return 37;
        case 0x27: return 41;
        case 0x28: return 39;
        case 0x29: return 50;
        case 0x2a: return 56;
        case 0x2b: return 42;
        case 0x2c: return 6;
        case 0x2d: return 7;
        case 0x2e: return 8;
        case 0x2f: return 9;
        case 0x30: return 11;
        case 0x31: return 45;
        case 0x32: return 46;
        case 0x33: return 43;
        case 0x34: return 47;
        case 0x35: return 44;
        case 0x36: return 60;
        case 0x37: return 67;
        case 0x38: return 58;
        case 0x39: return 49;
        case 0x3a: return 57;
        case 0x3b: return 122;
        case 0x3c: return 120;
        case 0x3d: return 99;
        case 0x3e: return 118;
        case 0x3f: return 96;
        case 0x40: return 97;
        case 0x41: return 98;
        case 0x42: return 100;
        case 0x43: return 101;
        case 0x44: return 109;
        case 0x45: return 71;
        case 0x46: return 0x10046;
        case 0x47: return 89;
        case 0x48: return 91;
        case 0x49: return 92;
        case 0x4a: return 78;
        case 0x4b: return 86;
        case 0x4c: return 87;
        case 0x4d: return 88;
        case 0x4e: return 69;
        case 0x4f: return 83;
        case 0x50: return 84;
        case 0x51: return 85;
        case 0x52: return 82;
        case 0x53: return 65;
        case 0x57: return 103;
        case 0x58: return 111;
        case 0x64: return 105;
        case 0x65: return 107;
        case 0x66: return 113;
        case 0x67: return 106;
        case 0x68: return 64;
        case 0x69: return 79;
        case 0x6a: return 80;
        case 0x6b: return 90;
        case 0x11c: return 76;
        case 0x11d: return 62;
        case 0x135: return 75;
        case 0x54:
        case 0x137: return 0x10137;
        case 0x138: return 61;
        case 0x147: return 115;
        case 0x148: return 126;
        case 0x149: return 116;
        case 0x14b: return 123;
        case 0x14d: return 124;
        case 0x14f: return 119;
        case 0x150: return 125;
        case 0x151: return 121;
        case 0x152: return 114;
        case 0x153: return 117;
        case 0x15b: return 55;
        case 0x15c: return 54;
        case 0x15d: return 63;
        case 0x146:
        case 0x245: return 0x10245;
        default: return 0x10000u + static_cast<uint32_t>(scan);
    }
}

inline uint16_t evdev_to_scan(uint16_t evdev) {
    switch (evdev) {
        case 85: return 0x00;  // KEY_ZENKAKUHANKAKU
        case 86: return 0x56;  // KEY_102ND
        case 87: return 0x57;  // KEY_F11
        case 88: return 0x58;  // KEY_F12
        case 96: return 0x11c; // KEY_KPENTER
        case 97: return 0x11d; // KEY_RIGHTCTRL
        case 98: return 0x135; // KEY_KPSLASH
        case 99: return 0x137; // KEY_SYSRQ
        case 100: return 0x138; // KEY_RIGHTALT
        case 102: return 0x147; // KEY_HOME
        case 103: return 0x148; // KEY_UP
        case 104: return 0x149; // KEY_PAGEUP
        case 105: return 0x14b; // KEY_LEFT
        case 106: return 0x14d; // KEY_RIGHT
        case 107: return 0x14f; // KEY_END
        case 108: return 0x150; // KEY_DOWN
        case 109: return 0x151; // KEY_PAGEDOWN
        case 110: return 0x152; // KEY_INSERT
        case 111: return 0x153; // KEY_DELETE
        case 113: return 0x120; // KEY_MUTE
        case 114: return 0x12e; // KEY_VOLUMEDOWN
        case 115: return 0x130; // KEY_VOLUMEUP
        case 119: return 0x245; // KEY_PAUSE
        case 125: return 0x15b; // KEY_LEFTMETA
        case 126: return 0x15c; // KEY_RIGHTMETA
        case 127: return 0x15d; // KEY_COMPOSE
        case 183: return 0x64;  // KEY_F13
        case 184: return 0x65;  // KEY_F14
        case 185: return 0x66;  // KEY_F15
        case 186: return 0x67;  // KEY_F16
        case 187: return 0x68;  // KEY_F17
        case 188: return 0x69;  // KEY_F18
        case 189: return 0x6a;  // KEY_F19
        case 190: return 0x6b;  // KEY_F20
        default:
            if (evdev >= 1 && evdev <= 84) return evdev;
            return evdev;
    }
}

inline uint32_t mac_keycode_to_canonical(uint16_t vk) {
    // Apple HID / HIToolbox kVK_* matches canonical_code directly
    return static_cast<uint32_t>(vk);
}

inline uint16_t modifier_vk(uint32_t code) {
    switch (code) {
        case 59: return 0xa2; // LCtrl
        case 62: return 0xa3; // RCtrl
        case 58: return 0xa4; // LAlt
        case 61: return 0xa5; // RAlt / AltGr
        case 56: return 0xa0; // LShift
        case 60: return 0xa1; // RShift
        case 55: return 0x5b; // LWin
        case 54: return 0x5c; // RWin
        default: return 0;
    }
}

inline uint32_t code_for_us_char(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    switch (ch) {
        case 'A': return 0;
        case 'S': return 1;
        case 'D': return 2;
        case 'F': return 3;
        case 'H': return 4;
        case 'G': return 5;
        case 'Z': return 6;
        case 'X': return 7;
        case 'C': return 8;
        case 'V': return 9;
        case 'B': return 11;
        case 'Q': return 12;
        case 'W': return 13;
        case 'E': return 14;
        case 'R': return 15;
        case 'Y': return 16;
        case 'T': return 17;
        case '1': return 18;
        case '2': return 19;
        case '3': return 20;
        case '4': return 21;
        case '6': return 22;
        case '5': return 23;
        case '=': return 24;
        case '9': return 25;
        case '7': return 26;
        case '-': return 27;
        case '8': return 28;
        case '0': return 29;
        case ']': return 30;
        case 'O': return 31;
        case 'U': return 32;
        case '[': return 33;
        case 'I': return 34;
        case 'P': return 35;
        case 'L': return 37;
        case 'J': return 38;
        case '\'': return 39;
        case 'K': return 40;
        case ';': return 41;
        case '\\': return 42;
        case ',': return 43;
        case '/': return 44;
        case 'N': return 45;
        case 'M': return 46;
        case '.': return 47;
        case '`': return 50;
        default: return 0;
    }
}

class KeyTracker {
public:
    static constexpr uint64_t MODIFIER_GRACE_MS = 1500;

    void reset() {
        held_.clear();
        virtual_keys_.clear();
    }

    void remove_device(uintptr_t device) {
        for (auto it = held_.begin(); it != held_.end();) {
            if (it->first.first == device) it = held_.erase(it);
            else ++it;
        }
        for (auto it = virtual_keys_.begin(); it != virtual_keys_.end();) {
            if (it->first.first == device) it = virtual_keys_.erase(it);
            else ++it;
        }
    }

    void reconcile_foreground(const std::function<bool(uint16_t)>& is_down) {
        for (auto it = held_.begin(); it != held_.end();) {
            uint16_t vk = modifier_vk(canonical_code(it->first.second));
            if (vk == 0) {
                auto vkit = virtual_keys_.find(it->first);
                if (vkit != virtual_keys_.end()) vk = vkit->second;
            }
            if (vk != 0 && is_down(vk)) {
                ++it;
            } else {
                it = held_.erase(it);
            }
        }
        for (auto it = virtual_keys_.begin(); it != virtual_keys_.end();) {
            if (held_.find(it->first) == held_.end()) it = virtual_keys_.erase(it);
            else ++it;
        }
    }

    std::optional<uint32_t> process_event(uintptr_t device, uint16_t make, uint16_t flags, uint16_t vk, uint64_t now_ms, bool ignore_software) {
        if (ignore_software && device == 0) return std::nullopt;
        return process_event_raw(device, make, flags, vk, now_ms);
    }

    std::optional<ShortcutChord> shortcut_for(
        uintptr_t device,
        uint32_t code,
        uint64_t now_ms,
        const std::function<bool(uint16_t)>& is_down,
        const std::function<char(uint32_t)>& translate_layout = nullptr)
    {
        if (modifier_vk(code) != 0) return std::nullopt;

        for (auto it = held_.begin(); it != held_.end();) {
            bool same_domain = (it->first.first == 0) == (device == 0);
            bool fresh = (now_ms >= it->second) && (now_ms - it->second <= MODIFIER_GRACE_MS);
            if (!same_domain || fresh) {
                ++it;
                continue;
            }
            uint16_t mvk = modifier_vk(canonical_code(it->first.second));
            if (mvk == 0 || is_down(mvk)) {
                ++it;
            } else {
                virtual_keys_.erase(it->first);
                it = held_.erase(it);
            }
        }

        uint8_t mods = 0;
        for (const auto& kv : held_) {
            if ((kv.first.first == 0) != (device == 0)) continue;
            uint32_t c = canonical_code(kv.first.second);
            if (c == 61) return std::nullopt; // Ignore AltGr character composition
            if (c == 59 || c == 62) mods |= MOD_CTRL;
            else if (c == 58) mods |= MOD_ALT;
            else if (c == 56 || c == 60) mods |= MOD_SHIFT;
            else if (c == 55 || c == 54) mods |= MOD_WIN;
        }

        if (mods == 0) return std::nullopt;
        // Exclude bare Shift+key from shortcut rankings unless paired with Ctrl/Alt/Win
        // or a non-typing navigation/function key
        if (mods == MOD_SHIFT) return std::nullopt;

        uint32_t final_key = code;
        if (translate_layout) {
            char ch = translate_layout(code);
            if (ch != 0) {
                uint32_t mapped = code_for_us_char(ch);
                if (mapped != 0 || (ch == 'A' || ch == 'a')) {
                    final_key = mapped;
                }
            }
        }
        return ShortcutChord{mods, final_key};
    }

    size_t held_count() const { return held_.size(); }

private:
    std::map<std::pair<uintptr_t, uint16_t>, uint64_t> held_;
    std::map<std::pair<uintptr_t, uint16_t>, uint16_t> virtual_keys_;

    std::optional<uint32_t> process_event_raw(uintptr_t device, uint16_t make, uint16_t flags, uint16_t vk, uint64_t now_ms) {
        if (vk >= 255 || make == 0xff) return std::nullopt;
        uint16_t raw_id = static_cast<uint16_t>(make | ((flags & 6) << 7));
        if (raw_id == 0x12a || raw_id == 0x21d) return std::nullopt;
        if (vk == TC_VK_SHIFT && ((raw_id >= 0x47 && raw_id <= 0x49) || (raw_id >= 0x4b && raw_id <= 0x4d) || (raw_id >= 0x4f && raw_id <= 0x53))) {
            return std::nullopt;
        }
        if (vk == TC_VK_PAUSE) {
            if (flags & TC_RI_KEY_BREAK) {
                held_.erase({device, 0x245});
                return std::nullopt;
            }
            if (held_.find({device, 0x245}) == held_.end()) {
                return canonical_code(0x245);
            }
            return std::nullopt;
        }

        uint16_t identity = raw_id;
        if (vk == TC_VK_NUMLOCK) {
            identity = 0x45;
        } else if (make == 0) {
            identity = static_cast<uint16_t>(0x8000u | vk);
        } else if (raw_id == 0x54) {
            identity = 0x137;
        } else if (raw_id == 0x146) {
            identity = 0x245;
        }

        auto key = std::make_pair(device, identity);
        if ((flags & TC_RI_KEY_BREAK) == 0) {
            virtual_keys_[key] = vk;
            auto it = held_.find(key);
            if (it == held_.end()) {
                held_[key] = now_ms;
                return canonical_code(identity);
            }
            it->second = now_ms; // refresh timestamp on repeat without recounting
            return std::nullopt;
        } else {
            virtual_keys_.erase(key);
            bool was_held = held_.erase(key) > 0;
            if (identity == 0x137 && !was_held) {
                return canonical_code(identity);
            }
            return std::nullopt;
        }
    }
};

inline std::vector<uint32_t> extract_mouse_buttons(uint16_t flags) {
    std::vector<uint32_t> out;
    if (flags & 1) out.push_back(1);    // Left
    if (flags & 4) out.push_back(2);    // Right
    if (flags & 16) out.push_back(3);   // Middle
    if (flags & 64) out.push_back(4);   // Side Back (X1)
    if (flags & 256) out.push_back(5);  // Side Forward (X2)
    return out;
}

} // namespace tc
