#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace atdd::cadence {

struct Snapshot {
    std::uint64_t samples = 0;
    double meanMilliseconds = 0.0;
    double p50Milliseconds = 0.0;
    double p95Milliseconds = 0.0;
    double p99Milliseconds = 0.0;
    double maxMilliseconds = 0.0;
    std::uint32_t longGaps = 0;
    std::uint32_t overflow = 0;
    std::uint32_t invalid = 0;
};

class FrameCadence final {
public:
    static constexpr std::uint64_t kBinWidthMicroseconds = 10;
    static constexpr std::uint64_t kMaximumBinnedMicroseconds = 100'000;
    static constexpr std::uint64_t kLongGapMicroseconds = 250'000;
    static constexpr std::size_t kBinCount =
        kMaximumBinnedMicroseconds / kBinWidthMicroseconds + 1;

    void RecordQpc(std::int64_t counter, std::int64_t frequency) noexcept;
    void RecordIntervalMicroseconds(std::uint64_t microseconds) noexcept;
    [[nodiscard]] Snapshot TakeSnapshot() noexcept;

private:
    std::array<std::atomic<std::uint32_t>, kBinCount> bins_{};
    std::atomic<std::uint32_t> longGaps_{0};
    std::atomic<std::uint32_t> overflow_{0};
    std::atomic<std::uint32_t> invalid_{0};
    std::int64_t previousCounter_ = 0;
};

} // namespace atdd::cadence
