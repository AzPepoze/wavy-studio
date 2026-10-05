#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "AudioEngine.hpp"
#include <doctest/doctest.h>
#include <iostream>

TEST_CASE("Engine lifecycle without an audio device") {
    std::cout << "RtAudio compiled APIs:";
    for (const auto& api : wavy::AudioEngine::availableApis())
        std::cout << " " << api;
    std::cout << "\n";
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
