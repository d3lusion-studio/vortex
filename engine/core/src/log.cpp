#include "vortex/core/log.hpp"

#include "vortex/core/types.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vortex {

namespace {

const char* levelTag(LogLevel l) {
    switch (l) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Off:   return "OFF  ";
    }
    return "?????";
}

bool parseLevel(std::string_view name, LogLevel& out) {
    if (name == "trace") { out = LogLevel::Trace; return true; }
    if (name == "info")  { out = LogLevel::Info;  return true; }
    if (name == "warn")  { out = LogLevel::Warn;  return true; }
    if (name == "error") { out = LogLevel::Error; return true; }
    if (name == "off")   { out = LogLevel::Off;   return true; }
    return false;
}

struct Sink {
    int     id   = 0;
    LogSink fn   = nullptr;
    void*   user = nullptr;
};

void stderrSink(const LogRecord& r, void*) {
    // ONE call. Three of them (prefix, body, newline) is what let two threads splice their lines
    // into each other in the previous implementation.
    std::fputs(r.line, stderr);
}

void fileSink(const LogRecord& r, void* user) {
    auto* f = static_cast<std::FILE*>(user);
    std::fputs(r.line, f);
    // A log still sitting in a buffer when the process dies is not a log.
    std::fflush(f);
}

struct State {
    std::mutex                                mutex;
    LogLevel                                  level = LogLevel::Info;
    std::unordered_map<std::string, LogLevel> categories;
    std::vector<Sink>                         sinks;
    std::vector<std::FILE*>                   files;
    int                                       nextId        = 1;
    int                                       defaultSinkId = 0;

    State() {
        defaultSinkId = nextId++;
        sinks.push_back({defaultSinkId, stderrSink, nullptr});
    }
};

State& state() {
    static State s;
    return s;
}

// The level in force for a category: its own override if it has one, else the global level.
LogLevel effectiveLevel(State& s, const char* category) {
    if (!s.categories.empty()) {
        const auto it = s.categories.find(category);
        if (it != s.categories.end()) return it->second;
    }
    return s.level;
}

} // namespace

void setLogLevel(LogLevel l) {
    State& s = state();
    const std::lock_guard lock(s.mutex);
    s.level = l;
}

LogLevel logLevel() {
    State& s = state();
    const std::lock_guard lock(s.mutex);
    return s.level;
}

void setCategoryLevel(const char* category, LogLevel l) {
    if (category == nullptr) return;
    State& s = state();
    const std::lock_guard lock(s.mutex);
    s.categories[category] = l;
}

void clearCategoryLevels() {
    State& s = state();
    const std::lock_guard lock(s.mutex);
    s.categories.clear();
}

void initLogFromEnv() {
    const char* env = std::getenv("VORTEX_LOG");
    if (env == nullptr) return;

    // "info,Assets=trace,RHI=off" — a bare word sets the global level, a word=word sets one
    // category's. Order does not matter: a category override always wins over the global level.
    const std::string spec = env;
    usize start = 0;
    while (start < spec.size()) {
        const usize comma = spec.find(',', start);
        const std::string token =
            spec.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        start = (comma == std::string::npos) ? spec.size() : comma + 1;
        if (token.empty()) continue;

        const usize eq = token.find('=');
        LogLevel    level{};
        if (eq == std::string::npos) {
            if (parseLevel(token, level)) setLogLevel(level);
            else std::fprintf(stderr, "[WARN ][Log] VORTEX_LOG: unknown level '%s'\n",
                              token.c_str());
        } else {
            const std::string category = token.substr(0, eq);
            if (parseLevel(token.substr(eq + 1), level))
                setCategoryLevel(category.c_str(), level);
            else
                std::fprintf(stderr, "[WARN ][Log] VORTEX_LOG: unknown level for '%s'\n",
                             category.c_str());
        }
    }
}

int addLogSink(LogSink sink, void* user) {
    if (sink == nullptr) return 0;
    State& s = state();
    const std::lock_guard lock(s.mutex);
    const int id = s.nextId++;
    s.sinks.push_back({id, sink, user});
    return id;
}

void removeLogSink(int id) {
    State& s = state();
    const std::lock_guard lock(s.mutex);
    for (usize i = 0; i < s.sinks.size(); ++i)
        if (s.sinks[i].id == id) {
            s.sinks.erase(s.sinks.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
}

void removeDefaultLogSink() {
    State& s = state();
    int id = 0;
    {
        const std::lock_guard lock(s.mutex);
        id = s.defaultSinkId;
        s.defaultSinkId = 0;
    }
    if (id != 0) removeLogSink(id);
}

bool addFileLogSink(const char* path) {
    std::FILE* f = std::fopen(path, "a");
    if (f == nullptr) return false;

    State& s = state();
    {
        const std::lock_guard lock(s.mutex);
        s.files.push_back(f);
    }
    addLogSink(fileSink, f);
    return true;
}

void log(LogLevel level, const char* category, const char* fmt, ...) {
    if (level == LogLevel::Off) return;   // Off is a level to filter TO, not to log AT
    if (category == nullptr) category = "?";

    State& s = state();

    // Filter BEFORE expanding the format string. A trace call in a hot loop that is switched off
    // must cost a lookup and a compare — not a vsnprintf into a buffer nobody will read.
    {
        const std::lock_guard lock(s.mutex);
        if (static_cast<int>(level) < static_cast<int>(effectiveLevel(s, category))) return;
    }

    char body[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    // Wall-clock to the second: enough to line a log up against a crash dump or a screen capture,
    // and it needs no clock the engine has to own.
    char stamp[16] = "";
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm);

    char line[1200];
    std::snprintf(line, sizeof(line), "[%s][%s][%s] %s\n",
                  stamp, levelTag(level), category, body);

    const LogRecord record{level, category, body, line};

    // Copy the sink list, then call the sinks OUTSIDE the lock. A sink that logs — an in-game
    // console reporting its own error — would otherwise deadlock on a non-recursive mutex.
    std::vector<Sink> sinks;
    {
        const std::lock_guard lock(s.mutex);
        sinks = s.sinks;
    }
    for (const Sink& sink : sinks) sink.fn(record, sink.user);
}

}
