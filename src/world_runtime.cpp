#include "world_runtime.h"

#include <Windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace atdd::world {
namespace {

constexpr std::size_t kEntityIdOffset = 0x40;
constexpr std::size_t kPropStaticPhysicsOffset = 0x119;
constexpr std::size_t kPropBodiesBeginOffset = 0x260;
constexpr std::size_t kPropBodiesEndOffset = 0x264;
constexpr std::size_t kPropMeshEntityOffset = 0x340;
constexpr std::size_t kPropTypeOffset = 0x484;
constexpr std::size_t kEnemyMeshEntityOffset = 0x54C;
constexpr std::size_t kEnemyTypeOffset = 0x82C;
constexpr std::size_t kMeshSubMeshesBeginOffset = 0x30C;
constexpr std::size_t kMeshSubMeshesEndOffset = 0x310;
constexpr std::size_t kEntity3DParentNodeOffset = 0x04;
constexpr std::size_t kEntity3DActiveOffset = 0x24;
constexpr std::size_t kEntity3DChildrenCountOffset = 0x280;
constexpr std::size_t kEntity3DEntityParentOffset = 0x288;
constexpr std::size_t kSubMeshOwnerOffset = 0x370;
constexpr std::int32_t kFirstPropType = 0;
constexpr std::int32_t kPropTypeLastEnum = 16;
constexpr std::int32_t kEnemyTypeGrunt = 0;

bool IsReadable(const void* address, const std::size_t bytes) noexcept {
    if (address == nullptr || bytes == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
        return false;
    }
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
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
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
        return false;
    }
    const DWORD writable =
        PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if ((memory.Protect & writable) == 0) {
        return false;
    }
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    const auto regionStart = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto regionEnd = regionStart + memory.RegionSize;
    return start >= regionStart && start <= regionEnd && bytes <= regionEnd - start;
}

template <typename T> bool ReadAt(const void* base, const std::size_t offset, T& value) noexcept {
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

bool BoundedMotion(const Matrix& older, const Matrix& newer) noexcept {
    const float dx = newer.values[3] - older.values[3];
    const float dy = newer.values[7] - older.values[7];
    const float dz = newer.values[11] - older.values[11];
    const float maximum = Runtime::kMaximumPositionDeltaPerTick;
    return dx * dx + dy * dy + dz * dz <= maximum * maximum &&
           hands::OrientationDistance(older, newer) <= Runtime::kMaximumOrientationDeltaPerTick;
}

bool BoundedModelMotion(const Matrix& older, const Matrix& newer,
                        const std::uint64_t serialDelta) noexcept {
    const float dx = newer.values[3] - older.values[3];
    const float dy = newer.values[7] - older.values[7];
    const float dz = newer.values[11] - older.values[11];
    const float maximum =
        hands::History::kMaximumPositionDeltaPerTick * static_cast<float>(serialDelta);
    return dx * dx + dy * dy + dz * dz <= maximum * maximum &&
           hands::OrientationDistance(older, newer) <=
               hands::History::kMaximumOrientationDeltaPerTick * static_cast<float>(serialDelta);
}

} // namespace

void Runtime::Configure(const Functions& functions) noexcept {
    (void)EndRender();
    functions_ = functions;
    slots_ = {};
    slotBuckets_ = {};
    modelSlots_ = {};
    renderOwners_ = {};
    renderOwnerBuckets_ = {};
    renderRoots_ = {};
    renderRootBuckets_ = {};
    modelHistories_ = {};
    modelHistoryBuckets_ = {};
    propCaches_ = {};
    propCacheBuckets_ = {};
    for (auto& body : movedBodies_) {
        body.store(nullptr, std::memory_order_relaxed);
    }
    movedBodyCount_.store(0, std::memory_order_relaxed);
    movedBodyOverflow_.store(0, std::memory_order_relaxed);
    modelSlotCount_ = 0;
    renderOwnerCount_ = 0;
    renderRootCount_ = 0;
    renderClockSample_ = {};
    renderActive_ = false;
}

Runtime::PropCache* Runtime::FindPropCache(void* prop) noexcept {
    if (prop == nullptr)
        return nullptr;
    std::uintptr_t hash = reinterpret_cast<std::uintptr_t>(prop) >> 4U;
    hash ^= hash >> 16U;
    std::size_t bucket = hash & (kSlotBucketCount - 1);
    for (std::size_t probe = 0; probe < kSlotBucketCount; ++probe) {
        const std::size_t entry = propCacheBuckets_[bucket];
        if (entry == 0)
            return nullptr;
        PropCache& cache = propCaches_[entry - 1];
        if (cache.occupied && cache.prop == prop)
            return &cache;
        bucket = (bucket + 1) & (kSlotBucketCount - 1);
    }
    return nullptr;
}

void Runtime::RebuildPropCacheIndex() noexcept {
    propCacheBuckets_ = {};
    for (std::size_t index = 0; index < propCaches_.size(); ++index) {
        const PropCache& cache = propCaches_[index];
        if (!cache.occupied)
            continue;
        std::uintptr_t hash = reinterpret_cast<std::uintptr_t>(cache.prop) >> 4U;
        hash ^= hash >> 16U;
        std::size_t bucket = hash & (kSlotBucketCount - 1);
        for (std::size_t probe = 0; probe < kSlotBucketCount; ++probe) {
            if (propCacheBuckets_[bucket] == 0) {
                propCacheBuckets_[bucket] = index + 1;
                break;
            }
            bucket = (bucket + 1) & (kSlotBucketCount - 1);
        }
    }
}

Runtime::PropCache* Runtime::FindOrCreatePropCache(void* prop) noexcept {
    if (PropCache* existing = FindPropCache(prop); existing != nullptr) {
        return existing;
    }
    PropCache* selected = nullptr;
    for (PropCache& cache : propCaches_) {
        if (!cache.occupied) {
            selected = &cache;
            break;
        }
        if (selected == nullptr || cache.lastValidationSerial < selected->lastValidationSerial) {
            selected = &cache;
        }
    }
    if (selected == nullptr)
        return nullptr;
    *selected = {};
    selected->occupied = true;
    selected->prop = prop;
    RebuildPropCacheIndex();
    return selected;
}

void Runtime::RemovePropCache(void* prop) noexcept {
    PropCache* cache = FindPropCache(prop);
    if (cache == nullptr)
        return;
    *cache = {};
    RebuildPropCacheIndex();
}

Runtime::Slot* Runtime::FindRoot(void* root) noexcept {
    Slot* standalone = nullptr;
    for (Slot& slot : slots_) {
        if (!slot.occupied || slot.root != root) {
            continue;
        }
        if (slot.kind != Kind::standalonePhysicsBody) {
            return &slot;
        }
        standalone = &slot;
    }
    return standalone;
}

void Runtime::NotifyBodyMoved(void* body) noexcept {
    if (body == nullptr)
        return;
    const std::uint32_t index = movedBodyCount_.fetch_add(1, std::memory_order_relaxed);
    if (index >= movedBodies_.size()) {
        movedBodyOverflow_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    movedBodies_[index].store(body, std::memory_order_release);
}

bool Runtime::ReadWorld(void* mesh, Matrix& matrix) const noexcept {
    if (mesh == nullptr || functions_.getWorldMatrix == nullptr) {
        return false;
    }
    using GetWorldMatrix = Matrix*(__thiscall*)(void*);
    const auto getWorld = reinterpret_cast<GetWorldMatrix>(functions_.getWorldMatrix);
    const Matrix* world = getWorld(mesh);
    if (!IsReadable(world, sizeof(Matrix))) {
        return false;
    }
    std::memcpy(&matrix, world, sizeof(Matrix));
    return hands::IsRigid(matrix);
}

void Runtime::Untrack(void* entity) noexcept {
    bool removed = false;
    for (Slot& slot : slots_) {
        if (slot.occupied && slot.entity == entity) {
            slot = {};
            removed = true;
        }
    }
    if (removed) {
        RebuildSlotIndex();
    }
    RemovePropCache(entity);
}

Runtime::Slot* Runtime::FindSlot(void* entity, void* root) noexcept {
    static_assert((kSlotBucketCount & (kSlotBucketCount - 1)) == 0,
                  "slot bucket count must be a power of two");
    std::uintptr_t hash = reinterpret_cast<std::uintptr_t>(entity) >> 4U;
    const std::uintptr_t rootHash = reinterpret_cast<std::uintptr_t>(root) >> 4U;
    hash ^= rootHash + static_cast<std::uintptr_t>(0x9E3779B9U) + (hash << 6U) + (hash >> 2U);
    std::size_t bucket = hash & (kSlotBucketCount - 1);
    for (std::size_t probe = 0; probe < kSlotBucketCount; ++probe) {
        const std::size_t entry = slotBuckets_[bucket];
        if (entry == 0) {
            return nullptr;
        }
        Slot& slot = slots_[entry - 1];
        if (slot.occupied && slot.entity == entity && slot.root == root) {
            return &slot;
        }
        bucket = (bucket + 1) & (kSlotBucketCount - 1);
    }
    return nullptr;
}

bool Runtime::InsertSlot(Slot& slot) noexcept {
    std::uintptr_t hash = reinterpret_cast<std::uintptr_t>(slot.entity) >> 4U;
    const std::uintptr_t rootHash = reinterpret_cast<std::uintptr_t>(slot.root) >> 4U;
    hash ^= rootHash + static_cast<std::uintptr_t>(0x9E3779B9U) + (hash << 6U) + (hash >> 2U);
    std::size_t bucket = hash & (kSlotBucketCount - 1);
    const std::size_t entry = static_cast<std::size_t>(&slot - slots_.data()) + 1;
    for (std::size_t probe = 0; probe < kSlotBucketCount; ++probe) {
        if (slotBuckets_[bucket] == 0) {
            slotBuckets_[bucket] = entry;
            return true;
        }
        bucket = (bucket + 1) & (kSlotBucketCount - 1);
    }
    return false;
}

void Runtime::RebuildSlotIndex() noexcept {
    slotBuckets_ = {};
    for (Slot& slot : slots_) {
        if (slot.occupied) {
            (void)InsertSlot(slot);
        }
    }
}

Runtime::Slot* Runtime::FindOrCreate(void* entity, void* root,
                                     const std::uint64_t pendingSerial) noexcept {
    if (Slot* existing = FindSlot(entity, root); existing != nullptr) {
        return existing;
    }
    Slot* freeSlot = nullptr;
    Slot* oldest = nullptr;
    for (Slot& slot : slots_) {
        if (!slot.occupied && freeSlot == nullptr) {
            freeSlot = &slot;
        } else if (slot.occupied && !slot.staged &&
                   (oldest == nullptr || slot.stagedSerial < oldest->stagedSerial)) {
            oldest = &slot;
        }
    }
    Slot* selected = freeSlot != nullptr ? freeSlot : oldest;
    if (selected == nullptr || (selected->occupied && selected->stagedSerial == pendingSerial)) {
        return nullptr;
    }
    const bool replacing = selected->occupied;
    *selected = {};
    selected->occupied = true;
    selected->entity = entity;
    selected->root = root;
    if (replacing) {
        RebuildSlotIndex();
    } else if (!InsertSlot(*selected)) {
        *selected = {};
        return nullptr;
    }
    return selected;
}

Runtime::RenderOwner* Runtime::FindOrCreateRenderOwner(void* mesh) noexcept {
    static_assert((kRenderOwnerBucketCount & (kRenderOwnerBucketCount - 1)) == 0,
                  "render-owner bucket count must be a power of two");
    if (mesh == nullptr) {
        return nullptr;
    }
    std::uintptr_t hash = reinterpret_cast<std::uintptr_t>(mesh) >> 4U;
    hash ^= hash >> 16U;
    std::size_t bucket = hash & (kRenderOwnerBucketCount - 1);
    for (std::size_t probe = 0; probe < kRenderOwnerBucketCount; ++probe) {
        const std::size_t entry = renderOwnerBuckets_[bucket];
        if (entry == 0) {
            if (renderOwnerCount_ == renderOwners_.size()) {
                return nullptr;
            }
            RenderOwner& owner = renderOwners_[renderOwnerCount_];
            owner = {};
            owner.mesh = mesh;
            ++renderOwnerCount_;
            renderOwnerBuckets_[bucket] = renderOwnerCount_;
            return &owner;
        }
        RenderOwner& owner = renderOwners_[entry - 1];
        if (owner.mesh == mesh) {
            return &owner;
        }
        bucket = (bucket + 1) & (kRenderOwnerBucketCount - 1);
    }
    return nullptr;
}

Runtime::RenderOwner* Runtime::FindRenderOwner(void* mesh) noexcept {
    if (mesh == nullptr) {
        return nullptr;
    }
    std::uintptr_t hash = reinterpret_cast<std::uintptr_t>(mesh) >> 4U;
    hash ^= hash >> 16U;
    std::size_t bucket = hash & (kRenderOwnerBucketCount - 1);
    for (std::size_t probe = 0; probe < kRenderOwnerBucketCount; ++probe) {
        const std::size_t entry = renderOwnerBuckets_[bucket];
        if (entry == 0) {
            return nullptr;
        }
        RenderOwner& owner = renderOwners_[entry - 1];
        if (owner.mesh == mesh) {
            return &owner;
        }
        bucket = (bucket + 1) & (kRenderOwnerBucketCount - 1);
    }
    return nullptr;
}

bool Runtime::InsertRenderRoot(Slot& slot) noexcept {
    static_assert((kRenderOwnerBucketCount & (kRenderOwnerBucketCount - 1)) == 0,
                  "render-root bucket count must be a power of two");
    if (slot.root == nullptr || renderRootCount_ == renderRoots_.size()) {
        return false;
    }
    std::uintptr_t hash = reinterpret_cast<std::uintptr_t>(slot.root) >> 4U;
    hash ^= hash >> 16U;
    std::size_t bucket = hash & (kRenderOwnerBucketCount - 1);
    for (std::size_t probe = 0; probe < kRenderOwnerBucketCount; ++probe) {
        const std::size_t entry = renderRootBuckets_[bucket];
        if (entry == 0) {
            RenderRoot& renderRoot = renderRoots_[renderRootCount_];
            renderRoot.root = slot.root;
            renderRoot.slot = &slot;
            ++renderRootCount_;
            renderRootBuckets_[bucket] = renderRootCount_;
            return true;
        }
        if (renderRoots_[entry - 1].root == slot.root) {
            renderRoots_[entry - 1].slot = &slot;
            return true;
        }
        bucket = (bucket + 1) & (kRenderOwnerBucketCount - 1);
    }
    return false;
}

Runtime::Slot* Runtime::FindRenderRoot(void* root) noexcept {
    if (root == nullptr) {
        return nullptr;
    }
    std::uintptr_t hash = reinterpret_cast<std::uintptr_t>(root) >> 4U;
    hash ^= hash >> 16U;
    std::size_t bucket = hash & (kRenderOwnerBucketCount - 1);
    for (std::size_t probe = 0; probe < kRenderOwnerBucketCount; ++probe) {
        const std::size_t entry = renderRootBuckets_[bucket];
        if (entry == 0) {
            return nullptr;
        }
        RenderRoot& renderRoot = renderRoots_[entry - 1];
        if (renderRoot.root == root) {
            return renderRoot.slot;
        }
        bucket = (bucket + 1) & (kRenderOwnerBucketCount - 1);
    }
    return nullptr;
}

Runtime::Slot* Runtime::ResolveModelBodySlot(void* subMesh, void* owner, bool& matchedDirectParent,
                                             bool& matchedPropParent,
                                             bool& propParentMiss) noexcept {
    matchedDirectParent = false;
    matchedPropParent = false;
    propParentMiss = false;

    RenderOwner* renderOwner = FindRenderOwner(owner);
    Slot* entitySlot = renderOwner != nullptr ? renderOwner->readyEnemy : nullptr;
    if (entitySlot == nullptr) {
        void* parent = ReadMappedField<void*>(subMesh, kEntity3DEntityParentOffset);
        entitySlot = FindRenderRoot(parent);
        matchedDirectParent = entitySlot != nullptr;
    }
    if (entitySlot == nullptr && renderOwner != nullptr && renderOwner->propRootCount == 1) {
        entitySlot = renderOwner->soleReadyPropRoot;
    }
    if (entitySlot == nullptr && renderOwner != nullptr && renderOwner->propRootCount > 1 &&
        renderOwner->hasReadyProp) {
        if (functions_.getEntityParent == nullptr) {
            propParentMiss = true;
            return nullptr;
        }
        using GetEntityParent = void*(__thiscall*)(void*);
        const auto getParent = reinterpret_cast<GetEntityParent>(functions_.getEntityParent);
        void* child = subMesh;
        for (std::size_t depth = 0; depth < kMaximumParentDepth; ++depth) {
            void* parent = getParent(child);
            if (parent == nullptr || parent == child) {
                break;
            }
            for (Slot& slot : slots_) {
                if (slot.renderReady && slot.kind == Kind::propObject && slot.mesh == owner &&
                    slot.root == parent) {
                    entitySlot = &slot;
                    matchedPropParent = true;
                    break;
                }
            }
            if (entitySlot != nullptr) {
                break;
            }
            child = parent;
        }
        propParentMiss = entitySlot == nullptr;
    }
    return entitySlot;
}

Runtime::ModelHistorySlot* Runtime::FindModelHistory(void* subMesh) noexcept {
    if (subMesh == nullptr)
        return nullptr;
    std::uintptr_t hash = reinterpret_cast<std::uintptr_t>(subMesh) >> 4U;
    hash ^= hash >> 16U;
    std::size_t bucket = hash & (kRenderOwnerBucketCount - 1);
    for (std::size_t probe = 0; probe < kRenderOwnerBucketCount; ++probe) {
        const std::size_t entry = modelHistoryBuckets_[bucket];
        if (entry == 0)
            return nullptr;
        ModelHistorySlot& slot = modelHistories_[entry - 1];
        if (slot.occupied && slot.subMesh == subMesh)
            return &slot;
        bucket = (bucket + 1) & (kRenderOwnerBucketCount - 1);
    }
    return nullptr;
}

bool Runtime::InsertModelHistory(ModelHistorySlot& slot) noexcept {
    std::uintptr_t hash = reinterpret_cast<std::uintptr_t>(slot.subMesh) >> 4U;
    hash ^= hash >> 16U;
    std::size_t bucket = hash & (kRenderOwnerBucketCount - 1);
    const std::size_t entry = static_cast<std::size_t>(&slot - modelHistories_.data()) + 1;
    for (std::size_t probe = 0; probe < kRenderOwnerBucketCount; ++probe) {
        if (modelHistoryBuckets_[bucket] == 0) {
            modelHistoryBuckets_[bucket] = entry;
            return true;
        }
        bucket = (bucket + 1) & (kRenderOwnerBucketCount - 1);
    }
    return false;
}

void Runtime::RebuildModelHistoryIndex() noexcept {
    modelHistoryBuckets_ = {};
    for (ModelHistorySlot& slot : modelHistories_) {
        if (slot.occupied)
            (void)InsertModelHistory(slot);
    }
}

Runtime::ModelHistorySlot* Runtime::FindOrCreateModelHistory(void* subMesh, void* owner) noexcept {
    if (ModelHistorySlot* existing = FindModelHistory(subMesh); existing != nullptr) {
        if (existing->owner != owner) {
            existing->owner = owner;
            existing->snapshots = {};
            existing->snapshotCount = 0;
            existing->generation = 0;
            existing->hasGeneration = false;
        }
        return existing;
    }
    ModelHistorySlot* selected = nullptr;
    for (ModelHistorySlot& slot : modelHistories_) {
        if (!slot.occupied) {
            selected = &slot;
            break;
        }
        if (selected == nullptr || slot.lastSeenSerial < selected->lastSeenSerial) {
            selected = &slot;
        }
    }
    if (selected == nullptr)
        return nullptr;
    const bool replacing = selected->occupied;
    *selected = {};
    selected->occupied = true;
    selected->subMesh = subMesh;
    selected->owner = owner;
    if (replacing) {
        RebuildModelHistoryIndex();
    } else if (!InsertModelHistory(*selected)) {
        *selected = {};
        return nullptr;
    }
    return selected;
}

hands::PublishKind Runtime::PublishModelHistory(ModelHistorySlot& slot, const std::uint64_t serial,
                                                 const std::uint64_t clockGeneration,
                                                 const Matrix& matrix) noexcept {
    const auto resetTo = [&](const hands::PublishKind kind) noexcept {
        slot.snapshots = {};
        slot.snapshots[0] = {serial, matrix};
        slot.snapshotCount = 1;
        slot.generation = clockGeneration;
        slot.hasGeneration = true;
        return kind;
    };
    if (!hands::IsRigid(matrix)) {
        slot.snapshots = {};
        slot.snapshotCount = 0;
        slot.generation = 0;
        slot.hasGeneration = false;
        return hands::PublishKind::invalid;
    }
    if (!slot.hasGeneration || slot.generation != clockGeneration) {
        return resetTo(hands::PublishKind::resetGeneration);
    }
    if (slot.snapshotCount != 0) {
        const ModelHistorySlot::Snapshot& latest = slot.snapshots[slot.snapshotCount - 1];
        if (serial < latest.serial) {
            return resetTo(hands::PublishKind::resetNonMonotonic);
        }
        if (serial == latest.serial) {
            if (hands::NearlyEqual(latest.matrix, matrix)) {
                return hands::PublishKind::unchanged;
            }
            return resetTo(hands::PublishKind::resetSameTickMutation);
        }
        const std::uint64_t delta = serial - latest.serial;
        if (delta > clock::PresentationClock::kMaximumBridgedSerialDelta ||
            !BoundedModelMotion(latest.matrix, matrix, delta)) {
            return resetTo(hands::PublishKind::resetDiscontinuity);
        }

        // A stationary articulated submesh needs one validated endpoint, not
        // a presentation history. Keep replacing that dormant endpoint until
        // an actual matrix change creates a previous/current pair. Once an
        // active pair settles, collapse it back to one endpoint immediately.
        // This preserves the exact drawer/hinge path while preventing every
        // submesh of every multi-body prop from becoming render-active.
        if (hands::NearlyEqual(latest.matrix, matrix)) {
            slot.snapshots = {};
            slot.snapshots[0] = {serial, matrix};
            slot.snapshotCount = 1;
            return hands::PublishKind::unchanged;
        }
    }
    if (slot.snapshotCount == slot.snapshots.size()) {
        for (std::size_t index = 1; index < slot.snapshots.size(); ++index) {
            slot.snapshots[index - 1] = slot.snapshots[index];
        }
        --slot.snapshotCount;
    }
    slot.snapshots[slot.snapshotCount++] = {serial, matrix};
    return hands::PublishKind::appended;
}

hands::SampleResult Runtime::EvaluateModelHistory(const ModelHistorySlot& slot,
                                                  const clock::Sample& clockSample) const noexcept {
    if (clockSample.kind != clock::SampleKind::interpolated ||
        clockSample.olderSerial == clockSample.newerSerial) {
        return {hands::SampleKind::clockUnavailable, {}};
    }
    if (!slot.hasGeneration || slot.generation != clockSample.generation) {
        return {hands::SampleKind::generationMismatch, {}};
    }
    const ModelHistorySlot::Snapshot* older = nullptr;
    const ModelHistorySlot::Snapshot* newer = nullptr;
    for (std::size_t index = 0; index < slot.snapshotCount; ++index) {
        const ModelHistorySlot::Snapshot& candidate = slot.snapshots[index];
        if (candidate.serial == clockSample.olderSerial)
            older = &candidate;
        if (candidate.serial == clockSample.newerSerial)
            newer = &candidate;
    }
    if (older == nullptr || newer == nullptr) {
        return {hands::SampleKind::snapshotsMissing, {}};
    }
    Matrix interpolated{};
    if (!hands::Interpolate(older->matrix, newer->matrix, clockSample.alpha, interpolated)) {
        return {hands::SampleKind::invalid, {}};
    }
    return {hands::SampleKind::applied, interpolated};
}

bool Runtime::ModelHistoryPairMoved(const ModelHistorySlot& slot,
                                    const clock::Sample& clockSample) const noexcept {
    const ModelHistorySlot::Snapshot* older = nullptr;
    const ModelHistorySlot::Snapshot* newer = nullptr;
    for (std::size_t index = 0; index < slot.snapshotCount; ++index) {
        const ModelHistorySlot::Snapshot& candidate = slot.snapshots[index];
        if (candidate.serial == clockSample.olderSerial) {
            older = &candidate;
        }
        if (candidate.serial == clockSample.newerSerial) {
            newer = &candidate;
        }
    }
    return older != nullptr && newer != nullptr &&
           !hands::NearlyEqual(older->matrix, newer->matrix);
}

StageResult Runtime::StageProp(void* prop, const std::uint64_t pendingSerial,
                               PropStageInfo* stageInfo) noexcept {
    if (stageInfo != nullptr) *stageInfo = {};
    if (functions_.getBodyEnabled == nullptr) {
        Untrack(prop);
        return StageResult::invalid;
    }
    if (prop == nullptr) {
        return StageResult::invalid;
    }
    const auto* propBytes = static_cast<const std::uint8_t*>(prop);
    std::int32_t fastId = 0;
    std::memcpy(&fastId, propBytes + kEntityIdOffset, sizeof(fastId));
    if (PropCache* cache = FindPropCache(prop);
        cache != nullptr && cache->id == fastId && cache->allSleeping &&
        !cache->captureModelsWhileSleeping && pendingSerial < cache->nextValidationSerial) {
        return StageResult::cachedSleepingProp;
    }
    constexpr std::size_t propSpanEnd = kPropTypeOffset + sizeof(std::int32_t);
    if (!IsReadable(propBytes + kEntityIdOffset, propSpanEnd - kEntityIdOffset)) {
        Untrack(prop);
        return StageResult::invalid;
    }
    std::int32_t type = -1;
    std::uint8_t staticPhysics = 0;
    std::memcpy(&type, propBytes + kPropTypeOffset, sizeof(type));
    std::memcpy(&staticPhysics, propBytes + kPropStaticPhysicsOffset, sizeof(staticPhysics));
    if (staticPhysics > 1U) {
        Untrack(prop);
        return StageResult::invalid;
    }
    // Every shipped Lux prop subtype derives from iLuxProp and therefore uses
    // these same mesh/body-vector fields. Keep static props authoritative, but
    // admit every known physics-backed subtype instead of only Object (0):
    // drawers are commonly MoveObject (6), while doors/chests use other values.
    if (type < kFirstPropType || type >= kPropTypeLastEnum || staticPhysics != 0U) {
        Untrack(prop);
        return StageResult::ineligible;
    }
    void* bodiesBegin = nullptr;
    void* bodiesEnd = nullptr;
    std::memcpy(&bodiesBegin, propBytes + kPropBodiesBeginOffset, sizeof(bodiesBegin));
    std::memcpy(&bodiesEnd, propBytes + kPropBodiesEndOffset, sizeof(bodiesEnd));
    if (bodiesBegin == nullptr || bodiesEnd == nullptr) {
        Untrack(prop);
        return StageResult::invalid;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(bodiesBegin);
    const auto end = reinterpret_cast<std::uintptr_t>(bodiesEnd);
    if (end <= begin || (end - begin) % sizeof(void*) != 0) {
        Untrack(prop);
        return StageResult::ineligible;
    }
    const std::size_t bodyCount = (end - begin) / sizeof(void*);
    if (bodyCount == 0 || bodyCount > kMaximumBodiesPerProp ||
        !IsReadable(bodiesBegin, bodyCount * sizeof(void*))) {
        Untrack(prop);
        return StageResult::ineligible;
    }

    void* mesh = nullptr;
    void* parentNode = nullptr;
    std::int32_t id = 0;
    std::uint8_t active = 0;
    std::memcpy(&mesh, propBytes + kPropMeshEntityOffset, sizeof(mesh));
    std::memcpy(&id, propBytes + kEntityIdOffset, sizeof(id));
    if (mesh == nullptr) {
        Untrack(prop);
        return StageResult::invalid;
    }
    const auto* meshBytes = static_cast<const std::uint8_t*>(mesh);
    if (!IsReadable(meshBytes + kEntity3DParentNodeOffset,
                    kEntity3DActiveOffset + sizeof(active) - kEntity3DParentNodeOffset)) {
        Untrack(prop);
        return StageResult::invalid;
    }
    std::memcpy(&active, meshBytes + kEntity3DActiveOffset, sizeof(active));
    std::memcpy(&parentNode, meshBytes + kEntity3DParentNodeOffset, sizeof(parentNode));
    if (active > 1U || active == 0U) {
        Untrack(prop);
        return StageResult::invalid;
    }

    std::array<void*, kMaximumBodiesPerProp> roots{};
    std::memcpy(roots.data(), bodiesBegin, bodyCount * sizeof(void*));

    bool allSleeping = true;
    for (std::size_t index = 0; index < bodyCount; ++index) {
        void* root = roots[index];
        if (root == nullptr) {
            Untrack(prop);
            return StageResult::invalid;
        }
        bool duplicate = false;
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (roots[previous] == root) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        using GetBodyEnabled = bool(__thiscall*)(void*);
        const auto getBodyEnabled = reinterpret_cast<GetBodyEnabled>(functions_.getBodyEnabled);
        const bool bodyEnabled = getBodyEnabled(root);
        allSleeping = allSleeping && !bodyEnabled;
        if (!StageRoot(prop, mesh, root, parentNode, id, Kind::propObject, bodyEnabled, bodyCount,
                       pendingSerial)) {
            Untrack(prop);
            return StageResult::capacityExhausted;
        }
    }

    // Bodies can be created or destroyed during an object's lifetime. Retire
    // histories that were not present in the current, fully validated vector.
    bool retiredRoot = false;
    for (Slot& slot : slots_) {
        if (slot.occupied && slot.entity == prop && slot.kind == Kind::propObject &&
            slot.stagedSerial != pendingSerial) {
            slot = {};
            retiredRoot = true;
        }
    }
    if (retiredRoot) {
        RebuildSlotIndex();
    }
    if (PropCache* cache = FindOrCreatePropCache(prop); cache != nullptr) {
        const bool wasSleeping = cache->allSleeping;
        cache->id = id;
        cache->lastValidationSerial = pendingSerial;
        cache->allSleeping = allSleeping;
        cache->captureModelsWhileSleeping = bodyCount > 1;
        if (!allSleeping) {
            cache->nextValidationSerial = 0;
        } else if (!wasSleeping || cache->nextValidationSerial == 0) {
            std::uintptr_t phase = reinterpret_cast<std::uintptr_t>(prop) >> 4U;
            phase ^= phase >> 16U;
            cache->nextValidationSerial = pendingSerial + 30 + phase % kSleepingRevalidationTicks;
        } else {
            do {
                cache->nextValidationSerial += kSleepingRevalidationTicks;
            } while (cache->nextValidationSerial <= pendingSerial);
        }
    }
    if (stageInfo != nullptr) {
        stageInfo->mesh = mesh;
        stageInfo->id = id;
        stageInfo->bodyCount = bodyCount;
        stageInfo->allSleeping = allSleeping;
        stageInfo->valid = true;
    }
    return StageResult::stagedProp;
}

StageResult Runtime::StageEnemy(void* enemy, const std::uint64_t pendingSerial) noexcept {
    std::int32_t type = -1;
    if (!ReadAt(enemy, kEnemyTypeOffset, type)) {
        Untrack(enemy);
        return StageResult::invalid;
    }
    if (type != kEnemyTypeGrunt) {
        Untrack(enemy);
        return StageResult::ineligible;
    }
    return StageEnemyRoot(enemy, pendingSerial);
}

StageResult Runtime::StageEnemyRoot(void* entity, const std::uint64_t pendingSerial) noexcept {
    void* mesh = nullptr;
    void* parentNode = nullptr;
    std::int32_t id = 0;
    std::uint8_t active = 0;
    if (!ReadAt(entity, kEnemyMeshEntityOffset, mesh) || mesh == nullptr ||
        !ReadAt(entity, kEntityIdOffset, id) || !ReadAt(mesh, kEntity3DActiveOffset, active) ||
        active > 1U || active == 0U || !ReadAt(mesh, kEntity3DParentNodeOffset, parentNode)) {
        Untrack(entity);
        return StageResult::invalid;
    }
    if (!StageRoot(entity, mesh, mesh, parentNode, id, Kind::enemyGrunt, true, 1, pendingSerial)) {
        return StageResult::capacityExhausted;
    }
    return StageResult::stagedGrunt;
}

bool Runtime::StageRoot(void* entity, void* mesh, void* root, void* parentNode,
                        const std::int32_t id, const Kind kind, const bool bodyEnabled,
                        const std::size_t bodyCount, const std::uint64_t pendingSerial) noexcept {
    // A Newton callback can reveal a body before its owning Lux prop gets its
    // update. Once the stronger game-entity identity is available, discard the
    // provisional standalone slot so a root can never have competing render
    // histories.
    if (kind != Kind::standalonePhysicsBody) {
        bool removedStandalone = false;
        for (Slot& candidate : slots_) {
            if (candidate.occupied && candidate.kind == Kind::standalonePhysicsBody &&
                candidate.root == root) {
                candidate = {};
                removedStandalone = true;
            }
        }
        if (removedStandalone) {
            RebuildSlotIndex();
        }
    }
    Slot* slot = FindOrCreate(entity, root, pendingSerial);
    if (slot == nullptr) {
        return false;
    }
    bool wasBodyEnabled = slot->bodyEnabled;
    if (slot->mesh != nullptr && (slot->mesh != mesh || slot->root != root || slot->id != id ||
                                  slot->kind != kind || slot->parentNode != parentNode)) {
        slot->history.Reset();
        slot->hasLastWorld = false;
        slot->committedSerial = 0;
        wasBodyEnabled = false;
    }
    slot->entity = entity;
    slot->mesh = mesh;
    slot->root = root;
    slot->parentNode = parentNode;
    slot->id = id;
    slot->kind = kind;
    slot->captureFinal = kind == Kind::propObject && !bodyEnabled && wasBodyEnabled;
    slot->bodyCount = static_cast<std::uint16_t>(bodyCount);
    slot->bodyEnabled = bodyEnabled;
    slot->stagedSerial = pendingSerial;
    slot->staged = true;
    return true;
}

Runtime::Slot* Runtime::StageStandaloneBody(void* body,
                                            const std::uint64_t pendingSerial) noexcept {
    std::uint8_t active = 0;
    std::int32_t id = 0;
    std::uint32_t childCount = 0;
    if (!ReadAt(body, kEntity3DActiveOffset, active) || active != 1U ||
        !ReadAt(body, kEntityIdOffset, id) ||
        !ReadAt(body, kEntity3DChildrenCountOffset, childCount) || childCount == 0U ||
        childCount > kMaximumSubMeshesPerProp) {
        return nullptr;
    }
    if (!StageRoot(body, nullptr, body, nullptr, id, Kind::standalonePhysicsBody, true, 1,
                   pendingSerial)) {
        return nullptr;
    }
    return FindSlot(body, body);
}

BatchResult Runtime::CommitCompletedTick(const std::uint64_t tickSerial,
                                         const std::uint64_t clockGeneration) noexcept {
    BatchResult result{};
    const std::uint32_t notified = movedBodyCount_.exchange(0, std::memory_order_acq_rel);
    const std::uint32_t boundedNotified =
        notified < movedBodies_.size() ? notified : static_cast<std::uint32_t>(movedBodies_.size());
    result.movedCallbacks = boundedNotified;
    result.movedOverflow = movedBodyOverflow_.exchange(0, std::memory_order_relaxed);
    for (std::uint32_t index = 0; index < boundedNotified; ++index) {
        void* body = movedBodies_[index].exchange(nullptr, std::memory_order_acquire);
        Slot* slot = FindRoot(body);
        if (slot == nullptr) {
            slot = StageStandaloneBody(body, tickSerial);
        }
        if (slot == nullptr) {
            continue;
        }
        slot->bodyEnabled = true;
        slot->captureFinal = false;
        slot->stagedSerial = tickSerial;
        slot->staged = true;
        if (slot->kind == Kind::standalonePhysicsBody) {
            ++result.movedStandalone;
        } else if (PropCache* cache = FindPropCache(slot->entity); cache != nullptr) {
            cache->allSleeping = false;
            ++result.movedMatched;
        } else {
            ++result.movedMatched;
        }
    }
    for (Slot& slot : slots_) {
        slot.renderReady = false;
        if (!slot.occupied || !slot.staged) {
            continue;
        }
        if (slot.stagedSerial < tickSerial) {
            slot.staged = false;
            slot.history.Reset();
            slot.hasLastWorld = false;
            slot.committedSerial = 0;
            slot.moving = false;
            ++result.reset;
            continue;
        }
        if (slot.stagedSerial != tickSerial) {
            continue;
        }
        slot.staged = false;
        if (slot.kind == Kind::propObject && !slot.bodyEnabled && !slot.captureFinal) {
            slot.moving = false;
            ++result.sleeping;
            continue;
        }
        slot.captureFinal = false;
        Matrix completedWorld{};
        if (!ReadWorld(slot.root, completedWorld)) {
            slot.history.Reset();
            slot.hasLastWorld = false;
            slot.committedSerial = 0;
            slot.moving = false;
            ++result.invalid;
            continue;
        }
        if ((slot.committedSerial != 0 && slot.committedSerial + 1 != tickSerial) ||
            (slot.hasLastWorld && !BoundedMotion(slot.lastWorld, completedWorld))) {
            slot.history.Reset();
            slot.hasLastWorld = false;
            slot.moving = false;
            ++result.reset;
        }

        const bool changed =
            slot.hasLastWorld && !hands::NearlyEqual(slot.lastWorld, completedWorld);

        const hands::PublishKind published =
            slot.history.Publish(tickSerial, clockGeneration, completedWorld);
        switch (published) {
        case hands::PublishKind::appended:
        case hands::PublishKind::unchanged:
            ++result.captured;
            break;
        case hands::PublishKind::resetGeneration:
        case hands::PublishKind::resetNonMonotonic:
        case hands::PublishKind::resetSameTickMutation:
        case hands::PublishKind::resetDiscontinuity:
            ++result.reset;
            break;
        case hands::PublishKind::invalid:
            ++result.invalid;
            break;
        }
        slot.lastWorld = completedWorld;
        slot.hasLastWorld = published != hands::PublishKind::invalid;
        slot.moving = changed && published != hands::PublishKind::invalid;
        slot.committedSerial = tickSerial;
    }

    // The GOG build does not expose the source-level body-child relationship
    // for every articulated renderable at the model hook. cMeshEntity's exact
    // submesh vector is authoritative and stable, however. Capture those final
    // world matrices here, after global PostUpdate has completed the tick, so
    // rendering never has to infer ownership from a transformed child.
    std::array<void*, kMaximumEntities> capturedMeshes{};
    std::size_t capturedMeshCount = 0;
    for (const Slot& slot : slots_) {
        if (!slot.occupied || slot.kind != Kind::propObject || slot.bodyCount <= 1 ||
            slot.stagedSerial != tickSerial || slot.mesh == nullptr) {
            continue;
        }
        bool duplicate = false;
        for (std::size_t index = 0; index < capturedMeshCount; ++index) {
            if (capturedMeshes[index] == slot.mesh) {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;
        if (capturedMeshCount == capturedMeshes.size()) {
            ++result.modelCapacity;
            break;
        }
        capturedMeshes[capturedMeshCount++] = slot.mesh;

        void* subMeshesBegin = nullptr;
        void* subMeshesEnd = nullptr;
        if (!ReadAt(slot.mesh, kMeshSubMeshesBeginOffset, subMeshesBegin) ||
            !ReadAt(slot.mesh, kMeshSubMeshesEndOffset, subMeshesEnd) ||
            subMeshesBegin == nullptr || subMeshesEnd == nullptr) {
            ++result.modelInvalid;
            continue;
        }
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(subMeshesBegin);
        const std::uintptr_t end = reinterpret_cast<std::uintptr_t>(subMeshesEnd);
        if (end <= begin || (end - begin) % sizeof(void*) != 0) {
            ++result.modelInvalid;
            continue;
        }
        const std::size_t subMeshCount = (end - begin) / sizeof(void*);
        if (subMeshCount == 0 || subMeshCount > kMaximumSubMeshesPerProp ||
            !IsReadable(subMeshesBegin, subMeshCount * sizeof(void*))) {
            ++result.modelInvalid;
            continue;
        }
        std::array<void*, kMaximumSubMeshesPerProp> subMeshes{};
        std::memcpy(subMeshes.data(), subMeshesBegin, subMeshCount * sizeof(void*));
        for (std::size_t index = 0; index < subMeshCount; ++index) {
            void* subMesh = subMeshes[index];
            ++result.modelCandidates;
            Matrix completedModel{};
            if (subMesh == nullptr || !ReadWorld(subMesh, completedModel)) {
                ++result.modelInvalid;
                continue;
            }
            ModelHistorySlot* history = FindOrCreateModelHistory(subMesh, slot.mesh);
            if (history == nullptr) {
                ++result.modelCapacity;
                continue;
            }
            history->lastSeenSerial = tickSerial;
            const hands::PublishKind published =
                PublishModelHistory(*history, tickSerial, clockGeneration, completedModel);
            if (published == hands::PublishKind::invalid) {
                ++result.modelInvalid;
            } else {
                ++result.modelCaptured;
            }
        }
    }
    return result;
}

BatchResult Runtime::BeginRender(const clock::Sample& clockSample) noexcept {
    BatchResult result{};
    if (renderActive_) {
        result.modelRestoreFailed = EndRender();
    }
    modelSlotCount_ = 0;
    renderOwners_ = {};
    renderOwnerBuckets_ = {};
    renderOwnerCount_ = 0;
    renderRoots_ = {};
    renderRootBuckets_ = {};
    renderRootCount_ = 0;
    renderActive_ = false;
    renderClockSample_ = clockSample;
    for (Slot& slot : slots_) {
        slot.renderReady = false;
        if (!slot.occupied || !slot.moving) {
            continue;
        }
        const hands::SampleResult sampled = slot.history.Evaluate(clockSample);
        if (sampled.kind == hands::SampleKind::snapshotsMissing ||
            sampled.kind == hands::SampleKind::generationMismatch) {
            ++result.pairMiss;
            continue;
        }
        if (sampled.kind == hands::SampleKind::clockUnavailable) {
            continue;
        }
        if (sampled.kind != hands::SampleKind::applied) {
            ++result.invalid;
            continue;
        }

        Matrix inverse{};
        // lastWorld is the validated authoritative endpoint captured at the
        // completed-tick boundary. Re-reading the engine matrix once per
        // presentation frame added thousands of virtual calls per second in
        // busy scenes and cannot improve the fixed-tick endpoint.
        if (!slot.hasLastWorld || !hands::InverseRigid(slot.lastWorld, inverse) ||
            !hands::Multiply(sampled.matrix, inverse, slot.rootDelta)) {
            ++result.applyFailed;
            continue;
        }
        slot.renderReady = true;
        renderActive_ = true;
        ++result.ready;
    }

    // Build a compact, allocation-free owner index once per render. The model
    // hook is called for every visible submesh, so it must not scan all 512
    // histories just to reject unrelated geometry.
    for (Slot& slot : slots_) {
        if (!slot.renderReady) {
            continue;
        }
        if (slot.kind == Kind::standalonePhysicsBody) {
            if (!InsertRenderRoot(slot)) {
                slot.renderReady = false;
                ++result.applyFailed;
            }
            continue;
        }
        RenderOwner* owner = FindOrCreateRenderOwner(slot.mesh);
        if (owner == nullptr) {
            slot.renderReady = false;
            ++result.applyFailed;
            continue;
        }
        if (slot.kind == Kind::enemyGrunt) {
            owner->readyEnemy = &slot;
        } else {
            owner->hasReadyProp = true;
            owner->soleReadyPropRoot = &slot;
            if (slot.bodyCount > owner->propBodyCount) {
                owner->propBodyCount = slot.bodyCount;
            }
            if (!InsertRenderRoot(slot)) {
                slot.renderReady = false;
                ++result.applyFailed;
            }
        }
    }
    for (Slot& slot : slots_) {
        if (!slot.occupied || slot.kind != Kind::propObject) {
            continue;
        }
        RenderOwner* owner = FindRenderOwner(slot.mesh);
        if (owner != nullptr && owner->hasReadyProp) {
            ++owner->propRootCount;
        }
    }

    // Final-model histories have stronger provenance than the temporary
    // moving-body owner index: each identity was enumerated directly from a
    // confirmed multi-body dynamic prop at the completed-tick boundary. HPL's
    // render lists cache a pointer to the submesh world matrix and do not call
    // GetModelMatrix again on every presentation. For a moving history with no
    // validated body-parent route, override that cache for the render scope and
    // restore it byte-for-byte in EndRender.
    using GetWorldMatrix = Matrix*(__thiscall*)(void*);
    const auto getWorld = reinterpret_cast<GetWorldMatrix>(functions_.getWorldMatrix);
    for (ModelHistorySlot& history : modelHistories_) {
        if (!history.occupied) {
            continue;
        }
        if (!ModelHistoryPairMoved(history, clockSample)) {
            continue;
        }
        const hands::SampleResult sampled = EvaluateModelHistory(history, clockSample);
        if (sampled.kind != hands::SampleKind::applied) {
            continue;
        }
        renderActive_ = true;
        ++result.modelReady;
        ++result.modelMovingReady;

        bool matchedDirectParent = false;
        bool matchedPropParent = false;
        bool propParentMiss = false;
        Slot* bodySlot = ResolveModelBodySlot(history.subMesh, history.owner,
                                              matchedDirectParent,
                                              matchedPropParent, propParentMiss);
        const RenderOwner* historyOwner = FindRenderOwner(history.owner);
        const bool exactMultiBodyHistory = historyOwner != nullptr &&
                                           historyOwner->propRootCount > 1;
        // The final-model cache is the live-validated path for articulated
        // multi-body props: HPL can retain its matrix pointer across render
        // submissions and therefore bypass GetModelMatrix. Keep that exact
        // completed-tick history even when the corrected entity-parent map can
        // now identify a body. For one-body props and enemies, the cheaper
        // validated root/body path retains priority.
        if (bodySlot != nullptr && !exactMultiBodyHistory) {
            continue;
        }
        Matrix* world = getWorld != nullptr ? getWorld(history.subMesh) : nullptr;
        if (!IsWritable(world, sizeof(Matrix)) || !hands::IsRigid(*world)) {
            ++result.modelCacheInvalid;
            continue;
        }
        history.restore = *world;
        *world = sampled.matrix;
        history.overrideBuffer = world;
        history.overridden = true;
        ++result.modelCacheApplied;
    }

    if (!renderActive_) {
        return result;
    }

    for (std::size_t index = 0; index < renderOwnerCount_; ++index) {
        const RenderOwner& owner = renderOwners_[index];
        if (owner.hasReadyProp && owner.propBodyCount > 1) {
            ++result.multiBodyOwners;
        }
    }
    return result;
}

OverrideResult Runtime::OverrideModelMatrix(void* subMesh, const Matrix* original,
                                            const Matrix*& result,
                                            ModelHistoryGate* historyGate) noexcept {
    result = original;
    if (historyGate != nullptr) {
        *historyGate = ModelHistoryGate::notFound;
    }
    if (!renderActive_ || subMesh == nullptr) {
        return OverrideResult::notWorld;
    }
    ModelHistorySlot* modelHistory = FindModelHistory(subMesh);
    if (modelHistory != nullptr && modelHistory->overridden &&
        modelHistory->overrideBuffer != nullptr) {
        result = modelHistory->overrideBuffer;
        if (historyGate != nullptr) {
            *historyGate = hands::NearlyEqual(*modelHistory->overrideBuffer, modelHistory->restore)
                               ? ModelHistoryGate::appliedMovingUnchanged
                               : ModelHistoryGate::appliedMovingChanged;
        }
        return OverrideResult::appliedPropModelHistory;
    }
    // cSubMeshEntity::GetModelMatrix supplies a live object and this offset is
    // part of the executable compatibility signature gate. Reading the mapped field
    // directly avoids repeated VirtualQuery calls for every visible submesh
    // while retaining hash rejection and matched-object validation below.
    void* owner = ReadMappedField<void*>(subMesh, kSubMeshOwnerOffset);
    RenderOwner* renderOwner = FindRenderOwner(owner);
    bool matchedPropParent = false;
    bool matchedDirectParent = false;
    bool propParentMiss = false;
    Slot* entitySlot = ResolveModelBodySlot(subMesh, owner, matchedDirectParent, matchedPropParent,
                                            propParentMiss);
    if (renderOwner == nullptr && entitySlot == nullptr && modelHistory == nullptr) {
        return OverrideResult::notWorld;
    }
    // Prefer an exact direct or nested body parent when the runtime exposes
    // one. Otherwise use the final submesh matrix captured at the completed-
    // tick boundary. The history is keyed by a pointer enumerated from the
    // confirmed prop's own cMeshEntity vector, so it remains safe even when
    // the callback's owner or parent fields do not lead back to the Lux prop.
    if (modelHistory != nullptr && historyGate != nullptr && entitySlot != nullptr) {
        *historyGate = ModelHistoryGate::bodyPriority;
    }
    if (entitySlot == nullptr && modelHistory != nullptr &&
        ModelHistoryPairMoved(*modelHistory, renderClockSample_)) {
        const hands::SampleResult sampled = EvaluateModelHistory(*modelHistory, renderClockSample_);
        if (sampled.kind == hands::SampleKind::applied) {
            ModelSlot* output = nullptr;
            for (std::size_t index = 0; index < modelSlotCount_; ++index) {
                if (modelSlots_[index].subMesh == subMesh) {
                    output = &modelSlots_[index];
                    break;
                }
            }
            if (output == nullptr) {
                if (modelSlotCount_ == modelSlots_.size()) {
                    if (historyGate != nullptr) {
                        *historyGate = ModelHistoryGate::slotsExhausted;
                    }
                    return OverrideResult::slotsExhausted;
                }
                output = &modelSlots_[modelSlotCount_++];
                output->subMesh = subMesh;
            }
            output->matrix = sampled.matrix;
            result = &output->matrix;
            if (historyGate != nullptr) {
                if (!ModelHistoryPairMoved(*modelHistory, renderClockSample_)) {
                    *historyGate = ModelHistoryGate::applied;
                } else {
                    Matrix baseline{};
                    if (original != nullptr) {
                        baseline = *original;
                    }
                    *historyGate = hands::NearlyEqual(sampled.matrix, baseline)
                                       ? ModelHistoryGate::appliedMovingUnchanged
                                       : ModelHistoryGate::appliedMovingChanged;
                }
            }
            return OverrideResult::appliedPropModelHistory;
        }
        if (historyGate != nullptr) {
            switch (sampled.kind) {
            case hands::SampleKind::clockUnavailable:
                *historyGate = ModelHistoryGate::clockUnavailable;
                break;
            case hands::SampleKind::generationMismatch:
                *historyGate = ModelHistoryGate::generationMismatch;
                break;
            case hands::SampleKind::snapshotsMissing:
                *historyGate = ModelHistoryGate::snapshotsMissing;
                break;
            case hands::SampleKind::invalid:
                *historyGate = ModelHistoryGate::sampleInvalid;
                break;
            case hands::SampleKind::applied:
                break;
            }
        }
    }
    if (entitySlot == nullptr) {
        return propParentMiss ? OverrideResult::propParentMismatch : OverrideResult::notWorld;
    }
    const bool nullPropModel =
        original == nullptr && (entitySlot->kind == Kind::propObject ||
                                entitySlot->kind == Kind::standalonePhysicsBody);
    if (original != nullptr && !IsReadable(original, sizeof(Matrix))) {
        return OverrideResult::invalid;
    }
    if (original == nullptr && !nullPropModel) {
        return OverrideResult::invalid;
    }

    ModelSlot* output = nullptr;
    for (std::size_t index = 0; index < modelSlotCount_; ++index) {
        if (modelSlots_[index].subMesh == subMesh) {
            output = &modelSlots_[index];
            break;
        }
    }
    if (output == nullptr) {
        if (modelSlotCount_ == modelSlots_.size()) {
            return OverrideResult::slotsExhausted;
        }
        output = &modelSlots_[modelSlotCount_++];
        output->subMesh = subMesh;
    }
    // HPL2's cSubMeshEntity::GetModelMatrix deliberately returns null for a
    // static identity model matrix. Some physics-driven entity submeshes keep
    // that cached representation even though an attached body supplies their
    // per-tick movement. Preserve HPL's null-as-identity semantics, then apply
    // only the sampled body's render-space correction.
    const Matrix identity = hands::Identity();
    const Matrix& source = nullPropModel ? identity : *original;
    if (!hands::Multiply(entitySlot->rootDelta, source, output->matrix)) {
        return OverrideResult::invalid;
    }
    result = &output->matrix;
    if (entitySlot->kind == Kind::standalonePhysicsBody) {
        return nullPropModel ? OverrideResult::appliedStandaloneBodyNullModel
                             : OverrideResult::appliedStandaloneBody;
    }
    if (entitySlot->kind != Kind::propObject) {
        return OverrideResult::appliedGrunt;
    }
    if (matchedDirectParent && nullPropModel) {
        return OverrideResult::appliedPropDirectParentNullModel;
    }
    if (matchedDirectParent) {
        return OverrideResult::appliedPropDirectParent;
    }
    if (matchedPropParent && nullPropModel) {
        return OverrideResult::appliedPropParentNullModel;
    }
    if (matchedPropParent) {
        return OverrideResult::appliedPropParent;
    }
    return nullPropModel ? OverrideResult::appliedPropNullModel : OverrideResult::appliedProp;
}

std::uint32_t Runtime::EndRender() noexcept {
    std::uint32_t restoreFailed = 0;
    for (ModelHistorySlot& history : modelHistories_) {
        if (!history.overridden) {
            continue;
        }
        if (IsWritable(history.overrideBuffer, sizeof(Matrix))) {
            *history.overrideBuffer = history.restore;
        } else {
            history.snapshots = {};
            history.snapshotCount = 0;
            history.hasGeneration = false;
            ++restoreFailed;
        }
        history.overrideBuffer = nullptr;
        history.overridden = false;
    }
    renderActive_ = false;
    modelSlotCount_ = 0;
    renderOwnerCount_ = 0;
    renderOwnerBuckets_ = {};
    renderRootCount_ = 0;
    renderRoots_ = {};
    renderRootBuckets_ = {};
    for (Slot& slot : slots_) {
        slot.renderReady = false;
    }
    return restoreFailed;
}

const wchar_t* StageResultName(const StageResult result) noexcept {
    switch (result) {
    case StageResult::stagedProp:
        return L"staged-prop";
    case StageResult::stagedGrunt:
        return L"staged-grunt";
    case StageResult::cachedSleepingProp:
        return L"cached-sleeping-prop";
    case StageResult::ineligible:
        return L"ineligible";
    case StageResult::invalid:
        return L"invalid";
    case StageResult::capacityExhausted:
        return L"capacity";
    }
    return L"unknown";
}

const wchar_t* OverrideResultName(const OverrideResult result) noexcept {
    switch (result) {
    case OverrideResult::notWorld:
        return L"not-world";
    case OverrideResult::appliedProp:
        return L"applied-prop";
    case OverrideResult::appliedPropParent:
        return L"applied-prop-parent";
    case OverrideResult::appliedPropNullModel:
        return L"applied-prop-null-model";
    case OverrideResult::appliedPropParentNullModel:
        return L"applied-prop-parent-null-model";
    case OverrideResult::appliedPropDirectParent:
        return L"applied-prop-direct-parent";
    case OverrideResult::appliedPropDirectParentNullModel:
        return L"applied-prop-direct-parent-null-model";
    case OverrideResult::appliedPropModelHistory:
        return L"applied-prop-model-history";
    case OverrideResult::appliedGrunt:
        return L"applied-grunt";
    case OverrideResult::appliedStandaloneBody:
        return L"applied-standalone-body";
    case OverrideResult::appliedStandaloneBodyNullModel:
        return L"applied-standalone-body-null-model";
    case OverrideResult::propParentMismatch:
        return L"prop-parent-mismatch";
    case OverrideResult::invalid:
        return L"invalid";
    case OverrideResult::slotsExhausted:
        return L"slots-exhausted";
    }
    return L"unknown";
}

} // namespace atdd::world
