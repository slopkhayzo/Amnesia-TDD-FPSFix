#pragma once

#include "pose_interpolation.h"

#include <array>
#include <cstdint>

namespace atdd::pose {

struct Functions {
    void* updateGraphicsForFrame = nullptr;
};

enum class StageResult : std::uint32_t {
    staged,
    unavailable,
    inactive,
};

enum class UpdateResult : std::uint32_t {
    notTarget,
    captured,
    applied,
    alreadyApplied,
    unavailable,
    identityChanged,
    clockUnavailable,
    pairMissing,
    invalid,
};

class Runtime {
public:
    void Configure(const Functions& functions) noexcept;
    [[nodiscard]] StageResult StageHands(void* playerHands,
                                         std::uint64_t pendingSerial) noexcept;
    void CommitCompletedTick(std::uint64_t tickSerial,
                             std::uint64_t clockGeneration) noexcept;
    [[nodiscard]] UpdateResult BeginRender(const clock::Sample& clockSample,
                                           std::uint64_t tickSerial,
                                           float frameTime) noexcept;
    [[nodiscard]] UpdateResult OnMeshUpdated(void* mesh) noexcept;
    [[nodiscard]] bool EndRender() noexcept;
    [[nodiscard]] bool TakeInvalidMatrixDiagnostic(std::size_t& boneIndex,
                                                   hands::Matrix& matrix) noexcept;
    void Reset() noexcept;

private:
    Functions functions_{};
    struct StagedTarget {
        void* mesh = nullptr;
        std::uint64_t pendingSerial = 0;
        bool valid = false;
    };

    History history_{};
    StagedTarget staged_{};
    clock::Sample clockSample_{};
    void* targetMesh_ = nullptr;
    void* identityBuffer_ = nullptr;
    std::size_t identityBoneCount_ = 0;
    std::uint64_t currentSerial_ = 0;
    std::uint64_t currentGeneration_ = 0;
    std::uint64_t lastCapturedSerial_ = 0;
    hands::Matrix* overrideBuffer_ = nullptr;
    std::size_t overrideBoneCount_ = 0;
    std::array<hands::Matrix, kMaximumBones> restore_{};
    hands::Matrix invalidMatrix_{};
    std::size_t invalidBoneIndex_ = 0;
    bool hasCapture_ = false;
    bool renderActive_ = false;
    bool overridden_ = false;
    bool hasInvalidDiagnostic_ = false;
};

[[nodiscard]] const wchar_t* StageResultName(StageResult result) noexcept;
[[nodiscard]] const wchar_t* UpdateResultName(UpdateResult result) noexcept;

} // namespace atdd::pose
