#include "vortex/core/console.hpp"

#include "vortex/core/log.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>

namespace vortex {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::vector<std::string> tokenize(std::string_view line) {
    std::vector<std::string> out;
    std::istringstream iss{std::string(line)};
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

}

bool CVar::setFromString(std::string_view text) {
    switch (m_type) {
    case CVarType::Bool: {
        const std::string t = lower(text);
        if (t == "1" || t == "true" || t == "on" || t == "yes")  { set(true);  return true; }
        if (t == "0" || t == "false"|| t == "off"|| t == "no")    { set(false); return true; }
        return false;
    }
    case CVarType::Int: {
        i32 v = 0;
        const char* begin = text.data();
        const char* end   = text.data() + text.size();
        auto [ptr, ec] = std::from_chars(begin, end, v);
        if (ec != std::errc{} || ptr != end) return false;
        set(v);
        return true;
    }
    case CVarType::Float: {
        std::string s(text);
        char* endp = nullptr;
        const f32 v = std::strtof(s.c_str(), &endp);
        if (endp != s.c_str() + s.size()) return false;
        set(v);
        return true;
    }
    case CVarType::String:
        set(std::string(text));
        return true;
    }
    return false;
}

std::string CVar::valueString() const {
    switch (m_type) {
    case CVarType::Bool:   return asBool() ? "true" : "false";
    case CVarType::Int:    return std::to_string(asInt());
    case CVarType::Float:  return std::to_string(asFloat());
    case CVarType::String: return m_string;
    }
    return {};
}

Console& Console::global() {
    static Console instance;
    return instance;
}

CVar* Console::getOrCreate(const char* name, CVarType type) {
    auto [it, inserted] = m_cvars.try_emplace(name);
    CVar& cv = it->second;
    if (inserted) {
        cv.m_name = it->first.c_str();
        cv.m_type = type;
    }
    return &cv;
}

CVar* Console::registerBool(const char* name, bool def, const char* help) {
    const bool fresh = m_cvars.find(name) == m_cvars.end();
    CVar* cv = getOrCreate(name, CVarType::Bool);
    cv->m_help = help;
    if (fresh) cv->m_number = def ? 1.0 : 0.0;
    return cv;
}

CVar* Console::registerInt(const char* name, i32 def, const char* help) {
    const bool fresh = m_cvars.find(name) == m_cvars.end();
    CVar* cv = getOrCreate(name, CVarType::Int);
    cv->m_help = help;
    if (fresh) cv->m_number = static_cast<f64>(def);
    return cv;
}

CVar* Console::registerFloat(const char* name, f32 def, const char* help) {
    const bool fresh = m_cvars.find(name) == m_cvars.end();
    CVar* cv = getOrCreate(name, CVarType::Float);
    cv->m_help = help;
    if (fresh) cv->m_number = static_cast<f64>(def);
    return cv;
}

CVar* Console::registerString(const char* name, const std::string& def, const char* help) {
    const bool fresh = m_cvars.find(name) == m_cvars.end();
    CVar* cv = getOrCreate(name, CVarType::String);
    cv->m_help = help;
    if (fresh) cv->m_string = def;
    return cv;
}

void Console::registerCommand(const char* name, Command fn, const char* help) {
    m_commands[name] = {std::move(fn), help ? help : ""};
}

CVar* Console::find(std::string_view name) {
    auto it = m_cvars.find(std::string(name));
    return it == m_cvars.end() ? nullptr : &it->second;
}

bool Console::execute(std::string_view line) {
    const std::vector<std::string> tokens = tokenize(line);
    if (tokens.empty()) return true;

    const std::string& head = tokens[0];

    if (auto cmd = m_commands.find(head); cmd != m_commands.end()) {
        std::vector<std::string> args(tokens.begin() + 1, tokens.end());
        cmd->second.first(args);
        return true;
    }

    CVar* cv = find(head);
    if (!cv) {
        VORTEX_WARN("Console", "unknown cvar/command '%s'", head.c_str());
        return false;
    }

    if (tokens.size() == 1) {
        VORTEX_INFO("Console", "%s = %s", cv->name(), cv->valueString().c_str());
        return true;
    }

    std::string value = tokens[1];
    for (usize i = 2; i < tokens.size(); ++i) value += " " + tokens[i];

    if (!cv->setFromString(value)) {
        VORTEX_WARN("Console", "invalid value '%s' for %s", value.c_str(), cv->name());
        return false;
    }
    return true;
}

std::vector<Console::Entry> Console::list() const {
    std::vector<Entry> out;
    out.reserve(m_cvars.size() + m_commands.size());
    for (const auto& [name, cv] : m_cvars)
        out.push_back({name, cv.valueString(), cv.help(), false});
    for (const auto& [name, cmd] : m_commands)
        out.push_back({name, "", cmd.second, true});
    std::sort(out.begin(), out.end(),
              [](const Entry& a, const Entry& b) { return a.name < b.name; });
    return out;
}

}
