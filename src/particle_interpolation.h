#pragma once

namespace atdd::particle {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

[[nodiscard]] bool IsFinite(const Vec3& value) noexcept;
[[nodiscard]] bool Interpolate(const Vec3& older,
                               const Vec3& newer,
                               double alpha,
                               Vec3& result) noexcept;

} // namespace atdd::particle
