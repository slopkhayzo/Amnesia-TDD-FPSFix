#pragma once

#include <cstdint>

namespace atdd::mouse_preview {

struct TickRoute {
    std::int64_t simulationCounts = 0;
    std::int64_t deferredCounts = 0;
};

struct GuiPosition {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

enum class CommandShape : std::uint8_t {
    noSignal,
    direct,
    transformed,
    invalid,
};

// Classifies whether HPL's final fixed-tick camera command still has the
// direct linear relationship to raw mouse input required by the presentation
// preview. Camera effects such as the low-sanity moving average deliberately
// break that relationship and must remain engine-owned.
[[nodiscard]] CommandShape ClassifyCommandShape(float actualPitch,
                                                float actualYaw,
                                                float expectedPitch,
                                                float expectedYaw) noexcept;

// Calibration may follow only an already-established direct path. In
// particular, neither transformed fallback nor the command that completes its
// recovery may alter the reference used to recognize that recovery.
[[nodiscard]] bool ShouldUpdateDirectScale(bool calibratedThisCommand,
                                           bool wasAllowed,
                                           bool allowed,
                                           CommandShape shape) noexcept;

class PendingRawGate final {
public:
    static constexpr std::uint32_t kRecoverySamples = 12;

    void Reset(bool allowed = true) noexcept;
    [[nodiscard]] bool Update(CommandShape shape) noexcept;
    [[nodiscard]] bool Allowed() const noexcept { return allowed_; }

private:
    bool allowed_ = true;
    std::uint32_t recoverySamples_ = 0;
};

// Predicts HPL's software-cursor position from the last fixed-update absolute
// position and native raw counts that have arrived since that update. The
// result is clamped to the SDL client area and never carries history across
// fixed updates.
[[nodiscard]] GuiPosition PreviewGuiPosition(GuiPosition fixedPosition,
                                             std::int64_t pendingX,
                                             std::int64_t pendingY,
                                             std::int32_t clientWidth,
                                             std::int32_t clientHeight) noexcept;

// Returns the not-yet-dispatched part of a GUI move. This lets presentation
// updates deliver relative movement once, while the next fixed GUI update
// supplies only any correction between its authoritative absolute position
// and the last previewed position.
[[nodiscard]] GuiPosition GuiRelativeDelta(GuiPosition previous,
                                           GuiPosition current) noexcept;

// During gameplay, counts first acquired by HPL's fixed-tick SDL update are
// previewed immediately and delivered to simulation on the following tick.
// Counts already in pendingCounts have already been previewed and must be
// retired exactly once by the current simulation tick. Outside gameplay the
// stock behavior is retained by delivering both sources immediately.
[[nodiscard]] TickRoute RouteTickCounts(std::int32_t freshCounts,
                                        std::int64_t pendingCounts,
                                        bool previewEligible) noexcept;

// Replaces only the fractional error introduced by HPL's
// RoundToInt(raw*sensitivity), preserving every other component already in the
// player look command.
[[nodiscard]] bool ContinuousPlayerAmount(std::int64_t rawCounts,
                                          float sensitivity,
                                          std::int64_t roundedCounts,
                                          float stockAmount,
                                          float amountPerRoundedCount,
                                          float& adjustedAmount) noexcept;

// Converts accumulated raw SDL relative counts into a render-only angular
// preview without reproducing HPL's fixed-tick integer sensitivity rounding.
// anglePerRoundedCount is calibrated from HPL's authoritative camera command,
// so look-speed/state multipliers remain engine-owned.
[[nodiscard]] bool ContinuousAngle(std::int64_t rawCounts,
                                   float sensitivity,
                                   float anglePerRoundedCount,
                                   float& angle) noexcept;

} // namespace atdd::mouse_preview
