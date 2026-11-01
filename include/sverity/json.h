// Minimal, dependency-free JSON value / parser / writer.
// Objects keep insertion order so reports are deterministic.
#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace sverity::json {

class Value;
using Array = std::vector<Value>;
using Member = std::pair<std::string, Value>;
using Object = std::vector<Member>;

class Value {
public:
    Value() = default;                       // null
    Value(std::nullptr_t) {}
    Value(bool b) : v_(b) {}
    Value(int i) : v_(static_cast<int64_t>(i)) {}
    Value(unsigned i) : v_(static_cast<int64_t>(i)) {}
    Value(int64_t i) : v_(i) {}
    Value(uint64_t i) : v_(static_cast<int64_t>(i)) {}
    Value(double d) : v_(d) {}
    Value(const char* s) : v_(std::string(s)) {}
    Value(std::string s) : v_(std::move(s)) {}
    Value(std::string_view s) : v_(std::string(s)) {}
    Value(Array a) : v_(std::move(a)) {}
    Value(Object o) : v_(std::move(o)) {}

    static Value array() { return Value(Array{}); }
    static Value object() { return Value(Object{}); }

    bool isNull() const { return std::holds_alternative<std::monostate>(v_); }
    bool isBool() const { return std::holds_alternative<bool>(v_); }
    bool isInt() const { return std::holds_alternative<int64_t>(v_); }
    bool isNumber() const { return isInt() || std::holds_alternative<double>(v_); }
    bool isString() const { return std::holds_alternative<std::string>(v_); }
    bool isArray() const { return std::holds_alternative<Array>(v_); }
    bool isObject() const { return std::holds_alternative<Object>(v_); }

    bool asBool(bool def = false) const { return isBool() ? std::get<bool>(v_) : def; }
    int64_t asInt(int64_t def = 0) const;
    double asDouble(double def = 0) const;
    const std::string& asString() const;     // empty string if not a string
    const Array& asArray() const;            // empty array if not an array
    const Object& asObject() const;          // empty object if not an object
    Array& arr();                            // converts to array if null
    Object& obj();                           // converts to object if null

    /// Object member lookup; returns a shared null Value when absent.
    const Value& operator[](std::string_view key) const;
    const Value* find(std::string_view key) const;
    /// Appends (or replaces) an object member.
    Value& set(std::string key, Value v);
    Value& push(Value v);

    /// indent < 0 -> compact. Output is deterministic.
    std::string dump(int indent = 2) const;

private:
    void dumpTo(std::string& out, int indent, int depth) const;
    std::variant<std::monostate, bool, int64_t, double, std::string, Array, Object> v_;
};

struct ParseResult {
    Value value;
    bool ok = false;
    std::string error;   // "line:col: message" on failure
};

ParseResult parse(std::string_view text);
std::string escape(std::string_view s);

} // namespace sverity::json
