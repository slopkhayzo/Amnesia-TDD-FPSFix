#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace atdd::clock {

enum class ResetReason : std::uint32_t {
    none,
    explicitDiscontinuity,
    nonMonotonicWallTime,
    nonMonotonicTick,
    missingSnapshots,
    longStall,
};

enum class SampleKind : std::uint32_t {
    unavailable,
    interpolated,
    clampedToOldest,
    clampedToNewest,
    reset,
};

struct PublishResult {
    bool accepted = false;
    ResetReason resetReason = ResetReason::none;
};

struct Sample {
    SampleKind kind = SampleKind::unavailable;
    ResetReason resetReason = ResetReason::none;
    bool phaseRebased = false;
    std::uint64_t generation = 0;
    std::uint64_t olderSerial = 0;
    std::uint64_t newerSerial = 0;
    double alpha = 0.0;
    double targetTime = 0.0;
};

class PresentationClock {
  public:
    static constexpr double kFixedStepSeconds = 1.0 / 60.0;
    static constexpr double kInterpolationDelaySeconds = kFixedStepSeconds;
    static constexpr double kLongStallSeconds = 0.250;
    static constexpr std::uint64_t kMaximumBridgedSerialDelta = 1;
    // Rendering only ever interpolates the immediately previous and current
    // completed fixed states. HPL's logic-timer residual supplies alpha.
    static constexpr std::size_t kHistoryCapacity = 2;

    [[nodiscard]] PublishResult Publish(std::uint64_t serial, double wallTime,
                                        bool discontinuity = false) noexcept;
    [[nodiscard]] Sample Evaluate(double wallTime, double interpolationAlpha,
                                  bool phaseValid = true) noexcept;
    void Reset(ResetReason reason = ResetReason::explicitDiscontinuity) noexcept;

    [[nodiscard]] std::size_t SnapshotCount() const noexcept { return count_; }
    [[nodiscard]] std::uint64_t Generation() const noexcept { return generation_; }
    [[nodiscard]] ResetReason LastResetReason() const noexcept { return lastResetReason_; }

  private:
    struct Snapshot {
        std::uint64_t serial = 0;
        double wallArrivalTime = 0.0;
    };

    void Clear(ResetReason reason) noexcept;
    void Append(std::uint64_t serial, double wallTime) noexcept;
    [[nodiscard]] const Snapshot& At(std::size_t logicalIndex) const noexcept;

    std::array<Snapshot, kHistoryCapacity> snapshots_{};
    std::size_t first_ = 0;
    std::size_t count_ = 0;
    std::uint64_t generation_ = 0;
    ResetReason lastResetReason_ = ResetReason::none;
    double lastSampleWallTime_ = 0.0;
    bool hasLastSample_ = false;
};

[[nodiscard]] const wchar_t* ResetReasonName(ResetReason reason) noexcept;
[[nodiscard]] const wchar_t* SampleKindName(SampleKind kind) noexcept;
[[nodiscard]] bool LogicTimerAlpha(double nextTickMilliseconds,
                                   double stepMilliseconds,
                                   double speedMultiplier,
                                   double nowMilliseconds,
                                   double presentationDelayTicks,
                                   double& alpha,
                                   double* rawAlpha = nullptr) noexcept;

} // namespace atdd::clock
