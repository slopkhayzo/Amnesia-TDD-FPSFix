#pragma once

#include "hands_interpolation.h"
#include "presentation_clock.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace atdd::world {

using Matrix = hands::Matrix;

struct Functions {
    void* getWorldMatrix = nullptr;
    void* getEntityParent = nullptr;
    void* getBodyEnabled = nullptr;
};

enum class Kind : std::uint32_t {
    propObject,
    enemyGrunt,
    standalonePhysicsBody,
};

enum class StageResult : std::uint32_t {
    stagedProp,
    stagedGrunt,
    cachedSleepingProp,
    ineligible,
    invalid,
    capacityExhausted,
};

// A descriptor emitted only after StageProp has fully validated the Lux prop,
// mesh, body vector, and current Newton sleep state. Downstream presentation
// layers can consume it without repeating the expensive discovery walk.
struct PropStageInfo {
    void* mesh = nullptr;
    std::int32_t id = 0;
    std::size_t bodyCount = 0;
    bool allSleeping = false;
    bool valid = false;
};

struct BatchResult {
    std::uint32_t captured = 0;
    std::uint32_t reset = 0;
    std::uint32_t invalid = 0;
    std::uint32_t ready = 0;
    std::uint32_t pairMiss = 0;
    std::uint32_t applyFailed = 0;
    std::uint32_t sleeping = 0;
    std::uint32_t movedCallbacks = 0;
    std::uint32_t movedMatched = 0;
    std::uint32_t movedStandalone = 0;
    std::uint32_t movedOverflow = 0;
    std::uint32_t multiBodyOwners = 0;
    std::uint32_t modelCandidates = 0;
    std::uint32_t modelCaptured = 0;
    std::uint32_t modelInvalid = 0;
    std::uint32_t modelCapacity = 0;
    std::uint32_t modelReady = 0;
    std::uint32_t modelMovingReady = 0;
    std::uint32_t modelCacheApplied = 0;
    std::uint32_t modelCacheInvalid = 0;
    std::uint32_t modelRestoreFailed = 0;
};

enum class OverrideResult : std::uint32_t {
    notWorld,
    appliedProp,
    appliedPropParent,
    appliedPropNullModel,
    appliedPropParentNullModel,
    appliedPropDirectParent,
    appliedPropDirectParentNullModel,
    appliedPropModelHistory,
    appliedGrunt,
    appliedStandaloneBody,
    appliedStandaloneBodyNullModel,
    propParentMismatch,
    invalid,
    slotsExhausted,
};

// Diagnostic outcome for the exact cSubMeshEntity history fallback. This is
// deliberately separate from OverrideResult: a history can be found and then
// lose to the more precise body-parent route.
enum class ModelHistoryGate : std::uint32_t {
    notFound,
    bodyPriority,
    ownerReadInvalid,
    ownerMissing,
    ownerNotReady,
    staleSerial,
    clockUnavailable,
    generationMismatch,
    snapshotsMissing,
    sampleInvalid,
    slotsExhausted,
    applied,
    appliedMovingUnchanged,
    appliedMovingChanged,
};

class Runtime {
  public:
    static constexpr std::size_t kMaximumEntities = 512;
    static constexpr std::size_t kSlotBucketCount = 1024;
    static constexpr std::size_t kMaximumModelMatrices = 512;
    static constexpr std::size_t kMaximumRenderOwners = kMaximumEntities;
    static constexpr std::size_t kRenderOwnerBucketCount = 1024;
    static constexpr std::size_t kMaximumBodiesPerProp = 32;
    static constexpr std::size_t kMaximumSubMeshesPerProp = 128;
    static constexpr std::size_t kMaximumParentDepth = 16;
    static constexpr std::size_t kMaximumMovedBodiesPerTick = 1024;
    static constexpr std::uint64_t kSleepingRevalidationTicks = 60;
    static constexpr float kMaximumPositionDeltaPerTick = 0.75F;
    static constexpr float kMaximumOrientationDeltaPerTick = 0.78539816339F;

    void Configure(const Functions& functions) noexcept;
    [[nodiscard]] StageResult StageProp(void* prop, std::uint64_t pendingSerial,
                                        PropStageInfo* stageInfo = nullptr) noexcept;
    [[nodiscard]] StageResult StageEnemy(void* enemy, std::uint64_t pendingSerial) noexcept;
    void NotifyBodyMoved(void* body) noexcept;
    [[nodiscard]] BatchResult CommitCompletedTick(std::uint64_t tickSerial,
                                                  std::uint64_t clockGeneration) noexcept;
    [[nodiscard]] BatchResult BeginRender(const clock::Sample& clockSample) noexcept;
    [[nodiscard]] OverrideResult
    OverrideModelMatrix(void* subMesh, const Matrix* original, const Matrix*& result,
                        ModelHistoryGate* historyGate = nullptr) noexcept;
    [[nodiscard]] std::uint32_t EndRender() noexcept;

  private:
    struct Slot {
        void* entity = nullptr;
        void* mesh = nullptr;
        void* root = nullptr;
        void* parentNode = nullptr;
        std::int32_t id = 0;
        Kind kind = Kind::propObject;
        hands::History history{};
        Matrix lastWorld{};
        Matrix rootDelta{};
        std::uint64_t stagedSerial = 0;
        std::uint64_t committedSerial = 0;
        bool occupied = false;
        bool staged = false;
        bool hasLastWorld = false;
        bool moving = false;
        bool renderReady = false;
        bool bodyEnabled = false;
        bool captureFinal = false;
        std::uint16_t bodyCount = 0;
    };

    struct ModelSlot {
        void* subMesh = nullptr;
        Matrix matrix{};
    };

    struct RenderOwner {
        void* mesh = nullptr;
        Slot* readyEnemy = nullptr;
        Slot* soleReadyPropRoot = nullptr;
        std::size_t propRootCount = 0;
        std::size_t propBodyCount = 0;
        bool hasReadyProp = false;
    };

    struct RenderRoot {
        void* root = nullptr;
        Slot* slot = nullptr;
    };

    struct ModelHistorySlot {
        struct Snapshot {
            std::uint64_t serial = 0;
            Matrix matrix{};
        };

        void* subMesh = nullptr;
        void* owner = nullptr;
        std::array<Snapshot, clock::PresentationClock::kHistoryCapacity> snapshots{};
        std::size_t snapshotCount = 0;
        std::uint64_t generation = 0;
        std::uint64_t lastSeenSerial = 0;
        Matrix restore{};
        Matrix* overrideBuffer = nullptr;
        bool hasGeneration = false;
        bool occupied = false;
        bool overridden = false;
    };

    struct PropCache {
        void* prop = nullptr;
        std::int32_t id = 0;
        std::uint64_t lastValidationSerial = 0;
        std::uint64_t nextValidationSerial = 0;
        bool occupied = false;
        bool allSleeping = false;
        bool captureModelsWhileSleeping = false;
    };

    [[nodiscard]] StageResult StageEnemyRoot(void* entity, std::uint64_t pendingSerial) noexcept;
    [[nodiscard]] Slot* StageStandaloneBody(void* body,
                                            std::uint64_t pendingSerial) noexcept;
    [[nodiscard]] bool StageRoot(void* entity, void* mesh, void* root, void* parentNode,
                                 std::int32_t id, Kind kind, bool bodyEnabled,
                                 std::size_t bodyCount, std::uint64_t pendingSerial) noexcept;
    void Untrack(void* entity) noexcept;
    [[nodiscard]] PropCache* FindPropCache(void* prop) noexcept;
    [[nodiscard]] PropCache* FindOrCreatePropCache(void* prop) noexcept;
    void RemovePropCache(void* prop) noexcept;
    void RebuildPropCacheIndex() noexcept;
    [[nodiscard]] Slot* FindRoot(void* root) noexcept;
    [[nodiscard]] Slot* FindSlot(void* entity, void* root) noexcept;
    [[nodiscard]] bool InsertSlot(Slot& slot) noexcept;
    void RebuildSlotIndex() noexcept;
    [[nodiscard]] Slot* FindOrCreate(void* entity, void* root,
                                     std::uint64_t pendingSerial) noexcept;
    [[nodiscard]] RenderOwner* FindOrCreateRenderOwner(void* mesh) noexcept;
    [[nodiscard]] RenderOwner* FindRenderOwner(void* mesh) noexcept;
    [[nodiscard]] bool InsertRenderRoot(Slot& slot) noexcept;
    [[nodiscard]] Slot* FindRenderRoot(void* root) noexcept;
    [[nodiscard]] Slot* ResolveModelBodySlot(void* subMesh, void* owner, bool& matchedDirectParent,
                                             bool& matchedPropParent,
                                             bool& propParentMiss) noexcept;
    [[nodiscard]] ModelHistorySlot* FindModelHistory(void* subMesh) noexcept;
    [[nodiscard]] ModelHistorySlot* FindOrCreateModelHistory(void* subMesh, void* owner) noexcept;
    [[nodiscard]] bool InsertModelHistory(ModelHistorySlot& slot) noexcept;
    void RebuildModelHistoryIndex() noexcept;
    [[nodiscard]] hands::PublishKind PublishModelHistory(ModelHistorySlot& slot,
                                                         std::uint64_t serial,
                                                         std::uint64_t clockGeneration,
                                                         const Matrix& matrix) noexcept;
    [[nodiscard]] hands::SampleResult
    EvaluateModelHistory(const ModelHistorySlot& slot,
                         const clock::Sample& clockSample) const noexcept;
    [[nodiscard]] bool ModelHistoryPairMoved(const ModelHistorySlot& slot,
                                             const clock::Sample& clockSample) const noexcept;
    [[nodiscard]] bool ReadWorld(void* mesh, Matrix& matrix) const noexcept;

    Functions functions_{};
    std::array<Slot, kMaximumEntities> slots_{};
    std::array<std::size_t, kSlotBucketCount> slotBuckets_{};
    std::array<ModelSlot, kMaximumModelMatrices> modelSlots_{};
    std::array<RenderOwner, kMaximumRenderOwners> renderOwners_{};
    std::array<std::size_t, kRenderOwnerBucketCount> renderOwnerBuckets_{};
    std::array<RenderRoot, kMaximumRenderOwners> renderRoots_{};
    std::array<std::size_t, kRenderOwnerBucketCount> renderRootBuckets_{};
    std::size_t renderRootCount_ = 0;
    std::array<ModelHistorySlot, kMaximumModelMatrices> modelHistories_{};
    std::array<std::size_t, kRenderOwnerBucketCount> modelHistoryBuckets_{};
    std::array<PropCache, kMaximumEntities> propCaches_{};
    std::array<std::size_t, kSlotBucketCount> propCacheBuckets_{};
    std::array<std::atomic<void*>, kMaximumMovedBodiesPerTick> movedBodies_{};
    std::atomic<std::uint32_t> movedBodyCount_{0};
    std::atomic<std::uint32_t> movedBodyOverflow_{0};
    std::size_t modelSlotCount_ = 0;
    std::size_t renderOwnerCount_ = 0;
    clock::Sample renderClockSample_{};
    bool renderActive_ = false;
};

[[nodiscard]] const wchar_t* StageResultName(StageResult result) noexcept;
[[nodiscard]] const wchar_t* OverrideResultName(OverrideResult result) noexcept;

} // namespace atdd::world
