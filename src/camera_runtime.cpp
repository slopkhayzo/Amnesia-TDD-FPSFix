#include "camera_runtime.h"

#include <Windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace atdd::camera {
namespace {

constexpr std::size_t kSceneCurrentListenerOffset = 0x40;
constexpr std::size_t kViewportCameraOffset = 0x04;
constexpr std::size_t kViewportWorldOffset = 0x08;
constexpr std::size_t kViewportVisibleOffset = 0x0D;
constexpr std::size_t kCameraPositionOffset = 0x00;
constexpr std::size_t kCameraPitchOffset = 0x28;
constexpr std::size_t kCameraYawOffset = 0x2C;
constexpr std::size_t kCameraRollOffset = 0x30;
constexpr std::size_t kCameraPitchLimitMinOffset = 0x34;
constexpr std::size_t kCameraPitchLimitMaxOffset = 0x38;
constexpr std::size_t kCameraYawLimitMinOffset = 0x3C;
constexpr std::size_t kCameraYawLimitMaxOffset = 0x40;
constexpr std::size_t kCameraRotateModeOffset = 0x44;
constexpr std::size_t kCameraViewMatrixOffset = 0x4C;
constexpr std::size_t kCameraProjectionMatrixOffset = 0x8C;
constexpr std::size_t kCameraMoveMatrixOffset = 0xCC;
constexpr std::size_t kCameraFrustumOffset = 0x354;
constexpr std::size_t kCameraFlagsOffset = 0x850;
constexpr std::size_t kCameraViewUpdatedOffset = 0x851;
constexpr std::size_t kCameraMoveUpdatedOffset = 0x853;
constexpr std::size_t kCameraFrustumUpdatedOffset = 0x854;
constexpr float kTwoPi = 6.28318530717958647692F;

struct CameraAngleStorage {
    float pitch;
    float yaw;
    float roll;
    AngularLimits limits;
    std::uint32_t rotateMode;
};

static_assert(sizeof(CameraAngleStorage) == 0x20);

bool IsFinite(const AngularLimits& limits) noexcept {
    constexpr float kMaximumSaneAngle = 1000.0F;
    return std::isfinite(limits.pitchMin) && std::isfinite(limits.pitchMax) &&
           std::isfinite(limits.yawMin) && std::isfinite(limits.yawMax) &&
           std::abs(limits.pitchMin) <= kMaximumSaneAngle &&
           std::abs(limits.pitchMax) <= kMaximumSaneAngle &&
           std::abs(limits.yawMin) <= kMaximumSaneAngle &&
           std::abs(limits.yawMax) <= kMaximumSaneAngle;
}

bool HasYawLimits(const AngularLimits& limits) noexcept {
    return limits.yawMin != 0.0F || limits.yawMax != 0.0F;
}

float RebaseAngleNear(const float value, const float reference) noexcept {
    return value + std::round((reference - value) / kTwoPi) * kTwoPi;
}

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
    if (!IsReadable(address, bytes)) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory)) {
        return false;
    }
    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & writable) != 0;
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
    value = *reinterpret_cast<const T*>(address);
    return true;
}

} // namespace

void Runtime::Configure(const Functions& functions) noexcept {
    functions_ = functions;
    history_.Reset();
    identity_ = {};
    scene_ = nullptr;
    overriddenCamera_ = nullptr;
    hasIdentity_ = false;
    overrideActive_ = false;
}

FrameResult Runtime::CaptureCompletedTick(const std::uint64_t tickSerial,
                                          const std::uint64_t clockGeneration) noexcept {
    if (scene_ == nullptr) {
        return FrameResult::noScene;
    }

    Identity selected{};
    const SelectResult selection = SelectMainView(scene_, selected);
    if (selection != SelectResult::selected) {
        history_.Reset();
        hasIdentity_ = false;
        return SelectionFailure(selection);
    }

    Pose current{};
    if (!ReadPose(selected.camera, current)) {
        history_.Reset();
        hasIdentity_ = false;
        return FrameResult::invalidCamera;
    }
    if (!hasIdentity_ || identity_.viewport != selected.viewport ||
        identity_.world != selected.world || identity_.camera != selected.camera) {
        identity_ = selected;
        hasIdentity_ = true;
        history_.Reset();
        (void)history_.Publish(tickSerial, clockGeneration, current);
        return FrameResult::identityChanged;
    }

    const PublishResult published = history_.Publish(tickSerial, clockGeneration, current);
    switch (published.kind) {
    case PublishKind::appended:
    case PublishKind::unchanged:
        return FrameResult::captured;
    case PublishKind::invalid:
        return FrameResult::invalidCamera;
    case PublishKind::resetGeneration:
        return FrameResult::clockGenerationReset;
    case PublishKind::resetNonMonotonic:
        return FrameResult::nonMonotonicReset;
    case PublishKind::resetSameTickMutation:
        return FrameResult::sameTickMutation;
    case PublishKind::resetDiscontinuity:
        return FrameResult::discontinuityReset;
    }
    return FrameResult::invalidCamera;
}

Runtime::SelectResult Runtime::SelectMainView(void* scene,
                                               Identity& identity) const noexcept {
    void* viewport = nullptr;
    if (!ReadAt(scene, kSceneCurrentListenerOffset, viewport)) {
        return SelectResult::invalidLayout;
    }
    if (viewport == nullptr) {
        return SelectResult::noCurrentListener;
    }

    std::uint8_t visible = 0;
    if (!ReadAt(viewport, kViewportVisibleOffset, visible) || visible > 1U) {
        return SelectResult::invalidLayout;
    }
    if (visible == 0U) {
        return SelectResult::hidden;
    }

    identity = {};
    identity.viewport = viewport;
    if (!ReadAt(viewport, kViewportCameraOffset, identity.camera)) {
        return SelectResult::invalidLayout;
    }
    if (identity.camera == nullptr) {
        return SelectResult::missingCamera;
    }
    if (!ReadAt(viewport, kViewportWorldOffset, identity.world)) {
        return SelectResult::invalidLayout;
    }
    if (identity.world == nullptr) {
        return SelectResult::missingWorld;
    }
    return SelectResult::selected;
}

FrameResult Runtime::SelectionFailure(const SelectResult result) noexcept {
    switch (result) {
    case SelectResult::noCurrentListener: return FrameResult::noCurrentListener;
    case SelectResult::hidden: return FrameResult::listenerHidden;
    case SelectResult::missingCamera: return FrameResult::listenerMissingCamera;
    case SelectResult::missingWorld: return FrameResult::listenerMissingWorld;
    case SelectResult::invalidLayout: return FrameResult::invalidViewport;
    case SelectResult::selected: break;
    }
    return FrameResult::invalidViewport;
}

bool Runtime::ReadPose(void* camera, Pose& pose, AngularLimits* const limits) const noexcept {
    CameraAngleStorage angles{};
    if (!ReadAt(camera, kCameraPositionOffset, pose.position) ||
        !ReadAt(camera, kCameraPitchOffset, angles) || angles.rotateMode != 0U) {
        return false;
    }
    pose.pitch = angles.pitch;
    pose.yaw = angles.yaw;
    pose.roll = angles.roll;
    if (!IsFinite(pose) || !IsFinite(angles.limits)) {
        return false;
    }
    if (limits != nullptr) {
        *limits = angles.limits;
    }
    return true;
}

bool Runtime::ReadActiveAngularLimits(AngularLimits& limits) const noexcept {
    limits = {};
    if (!overrideActive_ || overriddenCamera_ == nullptr) {
        return false;
    }
    limits = authoritativeState_.angularLimits;
    return IsFinite(limits);
}

bool Runtime::SaveState(void* camera, SavedState& state) const noexcept {
    if (!ReadPose(camera, state.pose, &state.angularLimits) ||
        !ReadAt(camera, kCameraViewMatrixOffset, state.viewMatrix) ||
        !ReadAt(camera, kCameraProjectionMatrixOffset, state.projectionMatrix) ||
        !ReadAt(camera, kCameraMoveMatrixOffset, state.moveMatrix) ||
        !ReadAt(camera, kCameraFrustumOffset, state.frustum) ||
        !ReadAt(camera, kCameraFlagsOffset, state.flags)) {
        return false;
    }
    return true;
}

bool Runtime::ApplyPose(void* camera, const Pose& pose) const noexcept {
    if (camera == nullptr || !IsFinite(pose) || functions_.setPosition == nullptr ||
        functions_.setPitch == nullptr || functions_.setYaw == nullptr ||
        functions_.setRoll == nullptr) {
        return false;
    }
    auto* bytes = static_cast<std::uint8_t*>(camera);
    if (!IsWritable(bytes, kCameraFlagsOffset + 5)) {
        return false;
    }
    std::memcpy(bytes + kCameraPositionOffset, &pose.position, sizeof(pose.position));
    std::memcpy(bytes + kCameraPitchOffset, &pose.pitch, sizeof(pose.pitch));
    std::memcpy(bytes + kCameraYawOffset, &pose.yaw, sizeof(pose.yaw));
    std::memcpy(bytes + kCameraRollOffset, &pose.roll, sizeof(pose.roll));
    bytes[kCameraViewUpdatedOffset] = 1U;
    bytes[kCameraMoveUpdatedOffset] = 1U;
    bytes[kCameraFrustumUpdatedOffset] = 1U;

    Pose applied{};
    return ReadPose(camera, applied) && NearlyEqual(applied, pose, 0.0F);
}

bool Runtime::RestoreState(void* camera, const SavedState& state) const noexcept {
    if (camera == nullptr) {
        return false;
    }
    auto* bytes = static_cast<std::uint8_t*>(camera);
    if (!IsWritable(bytes, kCameraFlagsOffset + state.flags.size())) {
        return false;
    }
    std::memcpy(bytes + kCameraPositionOffset, &state.pose.position,
                sizeof(state.pose.position));
    std::memcpy(bytes + kCameraPitchOffset, &state.pose.pitch, sizeof(state.pose.pitch));
    std::memcpy(bytes + kCameraYawOffset, &state.pose.yaw, sizeof(state.pose.yaw));
    std::memcpy(bytes + kCameraRollOffset, &state.pose.roll, sizeof(state.pose.roll));
    std::memcpy(bytes + kCameraViewMatrixOffset, state.viewMatrix.data(),
                state.viewMatrix.size());
    std::memcpy(bytes + kCameraProjectionMatrixOffset, state.projectionMatrix.data(),
                state.projectionMatrix.size());
    std::memcpy(bytes + kCameraMoveMatrixOffset, state.moveMatrix.data(),
                state.moveMatrix.size());
    std::memcpy(bytes + kCameraFrustumOffset, state.frustum.data(), state.frustum.size());
    std::memcpy(bytes + kCameraFlagsOffset, state.flags.data(), state.flags.size());

    Pose restored{};
    return ReadPose(camera, restored) && NearlyEqual(restored, state.pose, 0.0F);
}

FrameResult Runtime::Apply(void* scene,
                           const std::uint64_t tickSerial,
                           const clock::Sample& clockSample) noexcept {
    if (overrideActive_) {
        return FrameResult::applyFailed;
    }
    scene_ = scene;

    Identity selected{};
    const SelectResult selection = SelectMainView(scene, selected);
    if (selection != SelectResult::selected) {
        history_.Reset();
        hasIdentity_ = false;
        return SelectionFailure(selection);
    }
    if (!hasIdentity_ || identity_.viewport != selected.viewport ||
        identity_.world != selected.world || identity_.camera != selected.camera) {
        identity_ = selected;
        hasIdentity_ = true;
        history_.Reset();
        Pose first{};
        if (!ReadPose(identity_.camera, first)) {
            return FrameResult::invalidCamera;
        }
        (void)history_.Publish(tickSerial, clockSample.generation, first);
        return FrameResult::identityChanged;
    }

    Pose current{};
    if (!ReadPose(identity_.camera, current)) {
        history_.Reset();
        return FrameResult::invalidCamera;
    }
    const PublishResult published =
        history_.Publish(tickSerial, clockSample.generation, current);
    switch (published.kind) {
    case PublishKind::appended:
    case PublishKind::unchanged:
        break;
    case PublishKind::invalid:
        return FrameResult::invalidCamera;
    case PublishKind::resetGeneration:
        return FrameResult::clockGenerationReset;
    case PublishKind::resetNonMonotonic:
        return FrameResult::nonMonotonicReset;
    case PublishKind::resetSameTickMutation:
        return FrameResult::sameTickMutation;
    case PublishKind::resetDiscontinuity:
        return FrameResult::discontinuityReset;
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
        return FrameResult::invalidCamera;
    }

    if (!SaveState(identity_.camera, authoritativeState_)) {
        return FrameResult::invalidCamera;
    }
    overriddenCamera_ = identity_.camera;
    if (!ApplyPose(overriddenCamera_, sampled.pose)) {
        (void)RestoreState(overriddenCamera_, authoritativeState_);
        overriddenCamera_ = nullptr;
        return FrameResult::applyFailed;
    }
    overrideActive_ = true;
    return FrameResult::applied;
}

bool Runtime::ApplyAngularOffset(const float pitchDelta, const float yawDelta,
                                 Pose* const before, Pose* const after) noexcept {
    if (!overrideActive_ || overriddenCamera_ == nullptr ||
        functions_.setPitch == nullptr || functions_.setYaw == nullptr ||
        !std::isfinite(pitchDelta) || !std::isfinite(yawDelta)) {
        return false;
    }
    Pose current{};
    if (!ReadPose(overriddenCamera_, current)) {
        return false;
    }
    if (before != nullptr) {
        *before = current;
    }
    using SetAngle = void(__thiscall*)(void*, float);
    const auto setPitch = reinterpret_cast<SetAngle>(functions_.setPitch);
    const auto setYaw = reinterpret_cast<SetAngle>(functions_.setYaw);
    float targetYaw = current.yaw + yawDelta;
    if (HasYawLimits(authoritativeState_.angularLimits)) {
        // Quaternion-to-Euler interpolation returns a principal-range yaw,
        // while HPL's stateful constraints (notably ladders) retain the
        // authoritative unwrapped branch. SetYaw clamps numerically, so pass
        // the equivalent angle nearest that authoritative branch.
        targetYaw = RebaseAngleNear(targetYaw, authoritativeState_.pose.yaw);
    }
    if (!std::isfinite(targetYaw)) {
        return false;
    }
    setPitch(overriddenCamera_, current.pitch + pitchDelta);
    setYaw(overriddenCamera_, targetYaw);
    Pose applied{};
    if (!ReadPose(overriddenCamera_, applied)) {
        return false;
    }
    if (after != nullptr) {
        *after = applied;
    }
    return true;
}

bool Runtime::ReadActivePose(Pose& pose) const noexcept {
    return overrideActive_ && overriddenCamera_ != nullptr &&
           ReadPose(overriddenCamera_, pose);
}

bool Runtime::ReadAuthoritativePose(Pose& pose) const noexcept {
    if (!overrideActive_ || overriddenCamera_ == nullptr) {
        return false;
    }
    pose = authoritativeState_.pose;
    return IsFinite(pose);
}

bool Runtime::Restore() noexcept {
    if (!overrideActive_) {
        return true;
    }
    const bool restored = RestoreState(overriddenCamera_, authoritativeState_);
    overrideActive_ = false;
    overriddenCamera_ = nullptr;
    return restored;
}

const wchar_t* FrameResultName(const FrameResult result) noexcept {
    switch (result) {
    case FrameResult::captured: return L"captured";
    case FrameResult::applied: return L"applied";
    case FrameResult::noScene: return L"no-scene";
    case FrameResult::noCurrentListener: return L"no-current-listener";
    case FrameResult::listenerHidden: return L"listener-hidden";
    case FrameResult::listenerMissingCamera: return L"listener-missing-camera";
    case FrameResult::listenerMissingWorld: return L"listener-missing-world";
    case FrameResult::invalidViewport: return L"invalid-viewport";
    case FrameResult::identityChanged: return L"identity-changed";
    case FrameResult::invalidCamera: return L"invalid-camera";
    case FrameResult::clockGenerationReset: return L"clock-generation-reset";
    case FrameResult::nonMonotonicReset: return L"non-monotonic-reset";
    case FrameResult::sameTickMutation: return L"same-tick-mutation";
    case FrameResult::discontinuityReset: return L"discontinuity-reset";
    case FrameResult::clockUnavailable: return L"clock-unavailable";
    case FrameResult::snapshotsMissing: return L"snapshots-missing";
    case FrameResult::applyFailed: return L"apply-failed";
    }
    return L"unknown";
}

} // namespace atdd::camera
