#include "camera_interpolation.h"

#include <algorithm>
#include <cmath>

namespace atdd::camera {
namespace {

struct Quaternion {
    float w = 1.0F;
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

Quaternion Multiply(const Quaternion& left, const Quaternion& right) noexcept {
    return {
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
    };
}

Quaternion AxisAngle(const float angle,
                     const float x,
                     const float y,
                     const float z) noexcept {
    const float half = angle * 0.5F;
    const float sine = std::sin(half);
    return {std::cos(half), x * sine, y * sine, z * sine};
}

Quaternion FromEuler(const Pose& pose) noexcept {
    // HPL2's camera view is Rz(-roll) * Rx(-pitch) * Ry(-yaw).
    // Its world orientation is therefore Ry(yaw) * Rx(pitch) * Rz(roll).
    return Multiply(Multiply(AxisAngle(pose.yaw, 0.0F, 1.0F, 0.0F),
                             AxisAngle(pose.pitch, 1.0F, 0.0F, 0.0F)),
                    AxisAngle(pose.roll, 0.0F, 0.0F, 1.0F));
}

float Dot(const Quaternion& left, const Quaternion& right) noexcept {
    return left.w * right.w + left.x * right.x + left.y * right.y + left.z * right.z;
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

void ToEuler(const Quaternion& input, Pose& pose) noexcept {
    const Quaternion q = Normalize(input);

    // Matrix extraction for Ry(yaw) * Rx(pitch) * Rz(roll).
    const float m02 = 2.0F * (q.x * q.z + q.w * q.y);
    const float m10 = 2.0F * (q.x * q.y + q.w * q.z);
    const float m11 = 1.0F - 2.0F * (q.x * q.x + q.z * q.z);
    const float m12 = 2.0F * (q.y * q.z - q.w * q.x);
    const float m22 = 1.0F - 2.0F * (q.x * q.x + q.y * q.y);

    pose.pitch = std::asin(std::clamp(-m12, -1.0F, 1.0F));
    const float cosinePitch = std::cos(pose.pitch);
    if (std::abs(cosinePitch) > 1.0e-5F) {
        pose.yaw = std::atan2(m02, m22);
        pose.roll = std::atan2(m10, m11);
    } else {
        // Camera pitch is normally constrained away from this singularity.
        // Preserve a stable representation if a script reaches it.
        pose.yaw = std::atan2(-2.0F * (q.x * q.z - q.w * q.y),
                              1.0F - 2.0F * (q.y * q.y + q.z * q.z));
        pose.roll = 0.0F;
    }
}

bool IsDiscontinuous(const Pose& older,
                     const Pose& newer,
                     const std::uint64_t serialDelta) noexcept {
    const float dx = newer.position.x - older.position.x;
    const float dy = newer.position.y - older.position.y;
    const float dz = newer.position.z - older.position.z;
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

bool IsFinite(const Pose& pose) noexcept {
    return std::isfinite(pose.position.x) && std::isfinite(pose.position.y) &&
           std::isfinite(pose.position.z) && std::isfinite(pose.pitch) &&
           std::isfinite(pose.yaw) && std::isfinite(pose.roll);
}

bool NearlyEqual(const Pose& left, const Pose& right, const float epsilon) noexcept {
    return std::abs(left.position.x - right.position.x) <= epsilon &&
           std::abs(left.position.y - right.position.y) <= epsilon &&
           std::abs(left.position.z - right.position.z) <= epsilon &&
           std::abs(left.pitch - right.pitch) <= epsilon &&
           std::abs(left.yaw - right.yaw) <= epsilon &&
           std::abs(left.roll - right.roll) <= epsilon;
}

float OrientationDistance(const Pose& left, const Pose& right) noexcept {
    const Quaternion first = Normalize(FromEuler(left));
    const Quaternion second = Normalize(FromEuler(right));
    const float cosine = std::clamp(std::abs(Dot(first, second)), 0.0F, 1.0F);
    return 2.0F * std::acos(cosine);
}

bool Interpolate(const Pose& older,
                 const Pose& newer,
                 const double alpha,
                 Pose& result) noexcept {
    if (!IsFinite(older) || !IsFinite(newer) || !std::isfinite(alpha) ||
        alpha < 0.0 || alpha > 1.0) {
        return false;
    }
    const float t = static_cast<float>(alpha);
    result.position = {
        older.position.x + (newer.position.x - older.position.x) * t,
        older.position.y + (newer.position.y - older.position.y) * t,
        older.position.z + (newer.position.z - older.position.z) * t,
    };
    ToEuler(Slerp(Normalize(FromEuler(older)), Normalize(FromEuler(newer)), t), result);
    return IsFinite(result);
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

void History::Append(const std::uint64_t serial, const Pose& pose) noexcept {
    if (count_ < kCapacity) {
        snapshots_[(first_ + count_) % kCapacity] = {serial, pose};
        ++count_;
    } else {
        snapshots_[first_] = {serial, pose};
        first_ = (first_ + 1) % kCapacity;
    }
}

PublishResult History::Publish(const std::uint64_t serial,
                               const std::uint64_t clockGeneration,
                               const Pose& pose) noexcept {
    if (!IsFinite(pose)) {
        Reset();
        return {PublishKind::invalid};
    }
    if (!hasGeneration_ || generation_ != clockGeneration) {
        Reset();
        generation_ = clockGeneration;
        hasGeneration_ = true;
        Append(serial, pose);
        return {PublishKind::resetGeneration};
    }
    if (count_ != 0) {
        const Snapshot& latest = At(count_ - 1);
        if (serial < latest.serial) {
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, pose);
            return {PublishKind::resetNonMonotonic};
        }
        if (serial == latest.serial) {
            if (NearlyEqual(latest.pose, pose)) {
                return {PublishKind::unchanged};
            }
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, pose);
            return {PublishKind::resetSameTickMutation};
        }
        const std::uint64_t delta = serial - latest.serial;
        if (delta > clock::PresentationClock::kMaximumBridgedSerialDelta ||
            IsDiscontinuous(latest.pose, pose, delta)) {
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, pose);
            return {PublishKind::resetDiscontinuity};
        }
    }
    Append(serial, pose);
    return {PublishKind::appended};
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
        if (candidate.serial == clockSample.olderSerial) {
            older = &candidate;
        }
        if (candidate.serial == clockSample.newerSerial) {
            newer = &candidate;
        }
    }
    if (older == nullptr || newer == nullptr) {
        return {SampleKind::snapshotsMissing, {}};
    }

    Pose interpolated{};
    if (!Interpolate(older->pose, newer->pose, clockSample.alpha, interpolated)) {
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

} // namespace atdd::camera
