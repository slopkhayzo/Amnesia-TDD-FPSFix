#include "pose_runtime.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template <typename T, std::size_t Size>
void Write(std::array<std::uint8_t, Size>& bytes,
           const std::size_t offset,
           const T& value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

atdd::hands::Matrix Translation(const float x) {
    auto matrix = atdd::hands::Identity();
    matrix.values[3] = x;
    return matrix;
}

atdd::clock::Sample Pair(const std::uint64_t older,
                         const std::uint64_t newer,
                         const double alpha) {
    atdd::clock::Sample sample{};
    sample.kind = atdd::clock::SampleKind::interpolated;
    sample.generation = 1;
    sample.olderSerial = older;
    sample.newerSerial = newer;
    sample.alpha = alpha;
    return sample;
}

struct Fixture {
    std::array<std::uint8_t, 0x100> hands{};
    std::array<std::uint8_t, 0x400> mesh{};
    std::array<atdd::hands::Matrix, 4> bones{};

    Fixture() {
        void* meshPointer = mesh.data();
        const std::uint8_t active = 1;
        auto* begin = bones.data();
        auto* end = bones.data() + 2;
        auto* capacity = bones.data() + bones.size();
        Write(hands, 0x44, meshPointer);
        Write(mesh, 0x24, active);
        Write(mesh, 0x380, begin);
        Write(mesh, 0x384, end);
        Write(mesh, 0x388, capacity);
    }

    void Set(const float base) {
        bones[0] = Translation(base);
        bones[1] = Translation(base + 1.0F);
    }
};

void __fastcall FakeUpdateGraphics(void*, void*, float) {}

void Commit(atdd::pose::Runtime& runtime, Fixture& fixture,
            const std::uint64_t serial) {
    Require(runtime.StageHands(fixture.hands.data(), serial) ==
                atdd::pose::StageResult::staged,
            "hands pose target was not staged");
    runtime.CommitCompletedTick(serial, 1);
}

void TestRenderOverrideAndRestore() {
    Fixture fixture;
    atdd::pose::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&FakeUpdateGraphics)});

    fixture.Set(0.0F);
    Commit(runtime, fixture, 1);
    Require(runtime.BeginRender(Pair(1, 2, 0.5), 1, 0.0F) ==
                atdd::pose::UpdateResult::identityChanged,
            "first palette did not establish identity");
    Require(runtime.EndRender(), "empty first render failed restoration");

    fixture.Set(2.0F);
    Commit(runtime, fixture, 2);
    Require(runtime.BeginRender(Pair(1, 2, 0.5), 2, 0.0F) ==
                atdd::pose::UpdateResult::applied,
            "second palette was not interpolated");
    Require(std::abs(fixture.bones[0].values[3] - 1.0F) < 1.0e-5F &&
                std::abs(fixture.bones[1].values[3] - 2.0F) < 1.0e-5F,
            "render palette is not the expected midpoint");
    Require(runtime.OnMeshUpdated(fixture.mesh.data()) ==
                atdd::pose::UpdateResult::alreadyApplied,
            "second submesh call attempted to capture the override");
    Require(runtime.EndRender(), "authoritative palette was not restored");
    Require(std::abs(fixture.bones[0].values[3] - 2.0F) < 1.0e-5F &&
                std::abs(fixture.bones[1].values[3] - 3.0F) < 1.0e-5F,
            "render override leaked past scene render");
}

void TestIdentityAndInvalidReset() {
    Fixture fixture;
    atdd::pose::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&FakeUpdateGraphics)});
    fixture.Set(0.0F);
    Commit(runtime, fixture, 1);
    (void)runtime.BeginRender(Pair(1, 2, 0.5), 1, 0.0F);
    (void)runtime.EndRender();

    auto* begin = fixture.bones.data();
    auto* end = fixture.bones.data() + 3;
    Write(fixture.mesh, 0x380, begin);
    Write(fixture.mesh, 0x384, end);
    fixture.bones[2] = Translation(2.0F);
    Commit(runtime, fixture, 2);
    Require(runtime.BeginRender(Pair(1, 2, 0.5), 2, 0.0F) ==
                atdd::pose::UpdateResult::identityChanged,
            "bone-count change crossed pose histories");
    (void)runtime.EndRender();

    fixture.bones[1].values[15] = 0.0F;
    Commit(runtime, fixture, 3);
    Require(runtime.BeginRender(Pair(2, 3, 0.5), 3, 0.0F) ==
                atdd::pose::UpdateResult::invalid,
            "non-rigid palette did not fail closed");
    std::size_t invalidBone = 0;
    atdd::hands::Matrix invalidMatrix{};
    Require(runtime.TakeInvalidMatrixDiagnostic(invalidBone, invalidMatrix) &&
                invalidBone == 1 && invalidMatrix.values[15] == 0.0F,
            "invalid palette did not expose its one-shot diagnostic");
    Require(!runtime.TakeInvalidMatrixDiagnostic(invalidBone, invalidMatrix),
            "invalid matrix diagnostic was not consumed");
}

} // namespace

int main() {
    TestRenderOverrideAndRestore();
    TestIdentityAndInvalidReset();
    std::cout << "pose runtime tests passed\n";
    return EXIT_SUCCESS;
}
