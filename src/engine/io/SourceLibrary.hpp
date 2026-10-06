#pragma once
#include "io/AudioFile.hpp"
#include "io/Peaks.hpp"
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace wavy {
struct Source {
    std::string path;
    std::shared_ptr<const AudioBuffer> audio;
    std::shared_ptr<const PeakPyramid> peaks;
};
class SourceLibrary {
  public:
    enum class State { Unknown, Loading, Ready, Failed };
    explicit SourceLibrary(unsigned targetSampleRate, std::size_t workerThreads = 0,
                           std::size_t memoryBudgetBytes = std::size_t{1} << 30);
    ~SourceLibrary();
    SourceLibrary(const SourceLibrary&) = delete;
    SourceLibrary& operator=(const SourceLibrary&) = delete;
    void request(const std::string& path);
    std::shared_ptr<const Source> get(const std::string& path) const;
    State state(const std::string& path) const;
    // Only meaningful in Failed state.
    LoadError error(const std::string& path) const;
    // Runs on a worker without the library lock; marshal to the caller's thread.
    // Callbacks must not destroy the library or call waitIdle().
    void setOnReady(std::function<void(const std::string& path, bool ok)> callback);
    // Cached audio capacity and conservative peak storage estimate; excludes in-flight loads.
    std::size_t memoryBytes() const;
    void waitIdle();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wavy
