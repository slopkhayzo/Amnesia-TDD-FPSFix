#include "particle_interpolation.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void TestEndpointsAndMidpoint() {
    const atdd::particle::Vec3 older{-2.0F, 4.0F, 8.0F};
    const atdd::particle::Vec3 newer{2.0F, 8.0F, -4.0F};
    atdd::particle::Vec3 result{};

    Require(atdd::particle::Interpolate(older, newer, 0.0, result) &&
                result.x == older.x && result.y == older.y && result.z == older.z,
            "alpha zero did not preserve the previous fixed state");
    Require(atdd::particle::Interpolate(older, newer, 1.0, result) &&
                result.x == newer.x && result.y == newer.y && result.z == newer.z,
            "alpha one did not preserve the current fixed state");
    Require(atdd::particle::Interpolate(older, newer, 0.5, result) &&
                std::abs(result.x) < 1.0e-6F &&
                std::abs(result.y - 6.0F) < 1.0e-6F &&
                std::abs(result.z - 2.0F) < 1.0e-6F,
            "particle midpoint was not linearly interpolated");
}

void TestInvalidInputsFailClosed() {
    const atdd::particle::Vec3 valid{};
    atdd::particle::Vec3 invalid{};
    invalid.y = std::numeric_limits<float>::quiet_NaN();
    atdd::particle::Vec3 result{};

    Require(!atdd::particle::Interpolate(invalid, valid, 0.5, result),
            "non-finite particle endpoint was accepted");
    Require(!atdd::particle::Interpolate(valid, valid, -0.01, result),
            "negative interpolation alpha was accepted");
    Require(!atdd::particle::Interpolate(valid, valid, 1.01, result),
            "alpha above one was accepted");
}

} // namespace

int main() {
    TestEndpointsAndMidpoint();
    TestInvalidInputsFailClosed();
    std::cout << "particle_interpolation_tests: PASS\n";
    return EXIT_SUCCESS;
}
