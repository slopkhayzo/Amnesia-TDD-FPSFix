#include "pose_interpolation.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

atdd::hands::Matrix Translation(const float x) {
    auto matrix = atdd::hands::Identity();
    matrix.values[3] = x;
    return matrix;
}

atdd::pose::Pose MakePose(const float base, const std::size_t count = 2) {
    atdd::pose::Pose pose{};
    pose.boneCount = count;
    for (std::size_t index = 0; index < count; ++index) {
        pose.bones[index] = Translation(base + static_cast<float>(index));
    }
    return pose;
}

atdd::clock::Sample Pair(const std::uint64_t older,
                         const std::uint64_t newer,
                         const double alpha) {
    atdd::clock::Sample sample{};
    sample.kind = atdd::clock::SampleKind::interpolated;
    sample.generation = 7;
    sample.olderSerial = older;
    sample.newerSerial = newer;
    sample.alpha = alpha;
    return sample;
}

void TestAtomicPosePair() {
    atdd::pose::History history;
    Require(history.Publish(4, 7, MakePose(0.0F)) ==
                atdd::pose::PublishKind::resetGeneration,
            "first pose did not establish its generation");
    Require(history.Publish(5, 7, MakePose(2.0F)) ==
                atdd::pose::PublishKind::appended,
            "second pose did not append");
    const auto sampled = history.Evaluate(Pair(4, 5, 0.5));
    Require(sampled.kind == atdd::pose::SampleKind::applied &&
                sampled.pose.boneCount == 2 &&
                std::abs(sampled.pose.bones[0].values[3] - 1.0F) < 1.0e-5F &&
                std::abs(sampled.pose.bones[1].values[3] - 2.0F) < 1.0e-5F,
            "bones were not sampled from one atomic serial pair");
}

void TestCountAndMutationReset() {
    atdd::pose::History history;
    (void)history.Publish(1, 7, MakePose(0.0F));
    Require(history.Publish(2, 7, MakePose(1.0F, 3)) ==
                atdd::pose::PublishKind::resetBoneCount,
            "bone-count change did not reset history");
    Require(history.Publish(2, 7, MakePose(1.1F, 3)) ==
                atdd::pose::PublishKind::resetSameTickMutation,
            "same-tick pose mutation did not reset history");
}

void TestInvalidAndDiscontinuousPose() {
    atdd::pose::History history;
    auto invalid = MakePose(0.0F);
    invalid.bones[1].values[15] = 0.0F;
    Require(history.Publish(1, 7, invalid) == atdd::pose::PublishKind::invalid,
            "scaled bone palette was accepted");
    (void)history.Publish(1, 7, MakePose(0.0F));
    Require(history.Publish(2, 7, MakePose(20.0F)) ==
                atdd::pose::PublishKind::resetDiscontinuity,
            "large bone-palette jump did not reset history");
}

void TestAffineSkinMatrixInterpolation() {
    auto older = MakePose(0.0F, 1);
    auto newer = MakePose(2.0F, 1);
    older.bones[0].values[0] = 0.9F;
    older.bones[0].values[1] = 0.1F;
    newer.bones[0].values[0] = 1.1F;
    newer.bones[0].values[1] = 0.2F;
    atdd::pose::Pose result{};
    Require(atdd::pose::Interpolate(older, newer, 0.5, result),
            "valid affine skin matrices were rejected");
    Require(std::abs(result.bones[0].values[0] - 1.0F) < 1.0e-5F &&
                std::abs(result.bones[0].values[1] - 0.15F) < 1.0e-5F &&
                std::abs(result.bones[0].values[3] - 1.0F) < 1.0e-5F,
            "affine skin matrix was not interpolated element-wise");
}

} // namespace

int main() {
    TestAtomicPosePair();
    TestCountAndMutationReset();
    TestInvalidAndDiscontinuousPose();
    TestAffineSkinMatrixInterpolation();
    std::cout << "pose interpolation tests passed\n";
    return EXIT_SUCCESS;
}
