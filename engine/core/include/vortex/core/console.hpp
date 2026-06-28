#pragma once
#include "vortex/core/types.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vortex {

enum class CVarType { Bool, Int, Float, String };

class CVar {
public:
    [[nodiscard]] const char* name() const { return m_name; }
    [[nodiscard]] const char* help() const { return m_help; }
    [[nodiscard]] CVarType    type() const { return m_type; }

    [[nodiscard]] bool        asBool()   const { return m_number != 0.0; }
    [[nodiscard]] i32         asInt()    const { return static_cast<i32>(m_number); }
    [[nodiscard]] f32         asFloat()  const { return static_cast<f32>(m_number); }
    [[nodiscard]] const std::string& asString() const { return m_string; }

    void set(bool v)             { m_number = v ? 1.0 : 0.0; fire(); }
    void set(i32 v)              { m_number = static_cast<f64>(v); fire(); }
    void set(f32 v)              { m_number = static_cast<f64>(v); fire(); }
    void set(std::string v)      { m_string = std::move(v); fire(); }

    [[nodiscard]] bool setFromString(std::string_view text);

    void onChange(std::function<void(const CVar&)> cb) { m_onChange = std::move(cb); }

    [[nodiscard]] std::string valueString() const;

private:
    friend class Console;
    void fire() { if (m_onChange) m_onChange(*this); }

    const char*                     m_name = "";
    const char*                     m_help = "";
    CVarType                        m_type = CVarType::Float;
    f64                             m_number = 0.0;
    std::string                     m_string;
    std::function<void(const CVar&)> m_onChange;
};

class Console {
public:
    using Command = std::function<void(const std::vector<std::string>& args)>;

    [[nodiscard]] static Console& global();

    CVar* registerBool  (const char* name, bool def,               const char* help = "");
    CVar* registerInt   (const char* name, i32 def,                const char* help = "");
    CVar* registerFloat (const char* name, f32 def,                const char* help = "");
    CVar* registerString(const char* name, const std::string& def, const char* help = "");

    void  registerCommand(const char* name, Command fn, const char* help = "");

    [[nodiscard]] CVar* find(std::string_view name);

    bool execute(std::string_view line);

    struct Entry { std::string name; std::string value; std::string help; bool isCommand; };
    [[nodiscard]] std::vector<Entry> list() const;

private:
    CVar* getOrCreate(const char* name, CVarType type);

    std::unordered_map<std::string, CVar> m_cvars;
    std::unordered_map<std::string, std::pair<Command, std::string>> m_commands;
};

struct CVarBoolRef {
    CVar* var;
    CVarBoolRef(const char* name, bool def, const char* help = "")
        : var(Console::global().registerBool(name, def, help)) {}
    operator bool() const { return var->asBool(); }
};
struct CVarIntRef {
    CVar* var;
    CVarIntRef(const char* name, i32 def, const char* help = "")
        : var(Console::global().registerInt(name, def, help)) {}
    operator i32() const { return var->asInt(); }
};
struct CVarFloatRef {
    CVar* var;
    CVarFloatRef(const char* name, f32 def, const char* help = "")
        : var(Console::global().registerFloat(name, def, help)) {}
    operator f32() const { return var->asFloat(); }
};

}
