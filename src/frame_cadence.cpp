#include "frame_cadence.h"

#include <algorithm>
#include <cmath>

namespace atdd::cadence {
namespace {

double PercentileMilliseconds(const std::array<std::uint32_t, FrameCadence::kBinCount>& bins,
                              const std::uint64_t samples,
                              const double percentile) noexcept {
    if (samples == 0) {
        return 0.0;
    }
    const std::uint64_t target =
        std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::ceil(samples * percentile)));
    std::uint64_t cumulative = 0;
    for (std::size_t index = 0; index < bins.size(); ++index) {
        cumulative += bins[index];
        if (cumulative >= target) {
            return (static_cast<double>(index * FrameCadence::kBinWidthMicroseconds) +
                    static_cast<double>(FrameCadence::kBinWidthMicroseconds) * 0.5) /
                   1000.0;
        }
    }
    return static_cast<double>(FrameCadence::kMaximumBinnedMicroseconds) / 1000.0;
}

} // namespace

void FrameCadence::RecordQpc(const std::int64_t counter, const std::int64_t frequency) noexcept {
    if (counter <= 0 || frequency <= 0) {
        invalid_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (previousCounter_ == 0) {
        previousCounter_ = counter;
        return;
    }
    const std::int64_t difference = counter - previousCounter_;
    previousCounter_ = counter;
    if (difference <= 0) {
        invalid_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const double microseconds = static_cast<double>(difference) * 1'000'000.0 /
                                static_cast<double>(frequency);
    if (!std::isfinite(microseconds) || microseconds <= 0.0) {
        invalid_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    RecordIntervalMicroseconds(static_cast<std::uint64_t>(microseconds + 0.5));
}

void FrameCadence::RecordIntervalMicroseconds(const std::uint64_t microseconds) noexcept {
    if (microseconds == 0) {
        invalid_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (microseconds > kLongGapMicroseconds) {
        longGaps_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const bool exceeded = microseconds > kMaximumBinnedMicroseconds;
    if (exceeded) {
        overflow_.fetch_add(1, std::memory_order_relaxed);
    }
    const std::size_t bin = static_cast<std::size_t>(
        std::min(microseconds, kMaximumBinnedMicroseconds) / kBinWidthMicroseconds);
    bins_[bin].fetch_add(1, std::memory_order_relaxed);
}

Snapshot FrameCadence::TakeSnapshot() noexcept {
    Snapshot snapshot{};
    std::array<std::uint32_t, kBinCount> captured{};
    std::uint64_t weightedMicroseconds = 0;
    std::size_t maximumBin = 0;
    for (std::size_t index = 0; index < bins_.size(); ++index) {
        captured[index] = bins_[index].exchange(0, std::memory_order_acq_rel);
        snapshot.samples += captured[index];
        if (captured[index] != 0) {
            maximumBin = index;
            weightedMicroseconds +=
                static_cast<std::uint64_t>(captured[index]) *
                (index * kBinWidthMicroseconds + kBinWidthMicroseconds / 2);
        }
    }
    snapshot.longGaps = longGaps_.exchange(0, std::memory_order_acq_rel);
    snapshot.overflow = overflow_.exchange(0, std::memory_order_acq_rel);
    snapshot.invalid = invalid_.exchange(0, std::memory_order_acq_rel);
    if (snapshot.samples != 0) {
        snapshot.meanMilliseconds =
            static_cast<double>(weightedMicroseconds) /
            static_cast<double>(snapshot.samples) / 1000.0;
        snapshot.p50Milliseconds = PercentileMilliseconds(captured, snapshot.samples, 0.50);
        snapshot.p95Milliseconds = PercentileMilliseconds(captured, snapshot.samples, 0.95);
        snapshot.p99Milliseconds = PercentileMilliseconds(captured, snapshot.samples, 0.99);
        snapshot.maxMilliseconds =
            (static_cast<double>(maximumBin * kBinWidthMicroseconds) +
             static_cast<double>(kBinWidthMicroseconds) * 0.5) /
            1000.0;
    }
    return snapshot;
}

} // namespace atdd::cadence
