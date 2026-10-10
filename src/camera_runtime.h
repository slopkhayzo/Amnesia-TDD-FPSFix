#pragma once

#include "camera_interpolation.h"
#include "presentation_clock.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace atdd::camera {

struct Functions {
    void* setPosition = nullptr;
    void* setPitch = nullptr;
    void* setYaw = nullptr;
    void* setRoll = nullptr;
};

struct AngularLimits {
    float pitchMin = 0.0F;
    float pitchMax = 0.0F;
    float yawMin = 0.0F;
    float yawMax = 0.0F;
};

enum class FrameResult : std::uint32_t {
    captured,
    applied,
    noScene,
    noCurrentListener,
    listenerHidden,
    listenerMissingCamera,
    listenerMissingWorld,
    invalidViewport,
    identityChanged,
    invalidCamera,
    clockGenerationReset,
    nonMonotonicReset,
    sameTickMutation,
    discontinuityReset,
    clockUnavailable,
    snapshotsMissing,
    applyFailed,
};

class Runtime {
public:
    void Configure(const Functions& functions) noexcept;
    [[nodiscard]] FrameResult CaptureCompletedTick(std::uint64_t tickSerial,
                                                   std::uint64_t clockGeneration) noexcept;
    [[nodiscard]] FrameResult Apply(void* scene,
                                    std::uint64_t tickSerial,
                                    const clock::Sample& clockSample) noexcept;
    [[nodiscard]] bool ApplyAngularOffset(float pitchDelta, float yawDelta,
                                          Pose* before = nullptr,
                                          Pose* after = nullptr) noexcept;
    [[nodiscard]] bool ReadActivePose(Pose& pose) const noexcept;
    [[nodiscard]] bool ReadAuthoritativePose(Pose& pose) const noexcept;
    [[nodiscard]] bool ReadActiveAngularLimits(AngularLimits& limits) const noexcept;
    [[nodiscard]] void* ActiveCamera() const noexcept {
        return overrideActive_ ? overriddenCamera_ : nullptr;
    }
    [[nodiscard]] bool Restore() noexcept;

private:
    enum class SelectResult : std::uint32_t {
        selected,
        noCurrentListener,
        hidden,
        missingCamera,
        missingWorld,
        invalidLayout,
    };

    struct Identity {
        void* viewport = nullptr;
        void* world = nullptr;
        void* camera = nullptr;
    };

    struct SavedState {
        Pose pose{};
        AngularLimits angularLimits{};
        std::array<std::uint8_t, 64> viewMatrix{};
        std::array<std::uint8_t, 64> projectionMatrix{};
        std::array<std::uint8_t, 64> moveMatrix{};
        std::array<std::uint8_t, 0x4FC> frustum{};
        std::array<std::uint8_t, 5> flags{};
    };

    [[nodiscard]] SelectResult SelectMainView(void* scene,
                                              Identity& identity) const noexcept;
    [[nodiscard]] static FrameResult SelectionFailure(SelectResult result) noexcept;
    [[nodiscard]] bool ReadPose(void* camera, Pose& pose,
                                AngularLimits* limits = nullptr) const noexcept;
    [[nodiscard]] bool SaveState(void* camera, SavedState& state) const noexcept;
    [[nodiscard]] bool ApplyPose(void* camera, const Pose& pose) const noexcept;
    [[nodiscard]] bool RestoreState(void* camera, const SavedState& state) const noexcept;

    Functions functions_{};
    History history_{};
    Identity identity_{};
    void* scene_ = nullptr;
    void* overriddenCamera_ = nullptr;
    SavedState authoritativeState_{};
    bool hasIdentity_ = false;
    bool overrideActive_ = false;
};

[[nodiscard]] const wchar_t* FrameResultName(FrameResult result) noexcept;

} // namespace atdd::camera
