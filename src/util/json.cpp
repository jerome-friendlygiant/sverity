#include "sverity/json.h"

#include <charconv>
#include <cmath>
#include <cstdio>

namespace sverity::json {

static const Value kNull;
static const std::string kEmptyString;
static const Array kEmptyArray;
static const Object kEmptyObject;

int64_t Value::asInt(int64_t def) const {
    if (isInt()) return std::get<int64_t>(v_);
    if (auto* d = std::get_if<double>(&v_)) return static_cast<int64_t>(*d);
    return def;
}
double Value::asDouble(double def) const {
    if (auto* d = std::get_if<double>(&v_)) return *d;
    if (isInt()) return static_cast<double>(std::get<int64_t>(v_));
    return def;
}
const std::string& Value::asString() const {
    return isString() ? std::get<std::string>(v_) : kEmptyString;
}
const Array& Value::asArray() const { return isArray() ? std::get<Array>(v_) : kEmptyArray; }
const Object& Value::asObject() const { return isObject() ? std::get<Object>(v_) : kEmptyObject; }
Array& Value::arr() {
    if (!isArray()) v_ = Array{};
    return std::get<Array>(v_);
}
Object& Value::obj() {
    if (!isObject()) v_ = Object{};
    return std::get<Object>(v_);
}
const Value* Value::find(std::string_view key) const {
    if (!isObject()) return nullptr;
    for (auto& [k, v] : std::get<Object>(v_))
        if (k == key) return &v;
    return nullptr;
}
const Value& Value::operator[](std::string_view key) const {
    auto* p = find(key);
    return p ? *p : kNull;
}
Value& Value::set(std::string key, Value v) {
    auto& o = obj();
    for (auto& m : o)
        if (m.first == key) {
            m.second = std::move(v);
            return *this;
        }
    o.emplace_back(std::move(key), std::move(v));
    return *this;
}
Value& Value::push(Value v) {
    arr().push_back(std::move(v));
    return *this;
}

// Writes valid UTF-8 only: invalid bytes (and lone surrogates) become U+FFFD so reports always decode.
std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    size_t i = 0;
    auto cont = [&](size_t k) { return k < s.size() && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80; };
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04x", c);
                        out += buf;
                    } else out += static_cast<char>(c);
            }
            i++;
            continue;
        }
        size_t len = 0;
        uint32_t cp = 0;
        if (c >= 0xC2 && c <= 0xDF && cont(i + 1)) { len = 2; cp = ((c & 0x1Fu) << 6) | (s[i + 1] & 0x3Fu); }
        else if (c >= 0xE0 && c <= 0xEF && cont(i + 1) && cont(i + 2)) {
            len = 3;
            cp = ((c & 0x0Fu) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu);
            if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) len = 0;
        } else if (c >= 0xF0 && c <= 0xF4 && cont(i + 1) && cont(i + 2) && cont(i + 3)) {
            len = 4;
            cp = ((c & 0x07u) << 18) | ((s[i + 1] & 0x3Fu) << 12) | ((s[i + 2] & 0x3Fu) << 6) | (s[i + 3] & 0x3Fu);
            if (cp < 0x10000 || cp > 0x10FFFF) len = 0;
        }
        if (len) { out.append(s.substr(i, len)); i += len; }
        else { out += "\xEF\xBF\xBD"; i++; }
    }
    return out;
}

void Value::dumpTo(std::string& out, int indent, int depth) const {
    auto nl = [&](int d) {
        if (indent < 0) return;
        out += '\n';
        out.append(static_cast<size_t>(d * indent), ' ');
    };
    if (isNull()) out += "null";
    else if (isBool()) out += std::get<bool>(v_) ? "true" : "false";
    else if (isInt()) out += std::to_string(std::get<int64_t>(v_));
    else if (auto* d = std::get_if<double>(&v_)) {
        if (!std::isfinite(*d)) out += "null";
        else {
            char buf[40];
            auto r = std::to_chars(buf, buf + sizeof buf, *d);
            out.append(buf, r.ptr);
        }
    } else if (isString()) {
        out += '"';
        out += escape(std::get<std::string>(v_));
        out += '"';
    } else if (isArray()) {
        auto& a = std::get<Array>(v_);
        if (a.empty()) { out += "[]"; return; }
        out += '[';
        for (size_t i = 0; i < a.size(); i++) {
            if (i) out += ',';
            nl(depth + 1);
            a[i].dumpTo(out, indent, depth + 1);
        }
        nl(depth);
        out += ']';
    } else {
        auto& o = std::get<Object>(v_);
        if (o.empty()) { out += "{}"; return; }
        out += '{';
        for (size_t i = 0; i < o.size(); i++) {
            if (i) out += ',';
            nl(depth + 1);
            out += '"';
            out += escape(o[i].first);
            out += indent < 0 ? "\":" : "\": ";
            o[i].second.dumpTo(out, indent, depth + 1);
        }
        nl(depth);
        out += '}';
    }
}

std::string Value::dump(int indent) const {
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

// ---------------------------------------------------------------- parser
namespace {
struct Parser {
    std::string_view s{};
    size_t i = 0;
    std::string err{};

    bool fail(const char* msg) {
        if (err.empty()) {
            size_t line = 1, col = 1;
            for (size_t k = 0; k < i && k < s.size(); k++) {
                if (s[k] == '\n') { line++; col = 1; } else col++;
            }
            err = std::to_string(line) + ":" + std::to_string(col) + ": " + msg;
        }
        return false;
    }
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    }
    bool lit(std::string_view w) {
        if (s.substr(i, w.size()) == w) { i += w.size(); return true; }
        return false;
    }
    static void utf8(std::string& o, uint32_t cp) {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) {
            o += static_cast<char>(0xC0 | (cp >> 6));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            o += static_cast<char>(0xE0 | (cp >> 12));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            o += static_cast<char>(0xF0 | (cp >> 18));
            o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(uint32_t& out) {
        if (i + 4 > s.size()) return fail("truncated \\u escape");
        out = 0;
        for (int k = 0; k < 4; k++) {
            char c = s[i++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
            else return fail("bad hex digit in \\u escape");
        }
        return true;
    }
    bool str(std::string& out) {
        if (i >= s.size() || s[i] != '"') return fail("expected string");
        i++;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && s.substr(i, 2) == "\\u") {
                        i += 2;
                        uint32_t lo = 0;
                        if (!hex4(lo)) return false;
                        if (lo < 0xDC00 || lo > 0xDFFF) return fail("invalid low surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                        return fail("lone surrogate in \\u escape");
                    }
                    utf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool value(Value& out, int depth) {
        if (depth > 256) return fail("nesting too deep");
        ws();
        if (i >= s.size()) return fail("unexpected end of input");
        char c = s[i];
        if (c == '{') {
            i++;
            Object o;
            ws();
            if (i < s.size() && s[i] == '}') { i++; out = Value(std::move(o)); return true; }
            for (;;) {
                ws();
                std::string k;
                if (!str(k)) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                i++;
                Value v;
                if (!value(v, depth + 1)) return false;
                o.emplace_back(std::move(k), std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == '}') { i++; break; }
                return fail("expected ',' or '}'");
            }
            out = Value(std::move(o));
            return true;
        }
        if (c == '[') {
            i++;
            Array a;
            ws();
            if (i < s.size() && s[i] == ']') { i++; out = Value(std::move(a)); return true; }
            for (;;) {
                Value v;
                if (!value(v, depth + 1)) return false;
                a.push_back(std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == ']') { i++; break; }
                return fail("expected ',' or ']'");
            }
            out = Value(std::move(a));
            return true;
        }
        if (c == '"') {
            std::string str_;
            if (!str(str_)) return false;
            out = Value(std::move(str_));
            return true;
        }
        if (lit("true")) { out = Value(true); return true; }
        if (lit("false")) { out = Value(false); return true; }
        if (lit("null")) { out = Value(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t st = i;
            bool isFloat = false;
            if (s[i] == '-') i++;
            while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' ||
                                    s[i] == 'E' || s[i] == '+' || s[i] == '-')) {
                if (s[i] == '.' || s[i] == 'e' || s[i] == 'E') isFloat = true;
                i++;
            }
            std::string_view n = s.substr(st, i - st);
            if (!isFloat) {
                int64_t v = 0;
                auto r = std::from_chars(n.data(), n.data() + n.size(), v);
                if (r.ec == std::errc() && r.ptr == n.data() + n.size()) { out = Value(v); return true; }
            }
            double d = 0;
            auto r = std::from_chars(n.data(), n.data() + n.size(), d);
            if (r.ec != std::errc() || r.ptr != n.data() + n.size()) return fail("bad number");
            out = Value(d);
            return true;
        }
        return fail("unexpected character");
    }
};
} // namespace

ParseResult parse(std::string_view text) {
    Parser p;
    p.s = text;
    ParseResult r;
    if (p.value(r.value, 0)) {
        p.ws();
        if (p.i != text.size()) p.fail("trailing characters after JSON value");
    }
    r.ok = p.err.empty();
    r.error = std::move(p.err);
    if (!r.ok) r.value = Value();
    return r;
}

} // namespace sverity::json
