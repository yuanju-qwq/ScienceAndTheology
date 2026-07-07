// Lightweight logging system — implementation.
//
// Single stderr sink with ANSI color, serialized via mutex. Output format:
//   [HH:MM:SS.mmm][T#12345][LEVEL][channel] message
//
// Color codes:
//   Trace = dark gray     Debug = gray       Info  = default
//   Warn  = yellow        Error = red        Fatal = bold red

#include "core/log.h"

// Define the implementation-side default channel so this TU itself
// can log without triggering the "unknown" fallback in log.h.
#ifndef SNT_LOG_CHANNEL
#  define SNT_LOG_CHANNEL "core"
#endif

#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace snt::core {

namespace {

// Per-level short labels (fixed 5-char width for alignment).
constexpr const char* kLevelLabels[] = {
    "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL",
};

// ANSI color codes per level. Empty string means "default color".
constexpr const char* kLevelColors[] = {
    "\x1b[90m",  // Trace - dark gray
    "\x1b[37m",  // Debug - light gray
    "",          // Info  - default
    "\x1b[33m",  // Warn  - yellow
    "\x1b[31m",  // Error - red
    "\x1b[1;31m", // Fatal - bold red
};

constexpr const char* kColorReset = "\x1b[0m";

// Format a wall-clock timestamp as HH:MM:SS.mmm into `buf` (>= 13 bytes).
void format_timestamp(char* buf, size_t buf_size) {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::snprintf(buf, buf_size, "%02d:%02d:%02d.%03d",
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<int>(ms.count()));
}

}  // namespace

// ---------------------------------------------------------------------------
// Logger::Impl: holds mutable state guarded by mutex.
// ---------------------------------------------------------------------------
struct Logger::Impl {
    std::mutex mutex;
    LogLevel   min_level = LogLevel::kInfo;
};

Logger::Logger() : impl_(new Impl()) {}
Logger::~Logger() { delete impl_; }

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::set_level(LogLevel level) {
    // set_level may be called from any thread; guard the write.
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->min_level = level;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->min_level;
}

void Logger::log(LogLevel level, const char* channel, const char* fmt, ...) {
    // Fast path: filter without locking. The level check is a single int
    // read; a torn read here only risks emitting one stray message, which
    // is acceptable. The actual write below is still mutex-guarded.
    if (static_cast<int>(level) < static_cast<int>(impl_->min_level)) {
        return;
    }

    // Format the user message first (outside the lock to minimize hold time).
    char msg_buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    // Build the full log line.
    char ts_buf[16];
    format_timestamp(ts_buf, sizeof(ts_buf));

    const int level_idx = static_cast<int>(level);
    const char* color   = kLevelColors[level_idx];
    const char* label   = kLevelLabels[level_idx];
    const auto  tid     = std::hash<std::thread::id>{}(std::this_thread::get_id());

    // Acquire the lock only for the actual write (atomic w.r.t. other logs).
    std::lock_guard<std::mutex> lock(impl_->mutex);

    // Re-check level inside the lock in case set_level ran between the
    // fast-path check and now.
    if (static_cast<int>(level) < static_cast<int>(impl_->min_level)) {
        return;
    }

    std::fprintf(stderr, "%s[%s][T#%x][%s][%-15s] %s%s\n",
                 color, ts_buf,
                 static_cast<unsigned>(tid),
                 label,
                 channel ? channel : "",
                 msg_buf,
                 kColorReset);
}

}  // namespace snt::core
