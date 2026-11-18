#pragma once

#include <chrono>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace Xora2D {

enum class LogLevel {
    Trace = 0,
    Info,
    Warn,
    Error,
};

// Static logging sink. There is one process-wide level filter so every
// engine system can log without owning a logger instance.
class Logger {
public:
    static void SetLevel(LogLevel level);
    static LogLevel GetLevel();
    static bool IsEnabled(LogLevel level);

    static void Log(LogLevel level, std::string_view message);

    // Optional in-process subscribers (editor console). Not invoked when nullptr.
    using Sink = std::function<void(LogLevel level, std::string_view message)>;
    static void SetSink(Sink sink);
    static void ClearSink();

    // Pure formatting helper so tests can check the line without capturing stderr.
    static std::string FormatLine(LogLevel level, std::string_view message,
                                  std::chrono::system_clock::time_point time);

    template <typename... Args>
    static void Trace(std::format_string<Args...> format, Args&&... args) {
        Write(LogLevel::Trace, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Info(std::format_string<Args...> format, Args&&... args) {
        Write(LogLevel::Info, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Warn(std::format_string<Args...> format, Args&&... args) {
        Write(LogLevel::Warn, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Error(std::format_string<Args...> format, Args&&... args) {
        Write(LogLevel::Error, format, std::forward<Args>(args)...);
    }

private:
    template <typename... Args>
    static void Write(LogLevel level, std::format_string<Args...> format, Args&&... args) {
        if (!IsEnabled(level)) {
            return;
        }
        Log(level, std::format(format, std::forward<Args>(args)...));
    }
};

} // namespace Xora2D

#define XORA_LOG_TRACE(...) ::Xora2D::Logger::Trace(__VA_ARGS__)
#define XORA_LOG_INFO(...) ::Xora2D::Logger::Info(__VA_ARGS__)
#define XORA_LOG_WARN(...) ::Xora2D::Logger::Warn(__VA_ARGS__)
#define XORA_LOG_ERROR(...) ::Xora2D::Logger::Error(__VA_ARGS__)
