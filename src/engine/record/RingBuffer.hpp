#pragma once
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace wavy::record {
class RingBuffer {
  public:
    explicit RingBuffer(std::size_t capacity = 48000 * 10 * 2, unsigned channels = 2)
        : channels_(channels), data_(sizeFor(capacity, channels)) {}
    std::size_t capacity() const noexcept { return data_.size(); }
    std::uint64_t droppedFrames() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }
    std::size_t tryWrite(const float* input, std::size_t count) noexcept {
        const auto write = write_.load(std::memory_order_relaxed);
        const auto read = read_.load(std::memory_order_acquire);
        auto accepted = std::min(count, data_.size() - (write - read));
        accepted -= accepted % channels_;
        copyIn(input, write, accepted);
        // Release publishes the samples; acquire prevents reuse until the reader has finished.
        write_.store(write + accepted, std::memory_order_release);
        dropped_.fetch_add((count - accepted) / channels_, std::memory_order_relaxed);
        return accepted;
    }
    std::size_t tryRead(float* output, std::size_t count) noexcept {
        const auto read = read_.load(std::memory_order_relaxed);
        const auto write = write_.load(std::memory_order_acquire);
        auto accepted = std::min(count, write - read);
        accepted -= accepted % channels_;
        const auto index = read & (data_.size() - 1);
        const auto first = std::min(accepted, data_.size() - index);
        std::copy_n(data_.data() + index, first, output);
        std::copy_n(data_.data(), accepted - first, output + first);
        read_.store(read + accepted, std::memory_order_release);
        return accepted;
    }
    // Both endpoints must be quiescent before resetting a session.
    void reset() noexcept {
        read_.store(0, std::memory_order_relaxed);
        write_.store(0, std::memory_order_relaxed);
        dropped_.store(0, std::memory_order_relaxed);
    }

  private:
    static std::size_t sizeFor(std::size_t size, unsigned channels) {
        if ((channels != 1 && channels != 2) ||
            size > (std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 2)))
            throw std::invalid_argument("Invalid recording ring capacity/channels");
        return std::bit_ceil(std::max(size, std::size_t(channels)));
    }
    void copyIn(const float* input, std::size_t write, std::size_t count) noexcept {
        const auto index = write & (data_.size() - 1);
        const auto first = std::min(count, data_.size() - index);
        std::copy_n(input, first, data_.data() + index);
        std::copy_n(input + first, count - first, data_.data());
    }
    unsigned channels_;
    std::vector<float> data_;
    alignas(64) std::atomic<std::size_t> write_{0};
    alignas(64) std::atomic<std::size_t> read_{0};
    alignas(64) std::atomic<std::uint64_t> dropped_{0};
};
static_assert(std::atomic<std::size_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
} // namespace wavy::record
