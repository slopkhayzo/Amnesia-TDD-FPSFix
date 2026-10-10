#include "enemy_pose_runtime.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace atdd::enemy_pose {
namespace {

constexpr std::size_t kEntityIdOffset = 0x40;
constexpr std::size_t kEnemyMeshEntityOffset = 0x54C;
constexpr std::size_t kEnemyTypeOffset = 0x82C;
constexpr std::size_t kEntityActiveOffset = 0x24;
constexpr std::size_t kBoneMatricesBeginOffset = 0x380;
constexpr std::size_t kBoneMatricesEndOffset = 0x384;
constexpr std::size_t kBoneMatricesCapacityOffset = 0x388;
constexpr std::size_t kMeshSubMeshesBeginOffset = 0x30C;
constexpr std::size_t kMeshSubMeshesEndOffset = 0x310;
constexpr std::size_t kSubMeshRenderFrameOffset = 0x344;
constexpr std::int32_t kEnemyTypeGrunt = 0;
constexpr std::size_t kMinimumRagdollBodies = 2;
constexpr std::size_t kMaximumPropBodies = 32;
constexpr std::size_t kMaximumSubMeshes = 128;
constexpr std::uint8_t kVisibilityGraceFrames = 2;

bool HasAccess(const void* address,
               const std::size_t bytes,
               const DWORD allowed) noexcept {
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
    if (base == nullptr) return false;
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
    return view.count != 0 && view.count <= pose::kMaximumBones &&
           IsReadable(view.begin, bytes) && IsWritable(view.begin, bytes);
}

bool ReadLatestRenderFrame(void* mesh, std::int32_t& latest) noexcept {
    void** begin = nullptr;
    void** end = nullptr;
    if (!ReadAt(mesh, kMeshSubMeshesBeginOffset, begin) ||
        !ReadAt(mesh, kMeshSubMeshesEndOffset, end) || begin == nullptr ||
        end == nullptr) {
        return false;
    }
    const auto beginAddress = reinterpret_cast<std::uintptr_t>(begin);
    const auto endAddress = reinterpret_cast<std::uintptr_t>(end);
    if (endAddress < beginAddress ||
        (endAddress - beginAddress) % sizeof(void*) != 0) {
        return false;
    }
    const std::size_t count = (endAddress - beginAddress) / sizeof(void*);
    if (count == 0 || count > kMaximumSubMeshes ||
        !IsReadable(begin, count * sizeof(void*))) {
        return false;
    }

    bool found = false;
    std::int32_t newest = -1;
    for (std::size_t index = 0; index < count; ++index) {
        void* subMesh = nullptr;
        std::memcpy(&subMesh, begin + index, sizeof(subMesh));
        std::int32_t frame = -1;
        if (subMesh == nullptr ||
            !ReadAt(subMesh, kSubMeshRenderFrameOffset, frame)) {
            return false;
        }
        if (!found || frame > newest) newest = frame;
        found = true;
    }
    latest = newest;
    return found;
}

} // namespace

void Runtime::Configure(const Functions& functions) noexcept {
    Reset();
    functions_ = functions;
}

Runtime::Slot* Runtime::Find(void* entity) noexcept {
    for (auto& slot : slots_) {
        if (slot.occupied && slot.entity == entity) return &slot;
    }
    return nullptr;
}

Runtime::Slot* Runtime::FindFree() noexcept {
    for (auto& slot : slots_) {
        if (!slot.occupied) return &slot;
    }
    return nullptr;
}

void Runtime::Clear(Slot& slot) noexcept {
    slot = {};
}

StageResult Runtime::StageTarget(void* entity, void* mesh, const std::int32_t id,
                                 const Kind kind,
                                 const std::uint64_t pendingSerial,
                                 const bool sleeping) noexcept {
    Slot* slot = Find(entity);
    if (slot == nullptr) {
        slot = FindFree();
        if (slot == nullptr) return StageResult::capacityExhausted;
        slot->occupied = true;
        slot->entity = entity;
    }
    if (slot->mesh != nullptr &&
        (slot->mesh != mesh || slot->id != id || slot->kind != kind)) {
        slot->history.Reset();
        slot->identityBuffer = nullptr;
        slot->identityBoneCount = 0;
        slot->lastCapturedSerial = 0;
        slot->hasCapture = false;
        slot->lastRenderFrame = -1;
        slot->visibilityGrace = 0;
        slot->visibilityInitialized = false;
    }
    slot->mesh = mesh;
    slot->id = id;
    slot->kind = kind;
    slot->stagedSerial = pendingSerial;
    slot->sleeping = sleeping;
    return kind == Kind::enemyGrunt ? StageResult::stagedEnemy
                                    : StageResult::stagedRagdoll;
}

StageResult Runtime::StageEnemy(void* enemy,
                                const std::uint64_t pendingSerial) noexcept {
    void* mesh = nullptr;
    std::int32_t id = 0;
    std::int32_t type = -1;
    std::uint8_t active = 0;
    if (enemy == nullptr || !ReadAt(enemy, kEnemyTypeOffset, type)) {
        return StageResult::invalid;
    }
    if (type != kEnemyTypeGrunt) {
        if (Slot* existing = Find(enemy); existing != nullptr) Clear(*existing);
        return StageResult::ineligible;
    }
    if (!ReadAt(enemy, kEntityIdOffset, id) ||
        !ReadAt(enemy, kEnemyMeshEntityOffset, mesh) || mesh == nullptr ||
        !ReadAt(mesh, kEntityActiveOffset, active) || active > 1) {
        if (Slot* existing = Find(enemy); existing != nullptr) Clear(*existing);
        return StageResult::invalid;
    }
    if (active == 0) {
        if (Slot* existing = Find(enemy); existing != nullptr) Clear(*existing);
        return StageResult::ineligible;
    }

    return StageTarget(enemy, mesh, id, Kind::enemyGrunt, pendingSerial);
}

StageResult Runtime::StageRagdollProp(void* prop, void* mesh,
                                      const std::int32_t id,
                                      const std::size_t bodyCount,
                                      const bool allSleeping,
                                      const std::uint64_t pendingSerial) noexcept {
    std::uint8_t active = 0;
    if (prop == nullptr || mesh == nullptr) {
        return StageResult::invalid;
    }
    if (bodyCount < kMinimumRagdollBodies || bodyCount > kMaximumPropBodies) {
        if (Slot* existing = Find(prop); existing != nullptr) Clear(*existing);
        return StageResult::ineligible;
    }
    Slot* existing = Find(prop);
    // A target which was already presented while awake gets one final sleeping
    // endpoint. A subsequently sleeping tick retires it until world staging
    // reports that its bodies have woken again.
    if (allSleeping && (existing == nullptr || existing->sleeping)) {
        if (existing != nullptr) Clear(*existing);
        return StageResult::ineligible;
    }
    if (!ReadAt(mesh, kEntityActiveOffset, active) || active > 1) {
        if (existing != nullptr) Clear(*existing);
        return StageResult::invalid;
    }
    VectorView vector{};
    const bool knownTarget = existing != nullptr && existing->mesh == mesh &&
                             existing->id == id &&
                             existing->kind == Kind::ragdollProp;
    if (active == 0 || (!knownTarget && !ReadVector(mesh, vector))) {
        if (existing != nullptr) Clear(*existing);
        return StageResult::ineligible;
    }
    return StageTarget(prop, mesh, id, Kind::ragdollProp, pendingSerial, allSleeping);
}

void Runtime::CommitCompletedTick(const std::uint64_t tickSerial,
                                  const std::uint64_t clockGeneration) noexcept {
    if (renderActive_) (void)EndRender();
    currentSerial_ = tickSerial;
    currentGeneration_ = clockGeneration;
    for (auto& slot : slots_) {
        if (!slot.occupied) continue;
        if (slot.stagedSerial != tickSerial) {
            Clear(slot);
            continue;
        }
        slot.committedSerial = tickSerial;
    }
}

BatchResult Runtime::BeginRender(const clock::Sample& clockSample,
                                 const std::uint64_t tickSerial,
                                 const float frameTime) noexcept {
    BatchResult result{};
    if (renderActive_) {
        const BatchResult restored = EndRender();
        result.restoreFailed += restored.restoreFailed;
    }
    if (functions_.updateGraphicsForFrame == nullptr || tickSerial != currentSerial_) {
        return result;
    }
    renderActive_ = true;
    using UpdateGraphicsForFrame = void(__thiscall*)(void*, float);
    const auto update =
        reinterpret_cast<UpdateGraphicsForFrame>(functions_.updateGraphicsForFrame);

    for (auto& slot : slots_) {
        if (!slot.occupied || slot.committedSerial != currentSerial_) continue;

        // Articulated props can remain awake indefinitely because of their
        // joints even when they are outside every viewport. HPL writes the
        // renderer's monotonically increasing frame number into each submesh
        // when cRenderList actually accepts it. Sample that existing stamp at
        // the next scene boundary, retaining two missed presentations as a
        // small occlusion grace period. Live enemies stay on their established
        // path; this gate is specifically for the broader physics-backed prop
        // class introduced for ragdolls.
        if (slot.kind == Kind::ragdollProp) {
            std::int32_t renderFrame = -1;
            if (!ReadLatestRenderFrame(slot.mesh, renderFrame)) {
                ++result.visibilityUnavailable;
            } else if (!slot.visibilityInitialized) {
                slot.visibilityInitialized = true;
                slot.lastRenderFrame = renderFrame;
                if (renderFrame < 0) {
                    ++result.visibilitySkipped;
                    continue;
                }
                slot.visibilityGrace = kVisibilityGraceFrames;
            } else if (renderFrame != slot.lastRenderFrame) {
                slot.lastRenderFrame = renderFrame;
                slot.visibilityGrace = kVisibilityGraceFrames;
            } else if (slot.visibilityGrace != 0) {
                --slot.visibilityGrace;
            } else {
                ++result.visibilitySkipped;
                continue;
            }
        }
        ++result.targets;
        VectorView vector{};
        const bool needsCapture = !slot.hasCapture ||
                                  slot.lastCapturedSerial != currentSerial_;
        if (needsCapture) {
            // Staging has already performed the entity/mesh/body lifecycle
            // validation for this completed tick. Update and validate the
            // palette once; subsequent presentations reuse that exact vector.
            update(slot.mesh, frameTime);
            if (!ReadVector(slot.mesh, vector)) {
                slot.history.Reset();
                slot.hasCapture = false;
                ++result.unavailable;
                continue;
            }
            const bool identityChanged = slot.identityBuffer != vector.begin ||
                                         slot.identityBoneCount != vector.count;
            if (identityChanged) {
                slot.history.Reset();
                slot.identityBuffer = vector.begin;
                slot.identityBoneCount = vector.count;
                slot.lastCapturedSerial = 0;
                slot.hasCapture = false;
                ++result.identityChanged;
            }

            pose::Pose authoritative{};
            authoritative.boneCount = vector.count;
            std::memcpy(authoritative.bones.data(), vector.begin,
                        vector.count * sizeof(hands::Matrix));
            const pose::PublishKind published = slot.history.Publish(
                currentSerial_, currentGeneration_, authoritative);
            slot.lastCapturedSerial = currentSerial_;
            slot.hasCapture = true;
            if (published == pose::PublishKind::invalid) {
                ++result.invalid;
                continue;
            }
            switch (published) {
            case pose::PublishKind::resetGeneration:
                ++result.generationReset;
                break;
            case pose::PublishKind::resetNonMonotonic:
                ++result.nonMonotonicReset;
                break;
            case pose::PublishKind::resetSameTickMutation:
                ++result.sameTickMutation;
                break;
            case pose::PublishKind::resetBoneCount:
                ++result.boneCountReset;
                break;
            case pose::PublishKind::resetDiscontinuity:
                ++result.discontinuityReset;
                break;
            case pose::PublishKind::appended:
            case pose::PublishKind::unchanged:
            case pose::PublishKind::invalid:
                break;
            }
            ++result.captured;
        } else {
            vector.begin = static_cast<hands::Matrix*>(slot.identityBuffer);
            vector.count = slot.identityBoneCount;
            const std::size_t bytes = vector.count * sizeof(hands::Matrix);
            if (vector.begin == nullptr || vector.count == 0 ||
                vector.count > pose::kMaximumBones ||
                !IsWritable(vector.begin, bytes)) {
                slot.history.Reset();
                slot.hasCapture = false;
                ++result.unavailable;
                continue;
            }
        }

        const pose::SampleResult sampled = slot.history.Evaluate(clockSample);
        switch (sampled.kind) {
        case pose::SampleKind::clockUnavailable:
        case pose::SampleKind::generationMismatch:
            ++result.clockWait;
            continue;
        case pose::SampleKind::snapshotsMissing:
            ++result.pairMissing;
            continue;
        case pose::SampleKind::invalid:
            ++result.invalid;
            continue;
        case pose::SampleKind::applied:
            break;
        }
        if (sampled.pose.boneCount != vector.count) {
            slot.history.Reset();
            slot.hasCapture = false;
            ++result.invalid;
            continue;
        }

        const std::size_t bytes = vector.count * sizeof(hands::Matrix);
        std::memcpy(slot.restore.data(), vector.begin, bytes);
        std::memcpy(vector.begin, sampled.pose.bones.data(), bytes);
        slot.overrideBuffer = vector.begin;
        slot.overrideBoneCount = vector.count;
        slot.overridden = true;
        ++result.applied;
    }
    return result;
}

BatchResult Runtime::EndRender() noexcept {
    BatchResult result{};
    renderActive_ = false;
    for (auto& slot : slots_) {
        if (!slot.overridden) continue;
        const std::size_t bytes = slot.overrideBoneCount * sizeof(hands::Matrix);
        if (IsWritable(slot.overrideBuffer, bytes)) {
            std::memcpy(slot.overrideBuffer, slot.restore.data(), bytes);
        } else {
            slot.history.Reset();
            slot.hasCapture = false;
            ++result.restoreFailed;
        }
        slot.overrideBuffer = nullptr;
        slot.overrideBoneCount = 0;
        slot.overridden = false;
    }
    return result;
}

void Runtime::Reset() noexcept {
    (void)EndRender();
    for (auto& slot : slots_) Clear(slot);
    currentSerial_ = 0;
    currentGeneration_ = 0;
}

const wchar_t* StageResultName(const StageResult result) noexcept {
    switch (result) {
    case StageResult::stagedEnemy: return L"staged-enemy";
    case StageResult::stagedRagdoll: return L"staged-ragdoll";
    case StageResult::ineligible: return L"ineligible";
    case StageResult::invalid: return L"invalid";
    case StageResult::capacityExhausted: return L"capacity-exhausted";
    }
    return L"unknown";
}

} // namespace atdd::enemy_pose
