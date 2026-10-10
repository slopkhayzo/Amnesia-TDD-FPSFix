#include "image_trail_timing.h"

#include <cmath>

namespace atdd::image_trail {
namespace {

constexpr double kReferenceRateHz = 60.0;
constexpr double kStockExponentScale = 0.015;
constexpr double kReferenceExponentScale =
    kStockExponentScale * kReferenceRateHz;

} // namespace

bool CorrectAmount(const float amount,
                   const float frameTimeSeconds,
                   float& correctedAmount) noexcept {
    if (!std::isfinite(amount) || amount < 0.0F ||
        !std::isfinite(frameTimeSeconds) || frameTimeSeconds <= 0.0F) {
        return false;
    }
    if (amount == 0.0F) {
        correctedAmount = 0.0F;
        return true;
    }

    const double stockCurrentWeight =
        std::exp(-kReferenceExponentScale * static_cast<double>(amount));
    if (!std::isfinite(stockCurrentWeight) || stockCurrentWeight <= 0.0 ||
        stockCurrentWeight >= 1.0) {
        return false;
    }

    // Work in logarithms to retain precision when the presentation interval
    // is very short and the required per-frame current weight is near zero.
    const double referenceRetentionLog = std::log1p(-stockCurrentWeight);
    const double elapsedReferenceFrames =
        static_cast<double>(frameTimeSeconds) * kReferenceRateHz;
    const double correctedCurrentWeight =
        -std::expm1(referenceRetentionLog * elapsedReferenceFrames);
    if (!std::isfinite(correctedCurrentWeight) ||
        correctedCurrentWeight <= 0.0 || correctedCurrentWeight > 1.0) {
        return false;
    }

    const double result =
        -std::log(correctedCurrentWeight) *
        static_cast<double>(frameTimeSeconds) / kStockExponentScale;
    if (!std::isfinite(result) || result < 0.0) {
        return false;
    }

    correctedAmount = static_cast<float>(result);
    return std::isfinite(correctedAmount) && correctedAmount >= 0.0F;
}

} // namespace atdd::image_trail
