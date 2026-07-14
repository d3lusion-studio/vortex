#include "vortex/core/settings.hpp"

#include "vortex/core/log.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace vortex {

namespace {

// "audio.master" -> {"audio", "master"}
std::vector<std::string> split(std::string_view key) {
    std::vector<std::string> parts;
    usize start = 0;
    while (start <= key.size()) {
        const usize dot = key.find('.', start);
        const usize end = (dot == std::string_view::npos) ? key.size() : dot;
        if (end > start) parts.emplace_back(key.substr(start, end - start));
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    return parts;
}

} // namespace

const json::Value* Settings::find(std::string_view key) const {
    const std::vector<std::string> parts = split(key);
    if (parts.empty()) return nullptr;

    const json::Value* node = &m_root;
    for (const std::string& part : parts) {
        if (!node->isObject() || !node->contains(part)) return nullptr;
        node = &(*node)[part];
    }
    return node;
}

json::Value& Settings::ensure(std::string_view key) {
    const std::vector<std::string> parts = split(key);
    if (parts.empty()) return m_root;

    // Walk the path, creating each level. A level that exists but is not an object — someone set
    // "audio" to a number and then asked for "audio.master" — is replaced by json::Value::at,
    // which is the honest outcome: the two settings contradict each other, and quietly dropping
    // the write would give a set() that appears to work and a get() that never sees it.
    json::Value* node = &m_root;
    for (usize i = 0; i + 1 < parts.size(); ++i) {
        json::Value& child = node->at(parts[i]);
        if (!child.isObject()) child = json::Value::object();
        node = &child;
    }
    return node->at(parts.back());
}

bool Settings::load(const char* path) {
    m_root   = json::Value::object();
    m_loaded = false;

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;   // first run: not an error, just nothing to read

    const auto size = in.tellg();
    in.seekg(0);
    std::string text(static_cast<usize>(size), '\0');
    in.read(text.data(), size);

    std::string      error;
    const json::Value parsed = json::parse(text, &error);
    if (!parsed.isObject()) {
        // A corrupt file must not take the game down with it. Say so, and fall back to defaults —
        // the next save() rewrites it correctly.
        VORTEX_WARN("Settings", "%s is not valid settings JSON (%s); using defaults",
                    path, error.c_str());
        return false;
    }

    m_root   = parsed;
    m_loaded = true;
    return true;
}

bool Settings::save(const char* path) const {
    // Create the directory: on a fresh machine ~/.config/<app>/ does not exist yet, and a save
    // that fails on first run is a settings system that never works for anyone.
    const std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(fsPath.parent_path(), ec);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        VORTEX_WARN("Settings", "cannot write %s", path);
        return false;
    }

    const std::string text = json::write(m_root, /*pretty=*/true);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

bool Settings::getBool(std::string_view key, bool fallback) const {
    const json::Value* v = find(key);
    return (v != nullptr && v->isBool()) ? v->asBool(fallback) : fallback;
}

i32 Settings::getInt(std::string_view key, i32 fallback) const {
    const json::Value* v = find(key);
    return (v != nullptr && v->isNumber()) ? v->asI32(fallback) : fallback;
}

f32 Settings::getFloat(std::string_view key, f32 fallback) const {
    const json::Value* v = find(key);
    return (v != nullptr && v->isNumber()) ? v->asF32(fallback) : fallback;
}

std::string Settings::getString(std::string_view key, std::string_view fallback) const {
    const json::Value* v = find(key);
    return (v != nullptr && v->isString()) ? v->asString(fallback) : std::string(fallback);
}

void Settings::set(std::string_view key, bool value)  { ensure(key) = json::Value(value); }
void Settings::set(std::string_view key, i32 value)   { ensure(key) = json::Value(value); }
void Settings::set(std::string_view key, f32 value)   { ensure(key) = json::Value(value); }
void Settings::set(std::string_view key, std::string_view value) {
    ensure(key) = json::Value(std::string(value));
}
void Settings::set(std::string_view key, const char* value) {
    set(key, std::string_view(value != nullptr ? value : ""));
}

bool Settings::has(std::string_view key) const { return find(key) != nullptr; }

void Settings::remove(std::string_view key) {
    // Setting to null is the closest the DOM allows; the getters treat a null as "not there"
    // because they check the type, so it reads as removed.
    if (has(key)) ensure(key) = json::Value{};
}

std::string Settings::defaultPath(const char* appName) {
    const char* app = (appName != nullptr && *appName != '\0') ? appName : "vortex";

#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"))
        return std::string(appdata) + "\\" + app + "\\settings.json";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"))
        if (*xdg != '\0') return std::string(xdg) + "/" + app + "/settings.json";
    if (const char* home = std::getenv("HOME"))
        return std::string(home) + "/.config/" + app + "/settings.json";
#endif
    return {};
}

}
