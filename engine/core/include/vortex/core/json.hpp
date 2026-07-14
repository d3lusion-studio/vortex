#pragma once
#include "vortex/core/types.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vortex::json {

// A small DOM. Scene files, prefabs and cooked-asset manifests are tens of
// kilobytes at most, so this favours being obvious over being fast: no arena, no
// string interning, and reads of a missing key hand back a shared null rather than
// throwing. Nothing here is on a frame's hot path.
class Value {
public:
    enum class Type : u8 { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(bool v) : m_type(Type::Bool), m_bool(v) {}                        // NOLINT(*-explicit-*)
    Value(f64 v) : m_type(Type::Number), m_number(v) {}                     // NOLINT(*-explicit-*)
    Value(f32 v) : m_type(Type::Number), m_number(static_cast<f64>(v)) {}   // NOLINT(*-explicit-*)
    Value(i32 v) : m_type(Type::Number), m_number(static_cast<f64>(v)) {}   // NOLINT(*-explicit-*)
    Value(u32 v) : m_type(Type::Number), m_number(static_cast<f64>(v)) {}   // NOLINT(*-explicit-*)
    Value(std::string v) : m_type(Type::String), m_string(std::move(v)) {}  // NOLINT(*-explicit-*)
    Value(const char* v) : m_type(Type::String), m_string(v) {}             // NOLINT(*-explicit-*)

    [[nodiscard]] static Value object() { Value v; v.m_type = Type::Object; return v; }
    [[nodiscard]] static Value array()  { Value v; v.m_type = Type::Array;  return v; }

    [[nodiscard]] Type type()     const noexcept { return m_type; }
    [[nodiscard]] bool isNull()   const noexcept { return m_type == Type::Null; }
    [[nodiscard]] bool isBool()   const noexcept { return m_type == Type::Bool; }
    [[nodiscard]] bool isNumber() const noexcept { return m_type == Type::Number; }
    [[nodiscard]] bool isString() const noexcept { return m_type == Type::String; }
    [[nodiscard]] bool isArray()  const noexcept { return m_type == Type::Array; }
    [[nodiscard]] bool isObject() const noexcept { return m_type == Type::Object; }

    // Reads are total: the wrong type, or a key that is not there, yields the
    // fallback. A scene file gaining a field should not break an older loader.
    [[nodiscard]] bool        asBool(bool fallback = false) const noexcept;
    [[nodiscard]] f64         asNumber(f64 fallback = 0.0) const noexcept;
    [[nodiscard]] f32         asF32(f32 fallback = 0.0f) const noexcept;
    [[nodiscard]] i32         asI32(i32 fallback = 0) const noexcept;
    [[nodiscard]] u32         asU32(u32 fallback = 0) const noexcept;
    [[nodiscard]] std::string asString(std::string_view fallback = {}) const;

    // Object access. Missing keys read back as null.
    [[nodiscard]] const Value& operator[](std::string_view key) const;
    [[nodiscard]] bool         contains(std::string_view key) const;
    Value&                     set(std::string key, Value value);

    // The stored value for `key`, creating it (as null) if it is not there. This is what lets a
    // caller walk INTO a nested object and edit it in place; without it, building a path means
    // copying each level back over itself, which works and is nonsense.
    Value&                     at(std::string_view key);

    // Array access. Out-of-range reads back as null.
    [[nodiscard]] const Value& operator[](usize index) const;
    Value&                     push(Value value);

    [[nodiscard]] usize size() const noexcept;

    [[nodiscard]] const std::vector<Value>& items() const noexcept { return m_array; }
    [[nodiscard]] const std::vector<std::pair<std::string, Value>>& fields() const noexcept {
        return m_object;
    }

private:
    Type        m_type = Type::Null;
    bool        m_bool = false;
    f64         m_number = 0.0;
    std::string m_string;

    // Insertion-ordered, so a file round-trips to a stable diff. Objects here hold
    // a handful of keys, and a linear scan of those beats a hash of them.
    std::vector<Value>                            m_array;
    std::vector<std::pair<std::string, Value>>    m_object;
};

// Returns a null Value and fills `error` on malformed input.
[[nodiscard]] Value parse(std::string_view text, std::string* error = nullptr);

[[nodiscard]] std::string write(const Value& value, bool pretty = true);

}
