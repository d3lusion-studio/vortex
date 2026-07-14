#include "vortex/core/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>

namespace vortex::json {

namespace {

const Value& nullValue() {
    static const Value kNull;
    return kNull;
}

}

bool Value::asBool(bool fallback) const noexcept {
    return m_type == Type::Bool ? m_bool : fallback;
}

f64 Value::asNumber(f64 fallback) const noexcept {
    return m_type == Type::Number ? m_number : fallback;
}

f32 Value::asF32(f32 fallback) const noexcept {
    return m_type == Type::Number ? static_cast<f32>(m_number) : fallback;
}

i32 Value::asI32(i32 fallback) const noexcept {
    return m_type == Type::Number ? static_cast<i32>(m_number) : fallback;
}

u32 Value::asU32(u32 fallback) const noexcept {
    return m_type == Type::Number ? static_cast<u32>(m_number) : fallback;
}

std::string Value::asString(std::string_view fallback) const {
    return m_type == Type::String ? m_string : std::string(fallback);
}

const Value& Value::operator[](std::string_view key) const {
    if (m_type != Type::Object) return nullValue();
    for (const auto& [name, value] : m_object)
        if (name == key) return value;
    return nullValue();
}

bool Value::contains(std::string_view key) const {
    if (m_type != Type::Object) return false;
    for (const auto& [name, value] : m_object)
        if (name == key) return true;
    return false;
}

Value& Value::set(std::string key, Value value) {
    m_type = Type::Object;
    for (auto& [name, existing] : m_object) {
        if (name == key) {
            existing = std::move(value);
            return existing;
        }
    }
    m_object.emplace_back(std::move(key), std::move(value));
    return m_object.back().second;
}

Value& Value::at(std::string_view key) {
    if (m_type != Type::Object) {
        // Turning a non-object into one loses whatever it held. That is the honest outcome: a
        // caller asking for a member of a number has already contradicted itself, and silently
        // doing nothing would hide it.
        *this = Value::object();
    }
    for (auto& [k, v] : m_object)
        if (k == key) return v;
    m_object.emplace_back(std::string(key), Value{});
    return m_object.back().second;
}

const Value& Value::operator[](usize index) const {
    if (m_type != Type::Array || index >= m_array.size()) return nullValue();
    return m_array[index];
}

Value& Value::push(Value value) {
    m_type = Type::Array;
    m_array.push_back(std::move(value));
    return m_array.back();
}

usize Value::size() const noexcept {
    switch (m_type) {
        case Type::Array:  return m_array.size();
        case Type::Object: return m_object.size();
        default:           return 0;
    }
}

// ----------------------------------------------------------------------- parse

namespace {

class Parser {
public:
    Parser(std::string_view text, std::string& error) : m_text(text), m_error(error) {}

    [[nodiscard]] Value run() {
        skipSpace();
        Value v = parseValue();
        if (!m_error.empty()) return {};
        skipSpace();
        if (m_pos != m_text.size()) {
            fail("trailing characters after the top-level value");
            return {};
        }
        return v;
    }

private:
    void fail(const char* what) {
        if (m_error.empty()) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "json: %s at offset %zu", what, m_pos);
            m_error = buf;
        }
    }

    [[nodiscard]] bool done() const { return m_pos >= m_text.size(); }
    [[nodiscard]] char peek() const { return done() ? '\0' : m_text[m_pos]; }

    void skipSpace() {
        while (!done()) {
            const char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++m_pos; continue; }
            break;
        }
    }

    [[nodiscard]] bool literal(std::string_view word) {
        if (m_text.compare(m_pos, word.size(), word) != 0) return false;
        m_pos += word.size();
        return true;
    }

    Value parseValue() {
        if (done()) { fail("unexpected end of input"); return {}; }
        switch (peek()) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': return Value(parseString());
            case 't': if (literal("true"))  return Value(true);  fail("bad literal"); return {};
            case 'f': if (literal("false")) return Value(false); fail("bad literal"); return {};
            case 'n': if (literal("null"))  return Value();      fail("bad literal"); return {};
            default:  return parseNumber();
        }
    }

    Value parseObject() {
        Value obj = Value::object();
        ++m_pos;   // '{'
        skipSpace();
        if (peek() == '}') { ++m_pos; return obj; }

        for (;;) {
            skipSpace();
            if (peek() != '"') { fail("expected a key"); return {}; }
            std::string key = parseString();
            if (!m_error.empty()) return {};

            skipSpace();
            if (peek() != ':') { fail("expected ':'"); return {}; }
            ++m_pos;

            skipSpace();
            Value value = parseValue();
            if (!m_error.empty()) return {};
            obj.set(std::move(key), std::move(value));

            skipSpace();
            if (peek() == ',') { ++m_pos; continue; }
            if (peek() == '}') { ++m_pos; return obj; }
            fail("expected ',' or '}'");
            return {};
        }
    }

    Value parseArray() {
        Value arr = Value::array();
        ++m_pos;   // '['
        skipSpace();
        if (peek() == ']') { ++m_pos; return arr; }

        for (;;) {
            skipSpace();
            Value value = parseValue();
            if (!m_error.empty()) return {};
            arr.push(std::move(value));

            skipSpace();
            if (peek() == ',') { ++m_pos; continue; }
            if (peek() == ']') { ++m_pos; return arr; }
            fail("expected ',' or ']'");
            return {};
        }
    }

    std::string parseString() {
        std::string out;
        ++m_pos;   // opening quote
        while (!done()) {
            const char c = m_text[m_pos++];
            if (c == '"') return out;
            if (c != '\\') { out.push_back(c); continue; }

            if (done()) break;
            const char esc = m_text[m_pos++];
            switch (esc) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    // Only the BMP, and only as UTF-8. Surrogate pairs are passed
                    // through as their replacement — no scene key needs them.
                    if (m_pos + 4 > m_text.size()) { fail("truncated \\u escape"); return {}; }
                    u32 code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = m_text[m_pos + static_cast<usize>(i)];
                        code <<= 4;
                        if (h >= '0' && h <= '9')      code |= static_cast<u32>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<u32>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<u32>(h - 'A' + 10);
                        else { fail("bad \\u escape"); return {}; }
                    }
                    m_pos += 4;
                    if (code < 0x80) {
                        out.push_back(static_cast<char>(code));
                    } else if (code < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default: fail("unknown escape"); return {};
            }
        }
        fail("unterminated string");
        return {};
    }

    Value parseNumber() {
        const usize start = m_pos;
        if (peek() == '-' || peek() == '+') ++m_pos;
        while (!done()) {
            const char c = m_text[m_pos];
            const bool numeric = (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E'
                              || c == '+' || c == '-';
            if (!numeric) break;
            ++m_pos;
        }
        if (start == m_pos) { fail("expected a value"); return {}; }

        f64 out = 0.0;
        const char* first = m_text.data() + start;
        const char* last  = m_text.data() + m_pos;
        const auto  res   = std::from_chars(first, last, out);
        if (res.ec != std::errc{} || res.ptr != last) { fail("malformed number"); return {}; }
        return Value(out);
    }

    std::string_view m_text;
    std::string&     m_error;
    usize            m_pos = 0;
};

}

Value parse(std::string_view text, std::string* error) {
    std::string local;
    Parser parser(text, error != nullptr ? *error : local);
    Value  value = parser.run();
    return value;
}

// ----------------------------------------------------------------------- write

namespace {

void writeString(const std::string& in, std::string& out) {
    out.push_back('"');
    for (const char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out.push_back(c);   // already UTF-8; pass it through
                }
        }
    }
    out.push_back('"');
}

void writeNumber(f64 v, std::string& out) {
    if (!std::isfinite(v)) { out += "0"; return; }   // JSON has no NaN or infinity

    // Integers print without a decimal point, which keeps entity ids and tile ids
    // readable in a diff. %.9g round-trips an f32 exactly.
    char buf[32];
    if (v == std::floor(v) && std::fabs(v) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", v);
    else                                           std::snprintf(buf, sizeof buf, "%.9g", v);
    out += buf;
}

void writeValue(const Value& value, std::string& out, bool pretty, int depth) {
    const auto newline = [&](int d) {
        if (!pretty) return;
        out.push_back('\n');
        out.append(static_cast<usize>(d) * 2, ' ');
    };

    switch (value.type()) {
        case Value::Type::Null:   out += "null"; break;
        case Value::Type::Bool:   out += value.asBool() ? "true" : "false"; break;
        case Value::Type::Number: writeNumber(value.asNumber(), out); break;
        case Value::Type::String: writeString(value.asString(), out); break;

        case Value::Type::Array: {
            if (value.items().empty()) { out += "[]"; break; }

            // A run of plain numbers is a vector, a colour, a UV rect or a row of
            // tile ids. Breaking those across lines makes a scene file both huge and
            // harder to read, so they stay inline; anything structured still nests.
            const bool inlineRun = [&] {
                for (const Value& item : value.items())
                    if (!item.isNumber()) return false;
                return true;
            }();

            out.push_back('[');
            bool first = true;
            for (const Value& item : value.items()) {
                if (!first) { out.push_back(','); if (pretty && inlineRun) out.push_back(' '); }
                first = false;
                if (!inlineRun) newline(depth + 1);
                writeValue(item, out, pretty, depth + 1);
            }
            if (!inlineRun) newline(depth);
            out.push_back(']');
            break;
        }

        case Value::Type::Object: {
            if (value.fields().empty()) { out += "{}"; break; }
            out.push_back('{');
            bool first = true;
            for (const auto& [key, field] : value.fields()) {
                if (!first) out.push_back(',');
                first = false;
                newline(depth + 1);
                writeString(key, out);
                out.push_back(':');
                if (pretty) out.push_back(' ');
                writeValue(field, out, pretty, depth + 1);
            }
            newline(depth);
            out.push_back('}');
            break;
        }
    }
}

}

std::string write(const Value& value, bool pretty) {
    std::string out;
    writeValue(value, out, pretty, 0);
    if (pretty) out.push_back('\n');
    return out;
}

}
