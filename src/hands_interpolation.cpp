#include "hands_interpolation.h"

#include <algorithm>
#include <cmath>

namespace atdd::hands {
namespace {

struct Quaternion {
    float w = 1.0F;
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

float Dot(const Quaternion& left, const Quaternion& right) noexcept {
    return left.w * right.w + left.x * right.x + left.y * right.y +
           left.z * right.z;
}

Quaternion Normalize(const Quaternion& value) noexcept {
    const float lengthSquared = Dot(value, value);
    if (!(lengthSquared > 0.0F) || !std::isfinite(lengthSquared)) {
        return {};
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return {value.w * inverseLength, value.x * inverseLength,
            value.y * inverseLength, value.z * inverseLength};
}

Quaternion FromMatrix(const Matrix& matrix) noexcept {
    const auto& m = matrix.values;
    Quaternion result{};
    const float trace = m[0] + m[5] + m[10];
    if (trace > 0.0F) {
        const float scale = std::sqrt(trace + 1.0F) * 2.0F;
        result.w = 0.25F * scale;
        result.x = (m[9] - m[6]) / scale;
        result.y = (m[2] - m[8]) / scale;
        result.z = (m[4] - m[1]) / scale;
    } else if (m[0] > m[5] && m[0] > m[10]) {
        const float scale = std::sqrt(1.0F + m[0] - m[5] - m[10]) * 2.0F;
        result.w = (m[9] - m[6]) / scale;
        result.x = 0.25F * scale;
        result.y = (m[1] + m[4]) / scale;
        result.z = (m[2] + m[8]) / scale;
    } else if (m[5] > m[10]) {
        const float scale = std::sqrt(1.0F + m[5] - m[0] - m[10]) * 2.0F;
        result.w = (m[2] - m[8]) / scale;
        result.x = (m[1] + m[4]) / scale;
        result.y = 0.25F * scale;
        result.z = (m[6] + m[9]) / scale;
    } else {
        const float scale = std::sqrt(1.0F + m[10] - m[0] - m[5]) * 2.0F;
        result.w = (m[4] - m[1]) / scale;
        result.x = (m[2] + m[8]) / scale;
        result.y = (m[6] + m[9]) / scale;
        result.z = 0.25F * scale;
    }
    return Normalize(result);
}

Quaternion Slerp(const Quaternion& from, const Quaternion& to, const float alpha) noexcept {
    Quaternion end = to;
    float cosine = Dot(from, end);
    if (cosine < 0.0F) {
        end = {-end.w, -end.x, -end.y, -end.z};
        cosine = -cosine;
    }
    cosine = std::clamp(cosine, -1.0F, 1.0F);
    if (cosine > 0.9995F) {
        return Normalize({
            from.w + alpha * (end.w - from.w),
            from.x + alpha * (end.x - from.x),
            from.y + alpha * (end.y - from.y),
            from.z + alpha * (end.z - from.z),
        });
    }
    const float angle = std::acos(cosine);
    const float sine = std::sin(angle);
    const float fromWeight = std::sin((1.0F - alpha) * angle) / sine;
    const float toWeight = std::sin(alpha * angle) / sine;
    return Normalize({
        from.w * fromWeight + end.w * toWeight,
        from.x * fromWeight + end.x * toWeight,
        from.y * fromWeight + end.y * toWeight,
        from.z * fromWeight + end.z * toWeight,
    });
}

void SetRotation(const Quaternion& input, Matrix& matrix) noexcept {
    const Quaternion q = Normalize(input);
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;
    matrix.values[0] = 1.0F - 2.0F * (yy + zz);
    matrix.values[1] = 2.0F * (xy - wz);
    matrix.values[2] = 2.0F * (xz + wy);
    matrix.values[4] = 2.0F * (xy + wz);
    matrix.values[5] = 1.0F - 2.0F * (xx + zz);
    matrix.values[6] = 2.0F * (yz - wx);
    matrix.values[8] = 2.0F * (xz - wy);
    matrix.values[9] = 2.0F * (yz + wx);
    matrix.values[10] = 1.0F - 2.0F * (xx + yy);
}

bool IsDiscontinuous(const Matrix& older,
                     const Matrix& newer,
                     const std::uint64_t serialDelta) noexcept {
    const float dx = newer.values[3] - older.values[3];
    const float dy = newer.values[7] - older.values[7];
    const float dz = newer.values[11] - older.values[11];
    const float allowedPosition =
        History::kMaximumPositionDeltaPerTick * static_cast<float>(serialDelta);
    if (dx * dx + dy * dy + dz * dz > allowedPosition * allowedPosition) {
        return true;
    }
    const float allowedAngle =
        History::kMaximumOrientationDeltaPerTick * static_cast<float>(serialDelta);
    return OrientationDistance(older, newer) > allowedAngle;
}

} // namespace

Matrix Identity() noexcept {
    Matrix result{};
    result.values[0] = 1.0F;
    result.values[5] = 1.0F;
    result.values[10] = 1.0F;
    result.values[15] = 1.0F;
    return result;
}

bool IsRigid(const Matrix& matrix) noexcept {
    for (const float value : matrix.values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    constexpr float epsilon = 2.0e-3F;
    if (std::abs(matrix.values[12]) > epsilon ||
        std::abs(matrix.values[13]) > epsilon ||
        std::abs(matrix.values[14]) > epsilon ||
        std::abs(matrix.values[15] - 1.0F) > epsilon) {
        return false;
    }
    for (int row = 0; row < 3; ++row) {
        float length = 0.0F;
        for (int column = 0; column < 3; ++column) {
            const float value = matrix.values[row * 4 + column];
            length += value * value;
        }
        if (std::abs(length - 1.0F) > epsilon) {
            return false;
        }
    }
    for (int first = 0; first < 3; ++first) {
        for (int second = first + 1; second < 3; ++second) {
            float dot = 0.0F;
            for (int column = 0; column < 3; ++column) {
                dot += matrix.values[first * 4 + column] *
                       matrix.values[second * 4 + column];
            }
            if (std::abs(dot) > epsilon) {
                return false;
            }
        }
    }
    const float determinant =
        matrix.values[0] * (matrix.values[5] * matrix.values[10] -
                            matrix.values[6] * matrix.values[9]) -
        matrix.values[1] * (matrix.values[4] * matrix.values[10] -
                            matrix.values[6] * matrix.values[8]) +
        matrix.values[2] * (matrix.values[4] * matrix.values[9] -
                            matrix.values[5] * matrix.values[8]);
    return std::abs(determinant - 1.0F) <= 4.0e-3F;
}

bool NearlyEqual(const Matrix& left, const Matrix& right, const float epsilon) noexcept {
    for (std::size_t index = 0; index < left.values.size(); ++index) {
        if (std::abs(left.values[index] - right.values[index]) > epsilon) {
            return false;
        }
    }
    return true;
}

float OrientationDistance(const Matrix& left, const Matrix& right) noexcept {
    const Quaternion first = FromMatrix(left);
    const Quaternion second = FromMatrix(right);
    const float cosine = std::clamp(std::abs(Dot(first, second)), 0.0F, 1.0F);
    return 2.0F * std::acos(cosine);
}

bool Interpolate(const Matrix& older,
                 const Matrix& newer,
                 const double alpha,
                 Matrix& result) noexcept {
    if (!IsRigid(older) || !IsRigid(newer) || !std::isfinite(alpha) ||
        alpha < 0.0 || alpha > 1.0) {
        return false;
    }
    const float t = static_cast<float>(alpha);
    result = Identity();
    SetRotation(Slerp(FromMatrix(older), FromMatrix(newer), t), result);
    result.values[3] = older.values[3] + (newer.values[3] - older.values[3]) * t;
    result.values[7] = older.values[7] + (newer.values[7] - older.values[7]) * t;
    result.values[11] = older.values[11] + (newer.values[11] - older.values[11]) * t;
    return IsRigid(result);
}

bool InverseRigid(const Matrix& matrix, Matrix& result) noexcept {
    if (!IsRigid(matrix)) {
        return false;
    }
    result = Identity();
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            result.values[row * 4 + column] = matrix.values[column * 4 + row];
        }
    }
    for (int row = 0; row < 3; ++row) {
        result.values[row * 4 + 3] =
            -(result.values[row * 4] * matrix.values[3] +
              result.values[row * 4 + 1] * matrix.values[7] +
              result.values[row * 4 + 2] * matrix.values[11]);
    }
    return IsRigid(result);
}

bool Multiply(const Matrix& left, const Matrix& right, Matrix& result) noexcept {
    Matrix product{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            float value = 0.0F;
            for (int inner = 0; inner < 4; ++inner) {
                value += left.values[row * 4 + inner] *
                         right.values[inner * 4 + column];
            }
            product.values[row * 4 + column] = value;
        }
    }
    for (const float value : product.values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    result = product;
    return true;
}

void History::Reset() noexcept {
    first_ = 0;
    count_ = 0;
    generation_ = 0;
    hasGeneration_ = false;
}

const History::Snapshot& History::At(const std::size_t logicalIndex) const noexcept {
    return snapshots_[(first_ + logicalIndex) % kCapacity];
}

void History::Append(const std::uint64_t serial, const Matrix& matrix) noexcept {
    if (count_ < kCapacity) {
        snapshots_[(first_ + count_) % kCapacity] = {serial, matrix};
        ++count_;
    } else {
        snapshots_[first_] = {serial, matrix};
        first_ = (first_ + 1) % kCapacity;
    }
}

PublishKind History::Publish(const std::uint64_t serial,
                             const std::uint64_t clockGeneration,
                             const Matrix& matrix) noexcept {
    if (!IsRigid(matrix)) {
        Reset();
        return PublishKind::invalid;
    }
    if (!hasGeneration_ || generation_ != clockGeneration) {
        Reset();
        generation_ = clockGeneration;
        hasGeneration_ = true;
        Append(serial, matrix);
        return PublishKind::resetGeneration;
    }
    if (count_ != 0) {
        const Snapshot& latest = At(count_ - 1);
        if (serial < latest.serial) {
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, matrix);
            return PublishKind::resetNonMonotonic;
        }
        if (serial == latest.serial) {
            if (NearlyEqual(latest.matrix, matrix)) {
                return PublishKind::unchanged;
            }
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, matrix);
            return PublishKind::resetSameTickMutation;
        }
        const std::uint64_t delta = serial - latest.serial;
        if (delta > clock::PresentationClock::kMaximumBridgedSerialDelta ||
            IsDiscontinuous(latest.matrix, matrix, delta)) {
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, matrix);
            return PublishKind::resetDiscontinuity;
        }
    }
    Append(serial, matrix);
    return PublishKind::appended;
}

SampleResult History::Evaluate(const clock::Sample& clockSample) const noexcept {
    if (clockSample.kind != clock::SampleKind::interpolated ||
        clockSample.olderSerial == clockSample.newerSerial) {
        return {SampleKind::clockUnavailable, {}};
    }
    if (!hasGeneration_ || generation_ != clockSample.generation) {
        return {SampleKind::generationMismatch, {}};
    }
    const Snapshot* older = nullptr;
    const Snapshot* newer = nullptr;
    for (std::size_t index = 0; index < count_; ++index) {
        const Snapshot& candidate = At(index);
        if (candidate.serial == clockSample.olderSerial) older = &candidate;
        if (candidate.serial == clockSample.newerSerial) newer = &candidate;
    }
    if (older == nullptr || newer == nullptr) {
        return {SampleKind::snapshotsMissing, {}};
    }
    Matrix interpolated{};
    if (!Interpolate(older->matrix, newer->matrix, clockSample.alpha, interpolated)) {
        return {SampleKind::invalid, {}};
    }
    return {SampleKind::applied, interpolated};
}

const wchar_t* PublishKindName(const PublishKind kind) noexcept {
    switch (kind) {
    case PublishKind::appended: return L"appended";
    case PublishKind::unchanged: return L"unchanged";
    case PublishKind::invalid: return L"invalid";
    case PublishKind::resetGeneration: return L"generation";
    case PublishKind::resetNonMonotonic: return L"non-monotonic";
    case PublishKind::resetSameTickMutation: return L"same-tick-mutation";
    case PublishKind::resetDiscontinuity: return L"discontinuity";
    }
    return L"unknown";
}

const wchar_t* SampleKindName(const SampleKind kind) noexcept {
    switch (kind) {
    case SampleKind::applied: return L"applied";
    case SampleKind::clockUnavailable: return L"clock-unavailable";
    case SampleKind::generationMismatch: return L"generation-mismatch";
    case SampleKind::snapshotsMissing: return L"snapshots-missing";
    case SampleKind::invalid: return L"invalid";
    }
    return L"unknown";
}

} // namespace atdd::hands
