#include "mouse_preview_math.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace atdd::mouse_preview {

namespace {

std::int32_t ClampGuiCoordinate(const std::int32_t base,
                                const std::int64_t pending,
                                const std::int32_t extent) noexcept {
    if (extent <= 0) {
        return base;
    }
    const std::int64_t value = static_cast<std::int64_t>(base) + pending;
    return static_cast<std::int32_t>(
        std::clamp(value, std::int64_t{0}, static_cast<std::int64_t>(extent - 1)));
}

std::int32_t SaturatingDifference(const std::int32_t current,
                                  const std::int32_t previous) noexcept {
    const std::int64_t difference = static_cast<std::int64_t>(current) - previous;
    return static_cast<std::int32_t>(std::clamp(
        difference,
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()),
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())));
}

} // namespace

CommandShape ClassifyCommandShape(const float actualPitch,
                                  const float actualYaw,
                                  const float expectedPitch,
                                  const float expectedYaw) noexcept {
    if (!std::isfinite(actualPitch) || !std::isfinite(actualYaw) ||
        !std::isfinite(expectedPitch) || !std::isfinite(expectedYaw)) {
        return CommandShape::invalid;
    }

    constexpr float kSignalEpsilon = 0.000001F;
    const bool pitchSignal = std::fabs(actualPitch) >= kSignalEpsilon ||
                             std::fabs(expectedPitch) >= kSignalEpsilon;
    const bool yawSignal = std::fabs(actualYaw) >= kSignalEpsilon ||
                           std::fabs(expectedYaw) >= kSignalEpsilon;
    if (!pitchSignal && !yawSignal) {
        return CommandShape::noSignal;
    }

    const auto compatible = [](const float actual, const float expected) noexcept {
        const float tolerance =
            std::fmax(std::fabs(expected) * 0.5F, 0.00001F);
        return std::fabs(actual - expected) <= tolerance;
    };
    return compatible(actualPitch, expectedPitch) &&
                   compatible(actualYaw, expectedYaw)
               ? CommandShape::direct
               : CommandShape::transformed;
}

bool ShouldUpdateDirectScale(const bool calibratedThisCommand,
                             const bool wasAllowed,
                             const bool allowed,
                             const CommandShape shape) noexcept {
    return !calibratedThisCommand && wasAllowed && allowed &&
           shape == CommandShape::direct;
}

void PendingRawGate::Reset(const bool allowed) noexcept {
    allowed_ = allowed;
    recoverySamples_ = 0;
}

bool PendingRawGate::Update(const CommandShape shape) noexcept {
    switch (shape) {
    case CommandShape::transformed:
    case CommandShape::invalid:
        allowed_ = false;
        recoverySamples_ = 0;
        break;
    case CommandShape::direct:
        if (!allowed_) {
            ++recoverySamples_;
            if (recoverySamples_ >= kRecoverySamples) {
                allowed_ = true;
                recoverySamples_ = 0;
            }
        } else {
            recoverySamples_ = 0;
        }
        break;
    case CommandShape::noSignal:
        if (!allowed_) {
            recoverySamples_ = 0;
        }
        break;
    }
    return allowed_;
}

GuiPosition PreviewGuiPosition(const GuiPosition fixedPosition,
                               const std::int64_t pendingX,
                               const std::int64_t pendingY,
                               const std::int32_t clientWidth,
                               const std::int32_t clientHeight) noexcept {
    return {
        ClampGuiCoordinate(fixedPosition.x, pendingX, clientWidth),
        ClampGuiCoordinate(fixedPosition.y, pendingY, clientHeight),
    };
}

GuiPosition GuiRelativeDelta(const GuiPosition previous,
                             const GuiPosition current) noexcept {
    return {
        SaturatingDifference(current.x, previous.x),
        SaturatingDifference(current.y, previous.y),
    };
}

TickRoute RouteTickCounts(const std::int32_t freshCounts,
                          const std::int64_t pendingCounts,
                          const bool previewEligible) noexcept {
    if (previewEligible) {
        return {pendingCounts, freshCounts};
    }
    return {pendingCounts + static_cast<std::int64_t>(freshCounts), 0};
}

bool ContinuousPlayerAmount(const std::int64_t rawCounts,
                            const float sensitivity,
                            const std::int64_t roundedCounts,
                            const float stockAmount,
                            const float amountPerRoundedCount,
                            float& adjustedAmount) noexcept {
    adjustedAmount = stockAmount;
    if (!std::isfinite(sensitivity) || !std::isfinite(stockAmount) ||
        !std::isfinite(amountPerRoundedCount) || sensitivity <= 0.0F ||
        amountPerRoundedCount == 0.0F) {
        return false;
    }
    const double fractionalCounts =
        static_cast<double>(rawCounts) * static_cast<double>(sensitivity) -
        static_cast<double>(roundedCounts);
    const double result = static_cast<double>(stockAmount) +
                          fractionalCounts *
                              static_cast<double>(amountPerRoundedCount);
    if (!std::isfinite(result) ||
        result < -static_cast<double>(std::numeric_limits<float>::max()) ||
        result > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }
    adjustedAmount = static_cast<float>(result);
    return true;
}

bool ContinuousAngle(const std::int64_t rawCounts,
                     const float sensitivity,
                     const float anglePerRoundedCount,
                     float& angle) noexcept {
    angle = 0.0F;
    if (!std::isfinite(sensitivity) || !std::isfinite(anglePerRoundedCount) ||
        sensitivity <= 0.0F) {
        return false;
    }
    const double result = static_cast<double>(rawCounts) *
                          static_cast<double>(sensitivity) *
                          static_cast<double>(anglePerRoundedCount);
    if (!std::isfinite(result) ||
        result < -static_cast<double>(std::numeric_limits<float>::max()) ||
        result > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }
    angle = static_cast<float>(result);
    return true;
}

} // namespace atdd::mouse_preview
