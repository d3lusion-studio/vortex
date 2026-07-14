#pragma once

namespace vortex {

// `Off` is a level you can filter TO, not one you can log AT. It is what silences a category.
enum class LogLevel { Trace, Info, Warn, Error, Off };

#if defined(__GNUC__) || defined(__clang__)
    #define VORTEX_PRINTF_FORMAT(fmt_idx, args_idx) \
        __attribute__((format(printf, fmt_idx, args_idx)))
#else
    #define VORTEX_PRINTF_FORMAT(fmt_idx, args_idx)
#endif

void log(LogLevel level, const char* category, const char* fmt, ...)
    VORTEX_PRINTF_FORMAT(3, 4);

#define VORTEX_TRACE(cat, ...) ::vortex::log(::vortex::LogLevel::Trace, cat, __VA_ARGS__)
#define VORTEX_INFO(cat, ...)  ::vortex::log(::vortex::LogLevel::Info,  cat, __VA_ARGS__)
#define VORTEX_WARN(cat, ...)  ::vortex::log(::vortex::LogLevel::Warn,  cat, __VA_ARGS__)
#define VORTEX_ERROR(cat, ...) ::vortex::log(::vortex::LogLevel::Error, cat, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Filtering
// ---------------------------------------------------------------------------

// A message below this level is dropped before its format string is expanded — a VORTEX_TRACE in
// a hot loop should cost a compare and a branch, not a vsnprintf into a buffer nobody reads.
void     setLogLevel(LogLevel);
LogLevel logLevel();

// A per-category override, for the one subsystem you are actually debugging.
//
// Without it, turning Trace on to watch the asset loader also turns on every Trace in the
// renderer, and the line you were looking for scrolls past at sixty frames a second. Pass
// LogLevel::Off to silence one noisy category and leave everything else alone.
void setCategoryLevel(const char* category, LogLevel);
void clearCategoryLevels();

// Read the environment: VORTEX_LOG=trace|info|warn|error|off sets the global level, and
// VORTEX_LOG=info,Assets=trace,RHI=off adds per-category overrides on top of it. App calls this
// at startup; call it yourself if you are not using App.
void initLogFromEnv();

// ---------------------------------------------------------------------------
// Sinks
// ---------------------------------------------------------------------------

struct LogRecord {
    LogLevel    level;
    const char* category;
    const char* message;   // the formatted body: no prefix, no newline
    // The whole line — timestamp, level, category, message, newline. A sink writes THIS, in one
    // call. The old implementation made three separate fprintf calls per message, so two threads
    // logging at once produced lines spliced into each other; a single write cannot do that.
    const char* line;
};

using LogSink = void (*)(const LogRecord&, void* user);

// stderr is installed by default. Removing it and adding your own is how a game puts its log in a
// file, or in an in-game console. Returns an id for removeLogSink().
int  addLogSink(LogSink sink, void* user = nullptr);
void removeLogSink(int id);

// Drop the built-in stderr sink — do this first if a file sink should not also echo to stderr.
void removeDefaultLogSink();

// Append every line to `path`. False if the file cannot be opened.
bool addFileLogSink(const char* path);

}
