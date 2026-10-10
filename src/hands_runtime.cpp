#include "hands_runtime.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace atdd::hands {
namespace {

constexpr std::size_t kHandsEntityOffset = 0x44;
constexpr std::size_t kHandObjectsBeginOffset = 0x60;
constexpr std::size_t kHandObjectsEndOffset = 0x64;
constexpr std::size_t kHandObjectMeshEntityOffset = 0xF4;
constexpr std::size_t kHandObjectBillboardsBeginOffset = 0xF8;
constexpr std::size_t kHandObjectBillboardsEndOffset = 0xFC;
constexpr std::size_t kHandObjectParticlesBeginOffset = 0x108;
constexpr std::size_t kHandObjectParticlesEndOffset = 0x10C;
constexpr std::size_t kHandObjectLightsBeginOffset = 0x118;
constexpr std::size_t kHandObjectLightsEndOffset = 0x11C;
constexpr std::size_t kEntityActiveOffset = 0x24;
constexpr std::size_t kSubMeshOwnerOffset = 0x370;
constexpr std::size_t kMaximumHandObjects = 64;
constexpr std::size_t kMaximumAttachmentsPerVector = 32;

bool IsReadable(const void* address, const std::size_t bytes) noexcept {
    if (address == nullptr || bytes == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
        return false;
    }
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY;
    if ((memory.Protect & readable) == 0) {
        return false;
    }
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    const auto regionStart = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto regionEnd = regionStart + memory.RegionSize;
    return start >= regionStart && start <= regionEnd && bytes <= regionEnd - start;
}

bool IsWritable(void* address, const std::size_t bytes) noexcept {
    if (address == nullptr || bytes == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
        return false;
    }
    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if ((memory.Protect & writable) == 0) {
        return false;
    }
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    const auto regionStart = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto regionEnd = regionStart + memory.RegionSize;
    return start >= regionStart && start <= regionEnd && bytes <= regionEnd - start;
}

template <typename T>
bool ReadAt(const void* base, const std::size_t offset, T& value) noexcept {
    if (base == nullptr) {
        return false;
    }
    const auto* address = static_cast<const std::uint8_t*>(base) + offset;
    if (!IsReadable(address, sizeof(T))) {
        return false;
    }
    std::memcpy(&value, address, sizeof(T));
    return true;
}

template <typename T> T ReadMappedField(const void* base, const std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

} // namespace

void Runtime::Configure(const Functions& functions) noexcept {
    (void)EndRender();
    functions_ = functions;
    history_.Reset();
    staged_ = {};
    identity_ = {};
    handObject_ = nullptr;
    handObjectEntity_ = nullptr;
    rootDelta_ = hands::Identity();
    presentationCorrection_ = hands::Identity();
    modelSlotCount_ = 0;
    ResetHandObjectHistories();
    attachmentSlots_ = {};
    attachmentBatch_ = {};
    attachmentSlotCount_ = 0;
    hasIdentity_ = false;
    hasPresentationCorrection_ = false;
    renderActive_ = false;
}

void Runtime::ResetHandObjectHistories() noexcept {
    for (HandObjectHistorySlot& slot : handObjectHistories_) {
        slot = {};
    }
}

Runtime::HandObjectHistorySlot* Runtime::FindOrCreateHandObjectHistory(
    void* const subMesh) noexcept {
    for (HandObjectHistorySlot& slot : handObjectHistories_) {
        if (slot.occupied && slot.subMesh == subMesh) {
            return &slot;
        }
    }
    for (HandObjectHistorySlot& slot : handObjectHistories_) {
        if (!slot.occupied) {
            slot = {};
            slot.subMesh = subMesh;
            slot.occupied = true;
            return &slot;
        }
    }
    return nullptr;
}

bool Runtime::SetPresentationCorrection(const Matrix* const correction) noexcept {
    hasPresentationCorrection_ = false;
    presentationCorrection_ = hands::Identity();
    if (correction == nullptr) {
        return true;
    }
    if (!IsRigid(*correction)) {
        return false;
    }
    presentationCorrection_ = *correction;
    hasPresentationCorrection_ = true;
    return true;
}

bool Runtime::ReadWorld(void* entity, Matrix& matrix) const noexcept {
    if (entity == nullptr || functions_.getWorldMatrix == nullptr) {
        return false;
    }
    using GetWorldMatrix = Matrix*(__thiscall*)(void*);
    const auto getWorld = reinterpret_cast<GetWorldMatrix>(functions_.getWorldMatrix);
    const Matrix* world = getWorld(entity);
    if (!IsReadable(world, sizeof(Matrix))) {
        return false;
    }
    std::memcpy(&matrix, world, sizeof(Matrix));
    return IsRigid(matrix);
}

bool Runtime::FindActiveHandObject(void* playerHands, void*& handObjectResult,
                                   void*& entity) const noexcept {
    handObjectResult = nullptr;
    entity = nullptr;
    void* beginPointer = nullptr;
    void* endPointer = nullptr;
    if (!ReadAt(playerHands, kHandObjectsBeginOffset, beginPointer) ||
        !ReadAt(playerHands, kHandObjectsEndOffset, endPointer)) {
        return false;
    }
    if (beginPointer == endPointer) {
        return true;
    }
    if (beginPointer == nullptr || endPointer == nullptr) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(beginPointer);
    const auto end = reinterpret_cast<std::uintptr_t>(endPointer);
    if (end < begin || (end - begin) % sizeof(void*) != 0) {
        return false;
    }
    const std::size_t count = (end - begin) / sizeof(void*);
    if (count > kMaximumHandObjects || !IsReadable(beginPointer, end - begin)) {
        return false;
    }
    for (std::size_t index = 0; index < count; ++index) {
        void* handObject = nullptr;
        void* meshEntity = nullptr;
        std::uint8_t active = 0;
        if (!ReadAt(beginPointer, index * sizeof(void*), handObject) ||
            handObject == nullptr ||
            !ReadAt(handObject, kHandObjectMeshEntityOffset, meshEntity)) {
            return false;
        }
        if (meshEntity == nullptr) {
            continue;
        }
        if (!ReadAt(meshEntity, kEntityActiveOffset, active) || active > 1U) {
            return false;
        }
        if (active != 0U) {
            if (entity != nullptr && entity != meshEntity) {
                return false;
            }
            handObjectResult = handObject;
            entity = meshEntity;
        }
    }
    return true;
}

FrameResult Runtime::Stage(void* playerHands) noexcept {
    staged_ = {};
    staged_.identity.helper = playerHands;
    std::uint8_t active = 0;
    if (!ReadAt(playerHands, kHandsEntityOffset, staged_.identity.entity) ||
        staged_.identity.entity == nullptr ||
        !ReadAt(staged_.identity.entity, kEntityActiveOffset, active) || active > 1U ||
        active == 0U || !ReadWorld(staged_.identity.entity, staged_.world) ||
        !FindActiveHandObject(playerHands, staged_.handObject,
                              staged_.handObjectEntity)) {
        return FrameResult::invalid;
    }
    staged_.valid = true;
    return FrameResult::staged;
}

FrameResult Runtime::CommitCompletedTick(const std::uint64_t tickSerial,
                                         const std::uint64_t clockGeneration) noexcept {
    currentSerial_ = tickSerial;
    currentGeneration_ = clockGeneration;
    if (!staged_.valid) {
        history_.Reset();
        ResetHandObjectHistories();
        hasIdentity_ = false;
        return FrameResult::unavailable;
    }

    const Staged completed = staged_;
    staged_ = {};
    handObject_ = completed.handObject;
    if (handObjectEntity_ != completed.handObjectEntity) {
        ResetHandObjectHistories();
    }
    handObjectEntity_ = completed.handObjectEntity;
    if (!hasIdentity_ || identity_.helper != completed.identity.helper ||
        identity_.entity != completed.identity.entity) {
        identity_ = completed.identity;
        hasIdentity_ = true;
        history_.Reset();
        ResetHandObjectHistories();
        (void)history_.Publish(tickSerial, clockGeneration, completed.world);
        return FrameResult::identityChanged;
    }

    switch (history_.Publish(tickSerial, clockGeneration, completed.world)) {
    case PublishKind::appended:
    case PublishKind::unchanged:
        return FrameResult::captured;
    case PublishKind::invalid:
        return FrameResult::invalid;
    case PublishKind::resetGeneration:
        return FrameResult::clockGenerationReset;
    case PublishKind::resetNonMonotonic:
        return FrameResult::nonMonotonicReset;
    case PublishKind::resetSameTickMutation:
        return FrameResult::sameTickMutation;
    case PublishKind::resetDiscontinuity:
        return FrameResult::discontinuityReset;
    }
    return FrameResult::invalid;
}

FrameResult Runtime::BeginRender(const clock::Sample& clockSample) noexcept {
    (void)EndRender();
    attachmentBatch_ = {};
    renderClockSample_ = clockSample;
    if (!hasIdentity_ || identity_.entity == nullptr) {
        return FrameResult::unavailable;
    }
    const SampleResult sampled = history_.Evaluate(clockSample);
    if (sampled.kind == SampleKind::clockUnavailable ||
        sampled.kind == SampleKind::generationMismatch) {
        return FrameResult::clockUnavailable;
    }
    if (sampled.kind == SampleKind::snapshotsMissing) {
        return FrameResult::snapshotsMissing;
    }
    if (sampled.kind != SampleKind::applied) {
        return FrameResult::invalid;
    }

    Matrix authoritative{};
    Matrix inverse{};
    if (!ReadWorld(identity_.entity, authoritative) ||
        !InverseRigid(authoritative, inverse) ||
        !Multiply(sampled.matrix, inverse, rootDelta_)) {
        return FrameResult::applyFailed;
    }
    if (hasPresentationCorrection_) {
        Matrix corrected{};
        if (!Multiply(presentationCorrection_, rootDelta_, corrected) ||
            !IsRigid(corrected)) {
            return FrameResult::applyFailed;
        }
        rootDelta_ = corrected;
    }
    renderActive_ = true;
    PrepareAttachments();
    return FrameResult::renderReady;
}

void Runtime::PrepareAttachmentVector(const std::size_t beginOffset,
                                      const std::size_t endOffset) noexcept {
    void* beginPointer = nullptr;
    void* endPointer = nullptr;
    if (!ReadAt(handObject_, beginOffset, beginPointer) ||
        !ReadAt(handObject_, endOffset, endPointer)) {
        ++attachmentBatch_.invalid;
        return;
    }
    if (beginPointer == endPointer) {
        return;
    }
    if (beginPointer == nullptr || endPointer == nullptr) {
        ++attachmentBatch_.invalid;
        return;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(beginPointer);
    const auto end = reinterpret_cast<std::uintptr_t>(endPointer);
    if (end < begin || (end - begin) % sizeof(void*) != 0) {
        ++attachmentBatch_.invalid;
        return;
    }
    const std::size_t count = (end - begin) / sizeof(void*);
    if (count > kMaximumAttachmentsPerVector ||
        !IsReadable(beginPointer, end - begin)) {
        ++attachmentBatch_.invalid;
        return;
    }

    using GetEntityParent = void*(__thiscall*)(void*);
    using GetWorldMatrix = Matrix*(__thiscall*)(void*);
    const auto getParent =
        reinterpret_cast<GetEntityParent>(functions_.getEntityParent);
    const auto getWorld = reinterpret_cast<GetWorldMatrix>(functions_.getWorldMatrix);
    for (std::size_t index = 0; index < count; ++index) {
        ++attachmentBatch_.candidates;
        void* attachment = nullptr;
        if (!ReadAt(beginPointer, index * sizeof(void*), attachment) ||
            attachment == nullptr || getParent == nullptr || getWorld == nullptr) {
            ++attachmentBatch_.invalid;
            continue;
        }
        if (getParent(attachment) != handObjectEntity_) {
            ++attachmentBatch_.parentMismatch;
            continue;
        }
        bool duplicate = false;
        for (std::size_t slotIndex = 0; slotIndex < attachmentSlotCount_; ++slotIndex) {
            if (attachmentSlots_[slotIndex].entity == attachment) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        if (attachmentSlotCount_ == attachmentSlots_.size()) {
            ++attachmentBatch_.capacityExhausted;
            continue;
        }
        Matrix* world = getWorld(attachment);
        if (!IsWritable(world, sizeof(Matrix)) || !IsRigid(*world)) {
            ++attachmentBatch_.invalid;
            continue;
        }
        Matrix interpolated{};
        if (!Multiply(rootDelta_, *world, interpolated)) {
            ++attachmentBatch_.invalid;
            continue;
        }
        AttachmentSlot& slot = attachmentSlots_[attachmentSlotCount_++];
        slot.entity = attachment;
        slot.world = world;
        slot.restore = *world;
        *world = interpolated;
        ++attachmentBatch_.applied;
    }
}

void Runtime::PrepareAttachments() noexcept {
    if (handObject_ == nullptr || handObjectEntity_ == nullptr ||
        functions_.getEntityParent == nullptr || functions_.getWorldMatrix == nullptr) {
        return;
    }
    PrepareAttachmentVector(kHandObjectBillboardsBeginOffset,
                            kHandObjectBillboardsEndOffset);
    PrepareAttachmentVector(kHandObjectParticlesBeginOffset,
                            kHandObjectParticlesEndOffset);
    PrepareAttachmentVector(kHandObjectLightsBeginOffset,
                            kHandObjectLightsEndOffset);
}

OverrideResult Runtime::OverrideModelMatrix(void* subMesh,
                                           const Matrix* original,
                                           const Matrix*& result) noexcept {
    result = original;
    if (!renderActive_ || subMesh == nullptr) {
        return OverrideResult::notHands;
    }
    // This hook is executing a method on a live cSubMeshEntity from the
    // signature-gated compatible build, so the mapped owner field is necessarily in
    // the same readable object. VirtualQuery here used to run for every visible
    // submesh, including hundreds of thousands of immediate rejections.
    void* owner = ReadMappedField<void*>(subMesh, kSubMeshOwnerOffset);
    const bool handsOwner = owner == identity_.entity;
    const bool handObjectOwner = handObjectEntity_ != nullptr &&
                                 owner == handObjectEntity_;
    if (!handsOwner && !handObjectOwner) {
        return OverrideResult::notHands;
    }
    if (!IsReadable(original, sizeof(Matrix))) {
        return OverrideResult::invalid;
    }

    ModelSlot* slot = nullptr;
    for (std::size_t index = 0; index < modelSlotCount_; ++index) {
        if (modelSlots_[index].subMesh == subMesh) {
            slot = &modelSlots_[index];
            break;
        }
    }
    if (slot == nullptr) {
        if (modelSlotCount_ == modelSlots_.size()) {
            return OverrideResult::slotsExhausted;
        }
        slot = &modelSlots_[modelSlotCount_++];
        slot->subMesh = subMesh;
    }
    if (handObjectOwner) {
        HandObjectHistorySlot* const history =
            FindOrCreateHandObjectHistory(subMesh);
        if (history == nullptr) {
            return OverrideResult::slotsExhausted;
        }
        if (history->lastCapturedSerial != currentSerial_) {
            const PublishKind published = history->history.Publish(
                currentSerial_, currentGeneration_, *original);
            history->lastCapturedSerial = currentSerial_;
            if (published == PublishKind::invalid) {
                history->history.Reset();
            }
        }
        const SampleResult sampled = history->history.Evaluate(renderClockSample_);
        if (sampled.kind == SampleKind::applied) {
            slot->matrix = sampled.matrix;
            if (hasPresentationCorrection_) {
                Matrix corrected{};
                if (!Multiply(presentationCorrection_, slot->matrix, corrected)) {
                    return OverrideResult::invalid;
                }
                slot->matrix = corrected;
            }
            result = &slot->matrix;
            return OverrideResult::appliedHandObjectHistory;
        }
    }
    if (!Multiply(rootDelta_, *original, slot->matrix)) {
        return OverrideResult::invalid;
    }
    result = &slot->matrix;
    return handObjectOwner ? OverrideResult::appliedHandObject
                           : OverrideResult::applied;
}

const AttachmentBatch& Runtime::LastAttachmentBatch() const noexcept {
    return attachmentBatch_;
}

std::uint32_t Runtime::EndRender() noexcept {
    std::uint32_t restoreFailures = 0;
    for (std::size_t index = attachmentSlotCount_; index > 0; --index) {
        AttachmentSlot& slot = attachmentSlots_[index - 1];
        if (IsWritable(slot.world, sizeof(Matrix))) {
            *slot.world = slot.restore;
        } else {
            ++restoreFailures;
        }
        slot = {};
    }
    renderActive_ = false;
    modelSlotCount_ = 0;
    attachmentSlotCount_ = 0;
    return restoreFailures;
}

const wchar_t* FrameResultName(const FrameResult result) noexcept {
    switch (result) {
    case FrameResult::staged: return L"staged";
    case FrameResult::captured: return L"captured";
    case FrameResult::renderReady: return L"render-ready";
    case FrameResult::unavailable: return L"unavailable";
    case FrameResult::identityChanged: return L"identity-changed";
    case FrameResult::invalid: return L"invalid";
    case FrameResult::clockGenerationReset: return L"generation";
    case FrameResult::nonMonotonicReset: return L"non-monotonic";
    case FrameResult::sameTickMutation: return L"same-tick-mutation";
    case FrameResult::discontinuityReset: return L"discontinuity";
    case FrameResult::clockUnavailable: return L"clock-unavailable";
    case FrameResult::snapshotsMissing: return L"snapshots-missing";
    case FrameResult::applyFailed: return L"apply-failed";
    }
    return L"unknown";
}

const wchar_t* OverrideResultName(const OverrideResult result) noexcept {
    switch (result) {
    case OverrideResult::notHands: return L"not-hands";
    case OverrideResult::applied: return L"applied";
    case OverrideResult::appliedHandObject: return L"applied-hand-object";
    case OverrideResult::appliedHandObjectHistory:
        return L"applied-hand-object-history";
    case OverrideResult::invalid: return L"invalid";
    case OverrideResult::slotsExhausted: return L"slots-exhausted";
    }
    return L"unknown";
}

} // namespace atdd::hands
