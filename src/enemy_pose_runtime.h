#pragma once

#include "pose_interpolation.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace atdd::enemy_pose {

struct Functions {
    void* updateGraphicsForFrame = nullptr;
};

enum class StageResult : std::uint32_t {
    stagedEnemy,
    stagedRagdoll,
    ineligible,
    invalid,
    capacityExhausted,
};

struct BatchResult {
    std::uint32_t targets = 0;
    std::uint32_t visibilitySkipped = 0;
    std::uint32_t visibilityUnavailable = 0;
    std::uint32_t captured = 0;
    std::uint32_t applied = 0;
    std::uint32_t identityChanged = 0;
    std::uint32_t generationReset = 0;
    std::uint32_t nonMonotonicReset = 0;
    std::uint32_t sameTickMutation = 0;
    std::uint32_t boneCountReset = 0;
    std::uint32_t discontinuityReset = 0;
    std::uint32_t clockWait = 0;
    std::uint32_t pairMissing = 0;
    std::uint32_t invalid = 0;
    std::uint32_t unavailable = 0;
    std::uint32_t restoreFailed = 0;
};

class Runtime {
public:
    static constexpr std::size_t kMaximumTargets = 8;

    void Configure(const Functions& functions) noexcept;
    [[nodiscard]] StageResult StageEnemy(void* enemy,
                                         std::uint64_t pendingSerial) noexcept;
    [[nodiscard]] StageResult StageRagdollProp(void* prop, void* mesh,
                                               std::int32_t id,
                                               std::size_t bodyCount,
                                               bool allSleeping,
                                               std::uint64_t pendingSerial) noexcept;
    void CommitCompletedTick(std::uint64_t tickSerial,
                             std::uint64_t clockGeneration) noexcept;
    [[nodiscard]] BatchResult BeginRender(const clock::Sample& clockSample,
                                          std::uint64_t tickSerial,
                                          float frameTime) noexcept;
    [[nodiscard]] BatchResult EndRender() noexcept;
    void Reset() noexcept;

private:
    enum class Kind : std::uint8_t {
        enemyGrunt,
        ragdollProp,
    };

    struct Slot {
        void* entity = nullptr;
        void* mesh = nullptr;
        std::int32_t id = 0;
        Kind kind = Kind::enemyGrunt;
        pose::History history{};
        void* identityBuffer = nullptr;
        std::size_t identityBoneCount = 0;
        std::uint64_t stagedSerial = 0;
        std::uint64_t committedSerial = 0;
        std::uint64_t lastCapturedSerial = 0;
        std::int32_t lastRenderFrame = -1;
        std::uint8_t visibilityGrace = 0;
        hands::Matrix* overrideBuffer = nullptr;
        std::size_t overrideBoneCount = 0;
        std::array<hands::Matrix, pose::kMaximumBones> restore{};
        bool occupied = false;
        bool hasCapture = false;
        bool visibilityInitialized = false;
        bool overridden = false;
        bool sleeping = false;
    };

    [[nodiscard]] Slot* Find(void* entity) noexcept;
    [[nodiscard]] Slot* FindFree() noexcept;
    [[nodiscard]] StageResult StageTarget(void* entity, void* mesh,
                                          std::int32_t id, Kind kind,
                                          std::uint64_t pendingSerial,
                                          bool sleeping = false) noexcept;
    void Clear(Slot& slot) noexcept;

    Functions functions_{};
    std::array<Slot, kMaximumTargets> slots_{};
    std::uint64_t currentSerial_ = 0;
    std::uint64_t currentGeneration_ = 0;
    bool renderActive_ = false;
};

[[nodiscard]] const wchar_t* StageResultName(StageResult result) noexcept;

} // namespace atdd::enemy_pose
