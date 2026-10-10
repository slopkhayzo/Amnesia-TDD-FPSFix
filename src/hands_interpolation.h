#pragma once

#include "presentation_clock.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace atdd::hands {

struct Matrix {
    std::array<float, 16> values{};
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

struct SampleResult {
    SampleKind kind = SampleKind::clockUnavailable;
    Matrix matrix{};
};

class History {
public:
    static constexpr std::size_t kCapacity = clock::PresentationClock::kHistoryCapacity;
    static constexpr float kMaximumPositionDeltaPerTick = 5.0F;
    static constexpr float kMaximumOrientationDeltaPerTick = 2.09439510239F;

    void Reset() noexcept;
    [[nodiscard]] PublishKind Publish(std::uint64_t serial,
                                      std::uint64_t clockGeneration,
                                      const Matrix& matrix) noexcept;
    [[nodiscard]] SampleResult Evaluate(const clock::Sample& clockSample) const noexcept;

private:
    struct Snapshot {
        std::uint64_t serial = 0;
        Matrix matrix{};
    };

    void Append(std::uint64_t serial, const Matrix& matrix) noexcept;
    [[nodiscard]] const Snapshot& At(std::size_t logicalIndex) const noexcept;

    std::array<Snapshot, kCapacity> snapshots_{};
    std::size_t first_ = 0;
    std::size_t count_ = 0;
    std::uint64_t generation_ = 0;
    bool hasGeneration_ = false;
};

[[nodiscard]] Matrix Identity() noexcept;
[[nodiscard]] bool IsRigid(const Matrix& matrix) noexcept;
[[nodiscard]] bool NearlyEqual(const Matrix& left,
                               const Matrix& right,
                               float epsilon = 1.0e-5F) noexcept;
[[nodiscard]] bool Interpolate(const Matrix& older,
                               const Matrix& newer,
                               double alpha,
                               Matrix& result) noexcept;
[[nodiscard]] bool InverseRigid(const Matrix& matrix, Matrix& result) noexcept;
[[nodiscard]] bool Multiply(const Matrix& left,
                            const Matrix& right,
                            Matrix& result) noexcept;
[[nodiscard]] float OrientationDistance(const Matrix& left,
                                        const Matrix& right) noexcept;
[[nodiscard]] const wchar_t* PublishKindName(PublishKind kind) noexcept;
[[nodiscard]] const wchar_t* SampleKindName(SampleKind kind) noexcept;

} // namespace atdd::hands
