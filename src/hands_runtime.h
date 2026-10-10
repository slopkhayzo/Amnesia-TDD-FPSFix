#pragma once

#include "hands_interpolation.h"
#include "presentation_clock.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace atdd::hands {

struct Functions {
    void* getWorldMatrix = nullptr;
    void* getEntityParent = nullptr;
};

struct AttachmentBatch {
    std::uint32_t candidates = 0;
    std::uint32_t applied = 0;
    std::uint32_t parentMismatch = 0;
    std::uint32_t invalid = 0;
    std::uint32_t capacityExhausted = 0;
};

enum class FrameResult : std::uint32_t {
    staged,
    captured,
    renderReady,
    unavailable,
    identityChanged,
    invalid,
    clockGenerationReset,
    nonMonotonicReset,
    sameTickMutation,
    discontinuityReset,
    clockUnavailable,
    snapshotsMissing,
    applyFailed,
};

enum class OverrideResult : std::uint32_t {
    notHands,
    applied,
    appliedHandObject,
    appliedHandObjectHistory,
    invalid,
    slotsExhausted,
};

class Runtime {
public:
    static constexpr std::size_t kMaximumModelMatrices = 128;
    static constexpr std::size_t kMaximumAttachments = 32;

    void Configure(const Functions& functions) noexcept;
    [[nodiscard]] FrameResult Stage(void* playerHands) noexcept;
    [[nodiscard]] FrameResult CommitCompletedTick(std::uint64_t tickSerial,
                                                   std::uint64_t clockGeneration) noexcept;
    [[nodiscard]] bool SetPresentationCorrection(const Matrix* correction) noexcept;
    [[nodiscard]] FrameResult BeginRender(const clock::Sample& clockSample) noexcept;
    [[nodiscard]] OverrideResult OverrideModelMatrix(void* subMesh,
                                                     const Matrix* original,
                                                     const Matrix*& result) noexcept;
    [[nodiscard]] const AttachmentBatch& LastAttachmentBatch() const noexcept;
    [[nodiscard]] std::uint32_t EndRender() noexcept;

private:
    struct Identity {
        void* helper = nullptr;
        void* entity = nullptr;
    };

    struct Staged {
        Identity identity{};
        void* handObject = nullptr;
        void* handObjectEntity = nullptr;
        Matrix world{};
        bool valid = false;
    };

    struct ModelSlot {
        void* subMesh = nullptr;
        Matrix matrix{};
    };

    struct AttachmentSlot {
        void* entity = nullptr;
        Matrix* world = nullptr;
        Matrix restore{};
    };

    struct HandObjectHistorySlot {
        void* subMesh = nullptr;
        History history{};
        std::uint64_t lastCapturedSerial = 0;
        bool occupied = false;
    };

    [[nodiscard]] bool ReadWorld(void* entity, Matrix& matrix) const noexcept;
    [[nodiscard]] bool FindActiveHandObject(void* playerHands, void*& handObject,
                                            void*& entity) const noexcept;
    void PrepareAttachments() noexcept;
    void PrepareAttachmentVector(std::size_t beginOffset,
                                 std::size_t endOffset) noexcept;
    void ResetHandObjectHistories() noexcept;
    [[nodiscard]] HandObjectHistorySlot* FindOrCreateHandObjectHistory(
        void* subMesh) noexcept;

    Functions functions_{};
    History history_{};
    Staged staged_{};
    Identity identity_{};
    void* handObject_ = nullptr;
    void* handObjectEntity_ = nullptr;
    Matrix rootDelta_{};
    Matrix presentationCorrection_{};
    std::array<ModelSlot, kMaximumModelMatrices> modelSlots_{};
    std::array<HandObjectHistorySlot, kMaximumModelMatrices>
        handObjectHistories_{};
    std::array<AttachmentSlot, kMaximumAttachments> attachmentSlots_{};
    AttachmentBatch attachmentBatch_{};
    std::size_t modelSlotCount_ = 0;
    std::size_t attachmentSlotCount_ = 0;
    std::uint64_t currentSerial_ = 0;
    std::uint64_t currentGeneration_ = 0;
    clock::Sample renderClockSample_{};
    bool hasIdentity_ = false;
    bool hasPresentationCorrection_ = false;
    bool renderActive_ = false;
};

[[nodiscard]] const wchar_t* FrameResultName(FrameResult result) noexcept;
[[nodiscard]] const wchar_t* OverrideResultName(OverrideResult result) noexcept;

} // namespace atdd::hands
