#include "particle_interpolation.h"

#include <cmath>

namespace atdd::particle {

bool IsFinite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool Interpolate(const Vec3& older,
                 const Vec3& newer,
                 const double alpha,
                 Vec3& result) noexcept {
    if (!IsFinite(older) || !IsFinite(newer) || !std::isfinite(alpha) ||
        alpha < 0.0 || alpha > 1.0) {
        return false;
    }

    const float factor = static_cast<float>(alpha);
    result = {
        older.x + (newer.x - older.x) * factor,
        older.y + (newer.y - older.y) * factor,
        older.z + (newer.z - older.z) * factor,
    };
    return IsFinite(result);
}

} // namespace atdd::particle
