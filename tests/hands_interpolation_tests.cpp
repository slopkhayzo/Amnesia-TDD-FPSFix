#include "hands_interpolation.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

constexpr float kPi = 3.14159265358979323846F;

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

atdd::hands::Matrix MakeTransform(const float x, const float yaw = 0.0F) {
    atdd::hands::Matrix matrix = atdd::hands::Identity();
    matrix.values[0] = std::cos(yaw);
    matrix.values[2] = std::sin(yaw);
    matrix.values[8] = -std::sin(yaw);
    matrix.values[10] = std::cos(yaw);
    matrix.values[3] = x;
    matrix.values[7] = x * 2.0F;
    matrix.values[11] = -x;
    return matrix;
}

atdd::clock::Sample Pair(const std::uint64_t generation,
                         const std::uint64_t older,
                         const std::uint64_t newer,
                         const double alpha) {
    atdd::clock::Sample sample{};
    sample.kind = atdd::clock::SampleKind::interpolated;
    sample.generation = generation;
    sample.olderSerial = older;
    sample.newerSerial = newer;
    sample.alpha = alpha;
    return sample;
}

void TestRigidInterpolationAndShortestArc() {
    const auto older = MakeTransform(0.0F, 179.0F * kPi / 180.0F);
    const auto newer = MakeTransform(2.0F, -179.0F * kPi / 180.0F);
    atdd::hands::Matrix midpoint{};
    Require(atdd::hands::Interpolate(older, newer, 0.5, midpoint),
            "valid rigid transforms were rejected");
    Require(std::abs(midpoint.values[3] - 1.0F) < 1.0e-5F &&
                std::abs(midpoint.values[7] - 2.0F) < 1.0e-5F &&
                std::abs(midpoint.values[11] + 1.0F) < 1.0e-5F,
            "translation was not interpolated linearly");
    const auto expected = MakeTransform(1.0F, kPi);
    Require(atdd::hands::OrientationDistance(midpoint, expected) < 1.0e-3F,
            "rotation did not take the shortest quaternion arc");
}

void TestInverseAndComposition() {
    const auto matrix = MakeTransform(3.0F, 0.7F);
    atdd::hands::Matrix inverse{};
    atdd::hands::Matrix product{};
    Require(atdd::hands::InverseRigid(matrix, inverse), "rigid inverse failed");
    Require(atdd::hands::Multiply(matrix, inverse, product), "matrix multiply failed");
    Require(atdd::hands::NearlyEqual(product, atdd::hands::Identity(), 2.0e-5F),
            "rigid inverse did not compose to identity");
}

void TestHistoryPairingAndResets() {
    atdd::hands::History history;
    Require(history.Publish(10, 3, MakeTransform(0.0F)) ==
                atdd::hands::PublishKind::resetGeneration,
            "first matrix did not establish generation");
    Require(history.Publish(11, 3, MakeTransform(2.0F)) ==
                atdd::hands::PublishKind::appended,
            "second matrix did not append");
    const auto sample = history.Evaluate(Pair(3, 10, 11, 0.25));
    Require(sample.kind == atdd::hands::SampleKind::applied &&
                std::abs(sample.matrix.values[3] - 0.5F) < 1.0e-5F,
            "history did not evaluate its exact serial pair");
    Require(history.Publish(11, 3, MakeTransform(2.1F)) ==
                atdd::hands::PublishKind::resetSameTickMutation,
            "same-tick mutation did not reset");
    Require(history.Publish(12, 3, MakeTransform(20.0F)) ==
                atdd::hands::PublishKind::resetDiscontinuity,
            "large root teleport did not reset");
}

void TestInvalidScaleFailsClosed() {
    auto scaled = MakeTransform(0.0F);
    scaled.values[0] = 2.0F;
    atdd::hands::Matrix output{};
    Require(!atdd::hands::IsRigid(scaled), "scaled matrix passed rigid validation");
    Require(!atdd::hands::Interpolate(scaled, MakeTransform(1.0F), 0.5, output),
            "scaled endpoint was interpolated");
}

} // namespace

int main() {
    TestRigidInterpolationAndShortestArc();
    TestInverseAndComposition();
    TestHistoryPairingAndResets();
    TestInvalidScaleFailsClosed();
    std::cout << "hands interpolation tests passed\n";
    return EXIT_SUCCESS;
}
