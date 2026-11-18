#include "Xora2D/Core/Logger.h"

#include <ctime>
#include <iostream>
#include <mutex>

namespace Xora2D {
namespace {

std::mutex& LogMutex() {
    static std::mutex mutex;
    return mutex;
}

LogLevel& MinimumLevel() {
    static LogLevel level = LogLevel::Info;
    return level;
}

Logger::Sink& ActiveSink() {
    static Logger::Sink sink;
    return sink;
}

std::string_view LevelLabel(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

} // namespace

void Logger::SetLevel(LogLevel level) {
    std::lock_guard lock(LogMutex());
    MinimumLevel() = level;
}

LogLevel Logger::GetLevel() {
    std::lock_guard lock(LogMutex());
    return MinimumLevel();
}

bool Logger::IsEnabled(LogLevel level) {
    std::lock_guard lock(LogMutex());
    return static_cast<int>(level) >= static_cast<int>(MinimumLevel());
}

std::string Logger::FormatLine(LogLevel level, std::string_view message,
                               std::chrono::system_clock::time_point time) {
    const std::time_t timeSeconds = std::chrono::system_clock::to_time_t(time);
    std::tm localTime{};
    localtime_r(&timeSeconds, &localTime);

    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()) % 1000;
    const int millisecondCount = static_cast<int>(milliseconds.count() < 0 ? 0 : milliseconds.count());

    return std::format("[{:02}:{:02}:{:02}.{:03}] [{}] {}", localTime.tm_hour, localTime.tm_min,
                        localTime.tm_sec, millisecondCount, LevelLabel(level), message);
}

void Logger::SetSink(Sink sink) {
    std::lock_guard lock(LogMutex());
    ActiveSink() = std::move(sink);
}

void Logger::ClearSink() {
    std::lock_guard lock(LogMutex());
    ActiveSink() = nullptr;
}

void Logger::Log(LogLevel level, std::string_view message) {
    if (!IsEnabled(level)) {
        return;
    }

    const std::string line = FormatLine(level, message, std::chrono::system_clock::now());
    std::lock_guard lock(LogMutex());
    if (ActiveSink()) {
        ActiveSink()(level, message);
    }
    std::cerr << line << '\n';
}

} // namespace Xora2D
