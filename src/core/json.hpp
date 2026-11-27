#pragma once

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace tc {

struct JsonValue {
    enum Type { Null, Bool, Int, Double, String, Array, Object };
    Type type = Null;
    bool bval = false;
    int64_t ival = 0;
    double dval = 0.0;
    std::string sval;
    std::vector<JsonValue> arr;
    std::map<std::string, JsonValue> obj;

    JsonValue() = default;
    JsonValue(std::nullptr_t) : type(Null) {}
    JsonValue(bool v) : type(Bool), bval(v) {}
    template <typename T, typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value, int>::type = 0>
    JsonValue(T v) : type(Int), ival(static_cast<int64_t>(v)), dval(static_cast<double>(v)) {}
    JsonValue(double v) : type(Double), dval(v) {}
    JsonValue(const char* s) : type(String), sval(s ? s : "") {}
    JsonValue(std::string s) : type(String), sval(std::move(s)) {}

    static JsonValue array() {
        JsonValue v;
        v.type = Array;
        return v;
    }
    static JsonValue object() {
        JsonValue v;
        v.type = Object;
        return v;
    }

    bool is_null() const { return type == Null; }
    bool is_bool() const { return type == Bool; }
    bool is_number() const { return type == Int || type == Double; }
    bool is_string() const { return type == String; }
    bool is_array() const { return type == Array; }
    bool is_object() const { return type == Object; }

    bool as_bool(bool def = false) const { return type == Bool ? bval : def; }
    int64_t as_int(int64_t def = 0) const {
        if (type == Int) return ival;
        if (type == Double) return static_cast<int64_t>(dval);
        return def;
    }
    double as_double(double def = 0.0) const {
        if (type == Double) return dval;
        if (type == Int) return static_cast<double>(ival);
        return def;
    }
    const std::string& as_string() const {
        static const std::string empty;
        return type == String ? sval : empty;
    }
    std::string as_string_or(const std::string& def) const {
        return type == String ? sval : def;
    }

    bool has(const std::string& k) const {
        return type == Object && obj.find(k) != obj.end();
    }
    const JsonValue& get(const std::string& k) const {
        static const JsonValue nil;
        if (type != Object) return nil;
        auto it = obj.find(k);
        return it != obj.end() ? it->second : nil;
    }
    void set(const std::string& k, JsonValue v) {
        if (type != Object) {
            type = Object;
            obj.clear();
        }
        obj[k] = std::move(v);
    }
    void push(JsonValue v) {
        if (type != Array) {
            type = Array;
            arr.clear();
        }
        arr.push_back(std::move(v));
    }

    static std::string escape_string(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8);
        out.push_back('"');
        for (unsigned char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out.push_back(static_cast<char>(c));
                    }
                    break;
            }
        }
        out.push_back('"');
        return out;
    }

    std::string dump(int indent = -1, int depth = 0) const {
        std::string out;
        dump_into(out, indent, depth);
        return out;
    }

    void dump_into(std::string& out, int indent = -1, int depth = 0) const {
        switch (type) {
            case Null: out += "null"; break;
            case Bool: out += (bval ? "true" : "false"); break;
            case Int: out += std::to_string(ival); break;
            case Double: {
                if (!std::isfinite(dval)) {
                    out += "null";
                } else {
                    std::ostringstream oss;
                    oss << dval;
                    out += oss.str();
                }
                break;
            }
            case String: out += escape_string(sval); break;
            case Array: {
                if (arr.empty()) {
                    out += "[]";
                    break;
                }
                out.push_back('[');
                for (size_t i = 0; i < arr.size(); ++i) {
                    if (i > 0) out.push_back(',');
                    if (indent >= 0) {
                        out.push_back('\n');
                        out.append((depth + 1) * indent, ' ');
                    }
                    arr[i].dump_into(out, indent, depth + 1);
                }
                if (indent >= 0) {
                    out.push_back('\n');
                    out.append(depth * indent, ' ');
                }
                out.push_back(']');
                break;
            }
            case Object: {
                if (obj.empty()) {
                    out += "{}";
                    break;
                }
                out.push_back('{');
                size_t i = 0;
                for (const auto& kv : obj) {
                    if (i++ > 0) out.push_back(',');
                    if (indent >= 0) {
                        out.push_back('\n');
                        out.append((depth + 1) * indent, ' ');
                    }
                    out += escape_string(kv.first);
                    out.push_back(':');
                    if (indent >= 0) out.push_back(' ');
                    kv.second.dump_into(out, indent, depth + 1);
                }
                if (indent >= 0) {
                    out.push_back('\n');
                    out.append(depth * indent, ' ');
                }
                out.push_back('}');
                break;
            }
        }
    }

    static bool parse(const std::string& src, JsonValue& out) {
        size_t pos = 0;
        if (!parse_value(src, pos, out, 0)) return false;
        skip_ws(src, pos);
        return pos == src.size();
    }

private:
    static void skip_ws(const std::string& s, size_t& pos) {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) {
            ++pos;
        }
    }
    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0x10FFFF) {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    static bool parse_hex4(const std::string& s, size_t& pos, uint32_t& out) {
        if (pos + 4 > s.size()) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s[pos++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (c - '0');
            else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
            else return false;
        }
        out = v;
        return true;
    }
    static bool parse_string(const std::string& s, size_t& pos, std::string& out) {
        if (pos >= s.size() || s[pos] != '"') return false;
        ++pos;
        out.clear();
        while (pos < s.size()) {
            char c = s[pos++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c == '\\') {
                if (pos >= s.size()) return false;
                char esc = s[pos++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = 0;
                        if (!parse_hex4(s, pos, cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF && pos + 6 <= s.size() && s[pos] == '\\' && s[pos + 1] == 'u') {
                            size_t saved = pos;
                            pos += 2;
                            uint32_t low = 0;
                            if (parse_hex4(s, pos, low) && low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                            } else {
                                pos = saved;
                            }
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default: return false;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }
    static bool parse_value(const std::string& s, size_t& pos, JsonValue& out, int depth) {
        if (depth > 64) return false;
        skip_ws(s, pos);
        if (pos >= s.size()) return false;
        char c = s[pos];
        if (c == '"') {
            std::string str;
            if (!parse_string(s, pos, str)) return false;
            out = JsonValue(std::move(str));
            return true;
        }
        if (c == '{') {
            ++pos;
            out = JsonValue::object();
            skip_ws(s, pos);
            if (pos < s.size() && s[pos] == '}') { ++pos; return true; }
            while (pos < s.size()) {
                skip_ws(s, pos);
                std::string key;
                if (!parse_string(s, pos, key)) return false;
                skip_ws(s, pos);
                if (pos >= s.size() || s[pos] != ':') return false;
                ++pos;
                JsonValue val;
                if (!parse_value(s, pos, val, depth + 1)) return false;
                out.obj[std::move(key)] = std::move(val);
                skip_ws(s, pos);
                if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
                if (pos < s.size() && s[pos] == '}') { ++pos; return true; }
                return false;
            }
            return false;
        }
        if (c == '[') {
            ++pos;
            out = JsonValue::array();
            skip_ws(s, pos);
            if (pos < s.size() && s[pos] == ']') { ++pos; return true; }
            while (pos < s.size()) {
                JsonValue val;
                if (!parse_value(s, pos, val, depth + 1)) return false;
                out.arr.push_back(std::move(val));
                skip_ws(s, pos);
                if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
                if (pos < s.size() && s[pos] == ']') { ++pos; return true; }
                return false;
            }
            return false;
        }
        if (s.compare(pos, 4, "true") == 0) { pos += 4; out = JsonValue(true); return true; }
        if (s.compare(pos, 5, "false") == 0) { pos += 5; out = JsonValue(false); return true; }
        if (s.compare(pos, 4, "null") == 0) { pos += 4; out = JsonValue(nullptr); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t start = pos;
            if (s[pos] == '-') ++pos;
            if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) return false;
            while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
            bool is_float = false;
            if (pos < s.size() && s[pos] == '.') {
                is_float = true;
                ++pos;
                if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) return false;
                while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
            }
            if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
                is_float = true;
                ++pos;
                if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
                if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) return false;
                while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
            }
            std::string num_str = s.substr(start, pos - start);
            if (is_float) {
                char* endp = nullptr;
                double d = std::strtod(num_str.c_str(), &endp);
                if (endp != num_str.c_str() + num_str.size()) return false;
                out = JsonValue(d);
            } else {
                char* endp = nullptr;
                long long iv = std::strtoll(num_str.c_str(), &endp, 10);
                if (endp != num_str.c_str() + num_str.size()) return false;
                out = JsonValue(static_cast<int64_t>(iv));
            }
            return true;
        }
        return false;
    }
};

} // namespace tc
