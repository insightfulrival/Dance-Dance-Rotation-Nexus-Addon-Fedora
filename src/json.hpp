// Small strict JSON reader (RFC 8259) for song files and clipboard imports.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace json {

struct Value {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Value> items;
    std::vector<std::pair<std::string, Value>> members;

    // Newtonsoft matches property names exactly first, then case-insensitively.
    const Value* find(const char* key) const {
        if (kind != Object) return nullptr;
        for (const auto& member : members)
            if (member.first == key) return &member.second;
        for (const auto& member : members) {
            if (member.first.size() != std::strlen(key)) continue;
            bool same = true;
            for (std::size_t i = 0; i < member.first.size() && same; ++i) {
                char a = member.first[i], b = key[i];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
                same = a == b;
            }
            if (same) return &member.second;
        }
        return nullptr;
    }
};

class Parser {
public:
    explicit Parser(const std::string& input) : s(input.data()), end(input.data() + input.size()) {
        if (end - s >= 3 && std::memcmp(s, "\xEF\xBB\xBF", 3) == 0) s += 3; // UTF-8 BOM
    }
    Value parse() {
        Value value = parse_value(0);
        skip();
        if (s != end) fail("trailing characters");
        return value;
    }

private:
    const char* s;
    const char* end;
    [[noreturn]] void fail(const char* what) { throw std::runtime_error(std::string("Invalid JSON: ") + what); }
    void skip() { while (s < end && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) ++s; }
    bool literal(const char* word) {
        std::size_t n = std::strlen(word);
        if (static_cast<std::size_t>(end - s) >= n && std::memcmp(s, word, n) == 0) { s += n; return true; }
        return false;
    }
    Value parse_value(int depth) {
        if (depth > 256) fail("nesting too deep");
        skip();
        if (s >= end) fail("unexpected end");
        Value v;
        switch (*s) {
            case '{': {
                ++s; v.kind = Value::Object; skip();
                if (s < end && *s == '}') { ++s; return v; }
                for (;;) {
                    skip();
                    if (s >= end || *s != '"') fail("expected key");
                    std::string key = parse_string();
                    skip();
                    if (s >= end || *s != ':') fail("expected ':'");
                    ++s;
                    v.members.emplace_back(std::move(key), parse_value(depth + 1));
                    skip();
                    if (s < end && *s == ',') { ++s; continue; }
                    if (s < end && *s == '}') { ++s; return v; }
                    fail("expected ',' or '}'");
                }
            }
            case '[': {
                ++s; v.kind = Value::Array; skip();
                if (s < end && *s == ']') { ++s; return v; }
                for (;;) {
                    v.items.push_back(parse_value(depth + 1));
                    skip();
                    if (s < end && *s == ',') { ++s; continue; }
                    if (s < end && *s == ']') { ++s; return v; }
                    fail("expected ',' or ']'");
                }
            }
            case '"': v.kind = Value::String; v.text = parse_string(); return v;
            case 't': if (literal("true")) { v.kind = Value::Bool; v.boolean = true; return v; } break;
            case 'f': if (literal("false")) { v.kind = Value::Bool; return v; } break;
            case 'n': if (literal("null")) return v; break;
            default: if (*s == '-' || (*s >= '0' && *s <= '9')) { v.kind = Value::Number; v.number = parse_number(); return v; }
        }
        fail("unexpected character");
    }
    double parse_number() {
        const char* start = s;
        if (*s == '-') ++s;
        if (s < end && *s == '0') ++s;
        else if (s < end && *s >= '1' && *s <= '9') while (s < end && *s >= '0' && *s <= '9') ++s;
        else fail("bad number");
        if (s < end && *s == '.') {
            ++s;
            if (s >= end || *s < '0' || *s > '9') fail("bad fraction");
            while (s < end && *s >= '0' && *s <= '9') ++s;
        }
        if (s < end && (*s == 'e' || *s == 'E')) {
            ++s;
            if (s < end && (*s == '+' || *s == '-')) ++s;
            if (s >= end || *s < '0' || *s > '9') fail("bad exponent");
            while (s < end && *s >= '0' && *s <= '9') ++s;
        }
        std::string token(start, s);
        return std::strtod(token.c_str(), nullptr);
    }
    unsigned hex4() {
        if (end - s < 4) fail("bad escape");
        unsigned value = 0;
        for (int i = 0; i < 4; ++i, ++s) {
            char c = *s;
            value <<= 4;
            if (c >= '0' && c <= '9') value |= c - '0';
            else if (c >= 'a' && c <= 'f') value |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') value |= c - 'A' + 10;
            else fail("bad escape");
        }
        return value;
    }
    static void append_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    std::string parse_string() {
        ++s; // opening quote
        std::string out;
        for (;;) {
            if (s >= end) fail("unterminated string");
            char c = *s++;
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\') { out += c; continue; }
            if (s >= end) fail("bad escape");
            switch (*s++) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && end - s >= 6 && s[0] == '\\' && s[1] == 'u') {
                        s += 2;
                        unsigned low = hex4();
                        if (low >= 0xDC00 && low <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        else { append_utf8(out, 0xFFFD); cp = low; }
                    }
                    if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
                    append_utf8(out, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
    }
};

inline Value parse(const std::string& text) { return Parser(text).parse(); }

} // namespace json
