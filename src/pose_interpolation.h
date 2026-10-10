#pragma once

#include "hands_interpolation.h"
#include "presentation_clock.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace atdd::pose {

constexpr std::size_t kMaximumBones = 128;

struct Pose {
    std::array<hands::Matrix, kMaximumBones> bones{};
    std::size_t boneCount = 0;
};

enum class PublishKind : std::uint32_t {
    appended,
    unchanged,
    invalid,
    resetGeneration,
    resetNonMonotonic,
    resetSameTickMutation,
    resetBoneCount,
    resetDiscontinuity,
};

enum class SampleKind : std::uint32_t {
    applied,
    clockUnavailable,
    generationMismatch,
    snapshotsMissing,
    invalid,
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
    [[nodiscard]] PublishKind Publish(std::uint64_t serial,
                                      std::uint64_t clockGeneration,
                                      const Pose& pose) noexcept;
    [[nodiscard]] SampleResult Evaluate(const clock::Sample& clockSample) const noexcept;

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

[[nodiscard]] bool IsValid(const Pose& pose) noexcept;
[[nodiscard]] bool IsValidMatrix(const hands::Matrix& matrix) noexcept;
[[nodiscard]] bool NearlyEqual(const Pose& left, const Pose& right) noexcept;
[[nodiscard]] bool Interpolate(const Pose& older,
                               const Pose& newer,
                               double alpha,
                               Pose& result) noexcept;
[[nodiscard]] const wchar_t* PublishKindName(PublishKind kind) noexcept;
[[nodiscard]] const wchar_t* SampleKindName(SampleKind kind) noexcept;

} // namespace atdd::pose
