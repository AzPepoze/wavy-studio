#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "audio/AudioEngine.hpp"
#include "core/Log.hpp"
#include <algorithm>
#include <cstdlib>
#include <doctest/doctest.h>
#include <string>

TEST_CASE("Engine lifecycle without an audio device") {
    wavy::AudioEngine engine(wavy::AudioEngine::DeviceMode::NoDevice);
    CHECK_FALSE(engine.isRunning());
    CHECK(engine.sampleRate() == 0);
    engine.stop();
    REQUIRE(engine.start());
    CHECK(engine.isRunning());
    CHECK(engine.sampleRate() == 48000);
    CHECK(engine.start());
    engine.stop();
    engine.stop();
    CHECK_FALSE(engine.isRunning());
    CHECK(engine.sampleRate() == 0);
    CHECK(engine.start());
}

namespace {
std::string captured;
void capture(std::string_view line) { captured.append(line); }
} // namespace
TEST_CASE("Logger category colors are stable pastels") {
    const auto audio = wavy::log::categoryColor("audio");
    CHECK(audio == wavy::log::categoryColor("audio"));
    CHECK(audio != wavy::log::categoryColor("qt"));
    CHECK(audio == wavy::log::Rgb{233, 150, 204});
    for (const auto category : {"audio", "qt", "app", "rtaudio", ""}) {
        const auto color = wavy::log::categoryColor(category);
        CHECK(std::min({color.r, color.g, color.b}) >= 110);
        CHECK(std::max({color.r, color.g, color.b}) >= 230);
    }
}
TEST_CASE("Logger parses levels and captures plain single-line output") {
    using namespace wavy::log;
    CHECK(parseLevel("trace") == Level::Trace);
    CHECK(parseLevel("debug") == Level::Debug);
    CHECK(parseLevel("info") == Level::Info);
    CHECK(parseLevel("warn") == Level::Warn);
    CHECK(parseLevel("error") == Level::Error);
    CHECK_FALSE(parseLevel("invalid").has_value());
#ifdef _WIN32
    _putenv_s("WAVY_LOG_COLOR", "never");
#else
    setenv("WAVY_LOG_COLOR", "never", 1);
#endif
    captured.clear();
    setSink(capture);
    setLevel(Level::Info);
    debug("audio", "hidden");
    info("audio", "started at {} Hz", 48000);
    write(Level::Warn, "qt", "first\nsecond");
    setSink(nullptr);
    CHECK(captured.find("\x1b") == std::string::npos);
    CHECK(captured.find("hidden") == std::string::npos);
    CHECK(captured.find(" INFO [audio] started at 48000 Hz\n") != std::string::npos);
    CHECK(captured.find(" WARN [qt] first second\n") != std::string::npos);
    CHECK(std::count(captured.begin(), captured.end(), '\n') == 2);
}
