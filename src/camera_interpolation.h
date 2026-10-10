#pragma once

#include "presentation_clock.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace atdd::camera {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Pose {
    Vec3 position{};
    float pitch = 0.0F;
    float yaw = 0.0F;
    float roll = 0.0F;
};

enum class PublishKind : std::uint32_t {
    appended,
    unchanged,
    invalid,
    resetGeneration,
    resetNonMonotonic,
    resetSameTickMutation,
    resetDiscontinuity,
};

enum class SampleKind : std::uint32_t {
    applied,
    clockUnavailable,
    generationMismatch,
    snapshotsMissing,
    invalid,
};

struct PublishResult {
    PublishKind kind = PublishKind::invalid;
};

struct SampleResult {
    SampleKind kind = SampleKind::clockUnavailable;
    Pose pose{};
};

class History {
public:
    static constexpr std::size_t kCapacity = clock::PresentationClock::kHistoryCapacity;
    static constexpr float kMaximumPositionDeltaPerTick = 5.0F;
    static constexpr float kMaximumOrientationDeltaPerTick = 2.09439510239F;

    void Reset() noexcept;
    [[nodiscard]] PublishResult Publish(std::uint64_t serial,
                                        std::uint64_t clockGeneration,
                                        const Pose& pose) noexcept;
    [[nodiscard]] SampleResult Evaluate(const clock::Sample& clockSample) const noexcept;
    [[nodiscard]] std::size_t SnapshotCount() const noexcept { return count_; }

private:
    struct Snapshot {
        std::uint64_t serial = 0;
        Pose pose{};
    };

    void Append(std::uint64_t serial, const Pose& pose) noexcept;
    [[nodiscard]] const Snapshot& At(std::size_t logicalIndex) const noexcept;

    std::array<Snapshot, kCapacity> snapshots_{};
    std::size_t first_ = 0;
    std::size_t count_ = 0;
    std::uint64_t generation_ = 0;
    bool hasGeneration_ = false;
};

[[nodiscard]] bool IsFinite(const Pose& pose) noexcept;
[[nodiscard]] bool NearlyEqual(const Pose& left,
                               const Pose& right,
                               float epsilon = 1.0e-5F) noexcept;
[[nodiscard]] bool Interpolate(const Pose& older,
                               const Pose& newer,
                               double alpha,
                               Pose& result) noexcept;
[[nodiscard]] float OrientationDistance(const Pose& left, const Pose& right) noexcept;
[[nodiscard]] const wchar_t* PublishKindName(PublishKind kind) noexcept;
[[nodiscard]] const wchar_t* SampleKindName(SampleKind kind) noexcept;

} // namespace atdd::camera
