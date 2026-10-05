#include "Log.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace wavy::log {
namespace {
Level initialLevel() {
    if (const auto* value = std::getenv("WAVY_LOG"))
        if (auto level = parseLevel(value))
            return *level;
#ifdef NDEBUG
    return Level::Info;
#else
    return Level::Debug;
#endif
}
std::atomic<Level> minimum{initialLevel()};
std::mutex outputMutex;
Sink outputSink = nullptr;
bool detectColor() {
    bool forced = false;
    if (const auto* value = std::getenv("WAVY_LOG_COLOR")) {
        if (std::string_view(value) == "never")
            return false;
        forced = std::string_view(value) == "always";
    }
    if (!forced && std::getenv("NO_COLOR"))
        return false;
#ifdef _WIN32
    const auto handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode;
    const bool terminal = GetConsoleMode(handle, &mode) &&
                          SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
    const bool terminal = isatty(STDERR_FILENO);
#endif
    return forced || terminal;
}
bool useColor() {
    static const bool color = detectColor();
    return color;
}
std::string singleLine(std::string_view text) {
    std::string result(text);
    std::replace(result.begin(), result.end(), '\n', ' ');
    std::replace(result.begin(), result.end(), '\r', ' ');
    return result;
}
} // namespace
std::optional<Level> parseLevel(std::string_view text) {
    constexpr std::string_view names[] = {"trace", "debug", "info", "warn", "error"};
    for (int i = 0; i < 5; ++i)
        if (text == names[i])
            return static_cast<Level>(i);
    return std::nullopt;
}
void setLevel(Level level) { minimum.store(level, std::memory_order_relaxed); }
bool enabled(Level level) { return level >= minimum.load(std::memory_order_relaxed); }
void setSink(Sink sink) {
    std::lock_guard lock(outputMutex);
    outputSink = sink;
}
Rgb categoryColor(std::string_view category) {
    std::uint32_t hash = 2166136261u;
    for (unsigned char byte : category) {
        hash ^= byte;
        hash *= 16777619u;
    }
    const double hue = (hash % 360u) / 60.0;
    constexpr double saturation = 0.65;
    constexpr double lightness = 0.75;
    constexpr double chroma = 2.0 * (1.0 - lightness) * saturation;
    const double x = chroma * (1.0 - std::abs(std::fmod(hue, 2.0) - 1.0));
    const double m = lightness - chroma / 2.0;
    const double channels[][3] = {{chroma, x, 0}, {x, chroma, 0}, {0, chroma, x},
                                  {0, x, chroma}, {x, 0, chroma}, {chroma, 0, x}};
    const auto& rgb = channels[static_cast<int>(hue)];
    return {static_cast<int>(std::lround((rgb[0] + m) * 255)),
            static_cast<int>(std::lround((rgb[1] + m) * 255)),
            static_cast<int>(std::lround((rgb[2] + m) * 255))};
}
void write(Level level, std::string_view category, std::string_view message) {
    if (!enabled(level))
        return;
    constexpr std::string_view names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
    constexpr Rgb colors[] = {
        {130, 130, 130}, {130, 130, 130}, {120, 210, 150}, {245, 205, 80}, {245, 90, 90}};
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::floor<std::chrono::seconds>(now);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds);
    const auto time = std::chrono::system_clock::to_time_t(seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    auto line = std::format("{:02}:{:02}:{:02}.{:03} ", local.tm_hour, local.tm_min, local.tm_sec,
                            milliseconds.count());
    const bool color = useColor();
    auto ansi = [](Rgb rgb) { return std::format("\x1b[38;2;{};{};{}m", rgb.r, rgb.g, rgb.b); };
    if (color)
        line += ansi(colors[static_cast<int>(level)]);
    line += names[static_cast<int>(level)];
    if (color)
        line += "\x1b[0m";
    line += " [";
    if (color)
        line += ansi(categoryColor(category));
    line += singleLine(category);
    if (color)
        line += "\x1b[0m";
    line += "] " + singleLine(message) + "\n";
    std::lock_guard lock(outputMutex);
    if (outputSink)
        outputSink(line);
    else {
        std::fwrite(line.data(), 1, line.size(), stderr);
        std::fflush(stderr);
    }
}
} // namespace wavy::log
