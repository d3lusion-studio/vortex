#pragma once
#include "vortex/core/json.hpp"
#include "vortex/core/types.hpp"

#include <string>
#include <string_view>

namespace vortex {

// Named values that survive the process: resolution, volume, keybinds, "did we already show the
// tutorial".
//
// Every getter takes a fallback, and that is not a convenience — it is the whole design. A
// settings file is written by a previous version of the game, edited by hand, or missing
// entirely, and code that reads it must keep working in all three cases. So there is no "does
// this key exist" dance at the call site: you ask for a value and say what it is if the file has
// nothing useful to say.
//
// A key is a dotted path — "audio.master", "window.width" — which nests in the JSON so a person
// can actually read and edit the file. That is the point of a text format; if nobody is ever
// going to open it, it should have been binary.
class Settings {
public:
    // Load from `path`. A missing or malformed file is NOT an error: you get an empty store, and
    // every get() returns its fallback. `loaded()` says which happened, for a game that wants to
    // tell first-run apart from a corrupted file.
    bool load(const char* path);

    // Write to `path`. Creates the file; overwrites it whole. False on a write failure.
    bool save(const char* path) const;

    [[nodiscard]] bool loaded() const { return m_loaded; }

    [[nodiscard]] bool        getBool(std::string_view key, bool fallback) const;
    [[nodiscard]] i32         getInt(std::string_view key, i32 fallback) const;
    [[nodiscard]] f32         getFloat(std::string_view key, f32 fallback) const;
    [[nodiscard]] std::string getString(std::string_view key, std::string_view fallback) const;

    void set(std::string_view key, bool value);
    void set(std::string_view key, i32 value);
    void set(std::string_view key, f32 value);
    void set(std::string_view key, std::string_view value);

    // Without this, set("stats.lastExit", "clean") stores `true`.
    //
    // const char* -> bool is a STANDARD conversion; const char* -> string_view is a user-defined
    // one, and standard conversions win overload resolution. So a string literal binds to the bool
    // overload, silently, and the value you get back is not the value you set. This overload is not
    // convenience — it is the fix.
    void set(std::string_view key, const char* value);

    [[nodiscard]] bool has(std::string_view key) const;
    void remove(std::string_view key);

    [[nodiscard]] const json::Value& root() const { return m_root; }

    // Where a game's settings belong on this machine: $XDG_CONFIG_HOME/<app>/settings.json, or
    // ~/.config/<app>/settings.json, or %APPDATA%\<app>\settings.json. Writing next to the
    // executable works right up until the game is installed somewhere the user cannot write to.
    //
    // Returns an empty string if no home directory can be found. Does NOT create the directory —
    // save() does that.
    [[nodiscard]] static std::string defaultPath(const char* appName);

private:
    [[nodiscard]] const json::Value* find(std::string_view key) const;
    json::Value&                     ensure(std::string_view key);

    json::Value m_root   = json::Value::object();
    bool        m_loaded = false;
};

}
