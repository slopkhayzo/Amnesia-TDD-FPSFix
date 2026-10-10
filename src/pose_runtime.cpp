#include "pose_runtime.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace atdd::pose {
namespace {

constexpr std::size_t kHandsMeshOffset = 0x44;
constexpr std::size_t kEntityActiveOffset = 0x24;
constexpr std::size_t kBoneMatricesBeginOffset = 0x380;
constexpr std::size_t kBoneMatricesEndOffset = 0x384;
constexpr std::size_t kBoneMatricesCapacityOffset = 0x388;

bool HasAccess(const void* address, const std::size_t bytes, const DWORD allowed) noexcept {
    if (address == nullptr || bytes == 0) return false;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0 ||
        (memory.Protect & allowed) == 0) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto regionEnd = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) +
                           memory.RegionSize;
    return begin <= regionEnd && bytes <= regionEnd - begin;
}

bool IsReadable(const void* address, const std::size_t bytes) noexcept {
    constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                               PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                               PAGE_EXECUTE_WRITECOPY;
    return HasAccess(address, bytes, readable);
}

bool IsWritable(void* address, const std::size_t bytes) noexcept {
    constexpr DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
                               PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return HasAccess(address, bytes, writable);
}

template <typename T>
bool ReadAt(const void* base, const std::size_t offset, T& value) noexcept {
    const auto* address = static_cast<const std::uint8_t*>(base) + offset;
    if (!IsReadable(address, sizeof(T))) return false;
    std::memcpy(&value, address, sizeof(T));
    return true;
}

struct VectorView {
    hands::Matrix* begin = nullptr;
    hands::Matrix* end = nullptr;
    hands::Matrix* capacity = nullptr;
    std::size_t count = 0;
};

bool ReadVector(void* mesh, VectorView& view) noexcept {
    if (!ReadAt(mesh, kBoneMatricesBeginOffset, view.begin) ||
        !ReadAt(mesh, kBoneMatricesEndOffset, view.end) ||
        !ReadAt(mesh, kBoneMatricesCapacityOffset, view.capacity) ||
        view.begin == nullptr) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(view.begin);
    const auto end = reinterpret_cast<std::uintptr_t>(view.end);
    const auto capacity = reinterpret_cast<std::uintptr_t>(view.capacity);
    if (end < begin || capacity < end) return false;
    const auto bytes = end - begin;
    const auto capacityBytes = capacity - begin;
    if (bytes % sizeof(hands::Matrix) != 0 ||
        capacityBytes % sizeof(hands::Matrix) != 0) {
        return false;
    }
    view.count = bytes / sizeof(hands::Matrix);
    return view.count != 0 && view.count <= kMaximumBones &&
           IsReadable(view.begin, bytes) && IsWritable(view.begin, bytes);
}

} // namespace

void Runtime::Configure(const Functions& functions) noexcept {
    Reset();
    functions_ = functions;
}

StageResult Runtime::StageHands(void* playerHands,
                                const std::uint64_t pendingSerial) noexcept {
    void* mesh = nullptr;
    std::uint8_t active = 0;
    if (playerHands == nullptr ||
        !ReadAt(playerHands, kHandsMeshOffset, mesh) || mesh == nullptr) {
        staged_ = {};
        return StageResult::unavailable;
    }
    if (!ReadAt(mesh, kEntityActiveOffset, active) || active > 1) {
        staged_ = {};
        return StageResult::unavailable;
    }
    if (active == 0) {
        staged_ = {};
        return StageResult::inactive;
    }
    staged_ = {mesh, pendingSerial, true};
    return StageResult::staged;
}

void Runtime::CommitCompletedTick(const std::uint64_t tickSerial,
                                  const std::uint64_t clockGeneration) noexcept {
    currentSerial_ = tickSerial;
    currentGeneration_ = clockGeneration;
    if (!staged_.valid || staged_.pendingSerial != tickSerial) {
        targetMesh_ = nullptr;
        identityBuffer_ = nullptr;
        identityBoneCount_ = 0;
        lastCapturedSerial_ = 0;
        hasCapture_ = false;
        history_.Reset();
        staged_ = {};
        return;
    }
    if (targetMesh_ != staged_.mesh) {
        targetMesh_ = staged_.mesh;
        identityBuffer_ = nullptr;
        identityBoneCount_ = 0;
        lastCapturedSerial_ = 0;
        hasCapture_ = false;
        history_.Reset();
    }
    staged_ = {};
}

UpdateResult Runtime::BeginRender(const clock::Sample& clockSample,
                                  const std::uint64_t tickSerial,
                                  const float frameTime) noexcept {
    if (overridden_) {
        (void)EndRender();
    }
    clockSample_ = clockSample;
    renderActive_ = targetMesh_ != nullptr && tickSerial == currentSerial_;
    if (!renderActive_ || functions_.updateGraphicsForFrame == nullptr) {
        return UpdateResult::notTarget;
    }
    using UpdateGraphicsForFrame = void(__thiscall*)(void*, float);
    reinterpret_cast<UpdateGraphicsForFrame>(functions_.updateGraphicsForFrame)(
        targetMesh_, frameTime);
    return OnMeshUpdated(targetMesh_);
}

UpdateResult Runtime::OnMeshUpdated(void* mesh) noexcept {
    if (!renderActive_ || mesh == nullptr || mesh != targetMesh_) {
        return UpdateResult::notTarget;
    }
    if (overridden_) {
        return mesh == targetMesh_ ? UpdateResult::alreadyApplied :
                                     UpdateResult::notTarget;
    }

    VectorView vector{};
    if (!ReadVector(mesh, vector)) {
        history_.Reset();
        hasCapture_ = false;
        return UpdateResult::unavailable;
    }
    const bool identityChanged = identityBuffer_ != vector.begin ||
                                 identityBoneCount_ != vector.count;
    if (identityChanged) {
        history_.Reset();
        identityBuffer_ = vector.begin;
        identityBoneCount_ = vector.count;
        lastCapturedSerial_ = 0;
        hasCapture_ = false;
    }

    UpdateResult captureResult = identityChanged ? UpdateResult::identityChanged :
                                                   UpdateResult::captured;
    if (!hasCapture_ || lastCapturedSerial_ != currentSerial_) {
        Pose authoritative{};
        authoritative.boneCount = vector.count;
        std::memcpy(authoritative.bones.data(), vector.begin,
                    vector.count * sizeof(hands::Matrix));
        for (std::size_t index = 0; index < authoritative.boneCount; ++index) {
            if (!IsValidMatrix(authoritative.bones[index])) {
                invalidBoneIndex_ = index;
                invalidMatrix_ = authoritative.bones[index];
                hasInvalidDiagnostic_ = true;
                break;
            }
        }
        const PublishKind published =
            history_.Publish(currentSerial_, currentGeneration_, authoritative);
        lastCapturedSerial_ = currentSerial_;
        hasCapture_ = true;
        switch (published) {
        case PublishKind::invalid:
            return UpdateResult::invalid;
        case PublishKind::resetGeneration:
        case PublishKind::resetNonMonotonic:
        case PublishKind::resetSameTickMutation:
        case PublishKind::resetBoneCount:
        case PublishKind::resetDiscontinuity:
            captureResult = UpdateResult::identityChanged;
            break;
        case PublishKind::appended:
        case PublishKind::unchanged:
            break;
        }
    }

    const SampleResult sampled = history_.Evaluate(clockSample_);
    switch (sampled.kind) {
    case SampleKind::clockUnavailable:
    case SampleKind::generationMismatch:
        return UpdateResult::clockUnavailable;
    case SampleKind::snapshotsMissing:
        return captureResult == UpdateResult::identityChanged ? captureResult :
                                                                UpdateResult::pairMissing;
    case SampleKind::invalid:
        return UpdateResult::invalid;
    case SampleKind::applied:
        break;
    }
    if (sampled.pose.boneCount != vector.count) {
        history_.Reset();
        hasCapture_ = false;
        return UpdateResult::invalid;
    }

    const std::size_t bytes = vector.count * sizeof(hands::Matrix);
    std::memcpy(restore_.data(), vector.begin, bytes);
    std::memcpy(vector.begin, sampled.pose.bones.data(), bytes);
    overrideBuffer_ = vector.begin;
    overrideBoneCount_ = vector.count;
    overridden_ = true;
    return UpdateResult::applied;
}

bool Runtime::EndRender() noexcept {
    renderActive_ = false;
    if (!overridden_) return true;
    const std::size_t bytes = overrideBoneCount_ * sizeof(hands::Matrix);
    const bool restored = IsWritable(overrideBuffer_, bytes);
    if (restored) {
        std::memcpy(overrideBuffer_, restore_.data(), bytes);
    }
    overrideBuffer_ = nullptr;
    overrideBoneCount_ = 0;
    overridden_ = false;
    return restored;
}

bool Runtime::TakeInvalidMatrixDiagnostic(std::size_t& boneIndex,
                                          hands::Matrix& matrix) noexcept {
    if (!hasInvalidDiagnostic_) return false;
    boneIndex = invalidBoneIndex_;
    matrix = invalidMatrix_;
    hasInvalidDiagnostic_ = false;
    return true;
}

void Runtime::Reset() noexcept {
    (void)EndRender();
    history_.Reset();
    staged_ = {};
    clockSample_ = {};
    targetMesh_ = nullptr;
    identityBuffer_ = nullptr;
    identityBoneCount_ = 0;
    currentSerial_ = 0;
    currentGeneration_ = 0;
    lastCapturedSerial_ = 0;
    hasCapture_ = false;
    hasInvalidDiagnostic_ = false;
}

const wchar_t* StageResultName(const StageResult result) noexcept {
    switch (result) {
    case StageResult::staged: return L"staged";
    case StageResult::unavailable: return L"unavailable";
    case StageResult::inactive: return L"inactive";
    }
    return L"unknown";
}

const wchar_t* UpdateResultName(const UpdateResult result) noexcept {
    switch (result) {
    case UpdateResult::notTarget: return L"not-target";
    case UpdateResult::captured: return L"captured";
    case UpdateResult::applied: return L"applied";
    case UpdateResult::alreadyApplied: return L"already-applied";
    case UpdateResult::unavailable: return L"unavailable";
    case UpdateResult::identityChanged: return L"identity-changed";
    case UpdateResult::clockUnavailable: return L"clock-unavailable";
    case UpdateResult::pairMissing: return L"pair-missing";
    case UpdateResult::invalid: return L"invalid";
    }
    return L"unknown";
}

} // namespace atdd::pose
