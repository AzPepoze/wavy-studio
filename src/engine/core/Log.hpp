#pragma once
#include <format>
#include <optional>
#include <string_view>
#include <utility>

namespace wavy::log {
enum class Level { Trace, Debug, Info, Warn, Error };
struct Rgb {
    int r, g, b;
    bool operator==(const Rgb&) const = default;
};
using Sink = void (*)(std::string_view);
std::optional<Level> parseLevel(std::string_view text);
void setLevel(Level level);
bool enabled(Level level);
Rgb categoryColor(std::string_view category);
void setSink(Sink sink);
// Formatting and output may block; never call from the audio callback.
void write(Level level, std::string_view category, std::string_view message);

template <typename... Args>
void message(Level level, std::string_view category, std::format_string<Args...> format,
             Args&&... args) {
    if (enabled(level))
        write(level, category, std::format(format, std::forward<Args>(args)...));
}
template <typename... Args>
void trace(std::string_view category, std::format_string<Args...> format, Args&&... args) {
    message(Level::Trace, category, format, std::forward<Args>(args)...);
}
template <typename... Args>
void debug(std::string_view category, std::format_string<Args...> format, Args&&... args) {
    message(Level::Debug, category, format, std::forward<Args>(args)...);
}
template <typename... Args>
void info(std::string_view category, std::format_string<Args...> format, Args&&... args) {
    message(Level::Info, category, format, std::forward<Args>(args)...);
}
template <typename... Args>
void warn(std::string_view category, std::format_string<Args...> format, Args&&... args) {
    message(Level::Warn, category, format, std::forward<Args>(args)...);
}
template <typename... Args>
void error(std::string_view category, std::format_string<Args...> format, Args&&... args) {
    message(Level::Error, category, format, std::forward<Args>(args)...);
}
} // namespace wavy::log
