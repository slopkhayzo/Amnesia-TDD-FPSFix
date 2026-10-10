#include "enemy_pose_runtime.h"

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
    std::array<std::uint8_t, 0x900> enemy{};
    std::array<std::uint8_t, 0x400> mesh{};
    std::array<atdd::hands::Matrix, 4> bones{};

    explicit Fixture(const std::int32_t id) {
        void* meshPointer = mesh.data();
        const std::int32_t grunt = 0;
        const std::uint8_t active = 1;
        auto* begin = bones.data();
        auto* end = bones.data() + 2;
        auto* capacity = bones.data() + bones.size();
        Write(enemy, 0x40, id);
        Write(enemy, 0x54C, meshPointer);
        Write(enemy, 0x82C, grunt);
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

struct RagdollFixture {
    std::array<std::uint8_t, 0x500> prop{};
    std::array<std::uint8_t, 0x400> mesh{};
    std::array<std::uint8_t, 0x400> subMesh{};
    std::array<void*, 1> subMeshes{};
    std::array<void*, 3> bodies{};
    std::array<std::uint8_t, 2> bodyStorage{};
    std::array<atdd::hands::Matrix, 6> bones{};

    explicit RagdollFixture(const std::int32_t id) {
        void* meshPointer = mesh.data();
        const std::int32_t propType = 0;
        const std::uint8_t active = 1;
        const std::uint8_t staticPhysics = 0;
        bodies[0] = &bodyStorage[0];
        bodies[1] = &bodyStorage[1];
        auto* bodiesBegin = bodies.data();
        auto* bodiesEnd = bodies.data() + 2;
        auto* bonesBegin = bones.data();
        auto* bonesEnd = bones.data() + 4;
        auto* bonesCapacity = bones.data() + bones.size();
        subMeshes[0] = subMesh.data();
        auto* subMeshesBegin = subMeshes.data();
        auto* subMeshesEnd = subMeshes.data() + subMeshes.size();
        const std::int32_t neverRendered = -1;
        Write(prop, 0x40, id);
        Write(prop, 0x119, staticPhysics);
        Write(prop, 0x260, bodiesBegin);
        Write(prop, 0x264, bodiesEnd);
        Write(prop, 0x340, meshPointer);
        Write(prop, 0x484, propType);
        Write(mesh, 0x24, active);
        Write(mesh, 0x30C, subMeshesBegin);
        Write(mesh, 0x310, subMeshesEnd);
        Write(mesh, 0x380, bonesBegin);
        Write(mesh, 0x384, bonesEnd);
        Write(mesh, 0x388, bonesCapacity);
        Write(subMesh, 0x344, neverRendered);
    }

    void Set(const float base) {
        for (std::size_t index = 0; index < 4; ++index) {
            bones[index] = Translation(base + static_cast<float>(index));
        }
    }

    void MarkRendered(const std::int32_t frame) {
        Write(subMesh, 0x344, frame);
    }
};

std::uint32_t g_updateCalls = 0;
void __fastcall FakeUpdateGraphics(void*, void*, float) {
    ++g_updateCalls;
}

void Commit(atdd::enemy_pose::Runtime& runtime,
            Fixture& fixture,
            const std::uint64_t serial) {
    Require(runtime.StageEnemy(fixture.enemy.data(), serial) ==
                atdd::enemy_pose::StageResult::stagedEnemy,
            "Grunt pose target was not staged");
    runtime.CommitCompletedTick(serial, 1);
}

void TestMultipleGruntsOverrideAndRestore() {
    Fixture first(10);
    Fixture second(11);
    atdd::enemy_pose::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&FakeUpdateGraphics)});

    first.Set(0.0F);
    second.Set(10.0F);
    Require(runtime.StageEnemy(first.enemy.data(), 1) ==
                atdd::enemy_pose::StageResult::stagedEnemy,
            "first Grunt was not staged");
    Require(runtime.StageEnemy(second.enemy.data(), 1) ==
                atdd::enemy_pose::StageResult::stagedEnemy,
            "second Grunt was not staged");
    runtime.CommitCompletedTick(1, 1);
    auto batch = runtime.BeginRender(Pair(1, 2, 0.5), 1, 0.0F);
    Require(batch.targets == 2 && batch.captured == 2 && batch.applied == 0,
            "first Grunt palettes did not establish independent histories");
    Require(runtime.EndRender().restoreFailed == 0,
            "empty first render failed restoration");

    first.Set(2.0F);
    second.Set(14.0F);
    Require(runtime.StageEnemy(first.enemy.data(), 2) ==
                atdd::enemy_pose::StageResult::stagedEnemy,
            "first Grunt was not restaged");
    Require(runtime.StageEnemy(second.enemy.data(), 2) ==
                atdd::enemy_pose::StageResult::stagedEnemy,
            "second Grunt was not restaged");
    runtime.CommitCompletedTick(2, 1);
    batch = runtime.BeginRender(Pair(1, 2, 0.5), 2, 0.0F);
    Require(batch.targets == 2 && batch.applied == 2,
            "both Grunt palettes were not interpolated");
    Require(std::abs(first.bones[0].values[3] - 1.0F) < 1.0e-5F &&
                std::abs(second.bones[0].values[3] - 12.0F) < 1.0e-5F,
            "Grunt palettes did not use their own midpoint histories");
    Require(runtime.EndRender().restoreFailed == 0,
            "Grunt palettes failed restoration");
    Require(std::abs(first.bones[0].values[3] - 2.0F) < 1.0e-5F &&
                std::abs(second.bones[0].values[3] - 14.0F) < 1.0e-5F,
            "authoritative Grunt palettes leaked across the render boundary");
}

void TestLifecycleAndFailClosedValidation() {
    Fixture fixture(20);
    atdd::enemy_pose::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&FakeUpdateGraphics)});
    fixture.Set(0.0F);
    Commit(runtime, fixture, 1);
    (void)runtime.BeginRender(Pair(1, 2, 0.5), 1, 0.0F);
    (void)runtime.EndRender();

    fixture.Set(2.0F);
    Commit(runtime, fixture, 2);
    auto batch = runtime.BeginRender(Pair(1, 2, 0.5), 2, 0.0F);
    Require(batch.applied == 1, "Grunt did not become interpolation-ready");
    (void)runtime.EndRender();

    const std::int32_t replacementId = 21;
    Write(fixture.enemy, 0x40, replacementId);
    fixture.Set(4.0F);
    Commit(runtime, fixture, 3);
    batch = runtime.BeginRender(Pair(2, 3, 0.5), 3, 0.0F);
    Require(batch.applied == 0 && batch.identityChanged != 0,
            "enemy ID reuse crossed Grunt pose histories");
    (void)runtime.EndRender();

    fixture.bones[0].values[15] = 0.0F;
    Commit(runtime, fixture, 4);
    batch = runtime.BeginRender(Pair(3, 4, 0.5), 4, 0.0F);
    Require(batch.invalid == 1 && batch.applied == 0,
            "malformed Grunt palette did not fail closed");
    (void)runtime.EndRender();

    runtime.CommitCompletedTick(5, 1);
    batch = runtime.BeginRender(Pair(4, 5, 0.5), 5, 0.0F);
    Require(batch.targets == 0,
            "unstaged Grunt survived the completed-tick lifecycle boundary");
}

void TestTypeGate() {
    Fixture fixture(30);
    const std::int32_t nonGrunt = 1;
    Write(fixture.enemy, 0x82C, nonGrunt);
    atdd::enemy_pose::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&FakeUpdateGraphics)});
    Require(runtime.StageEnemy(fixture.enemy.data(), 1) ==
                atdd::enemy_pose::StageResult::ineligible,
            "non-Grunt enemy entered the pose registry");
}

void TestRagdollPropOverrideAndRestore() {
    RagdollFixture fixture(40);
    atdd::enemy_pose::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&FakeUpdateGraphics)});

    fixture.Set(0.0F);
    fixture.MarkRendered(1);
    Require(runtime.StageRagdollProp(fixture.prop.data(), fixture.mesh.data(), 40,
                                     2, false, 1) ==
                atdd::enemy_pose::StageResult::stagedRagdoll,
            "multi-body skeletal prop was not staged as a ragdoll");
    runtime.CommitCompletedTick(1, 1);
    auto batch = runtime.BeginRender(Pair(1, 2, 0.5), 1, 0.0F);
    Require(batch.targets == 1 && batch.captured == 1 && batch.applied == 0,
            "first ragdoll palette did not establish history");
    (void)runtime.EndRender();

    fixture.Set(2.0F);
    fixture.MarkRendered(2);
    Require(runtime.StageRagdollProp(fixture.prop.data(), fixture.mesh.data(), 40,
                                     2, false, 2) ==
                atdd::enemy_pose::StageResult::stagedRagdoll,
            "ragdoll was not restaged");
    runtime.CommitCompletedTick(2, 1);
    batch = runtime.BeginRender(Pair(1, 2, 0.5), 2, 0.0F);
    Require(batch.applied == 1 &&
                std::abs(fixture.bones[0].values[3] - 1.0F) < 1.0e-5F,
            "ragdoll bone palette was not interpolated at the midpoint");
    Require(runtime.EndRender().restoreFailed == 0 &&
                std::abs(fixture.bones[0].values[3] - 2.0F) < 1.0e-5F,
            "authoritative ragdoll palette was not restored");
    const std::uint32_t updatesAfterCapture = g_updateCalls;
    fixture.MarkRendered(3);
    batch = runtime.BeginRender(Pair(1, 2, 0.75), 2, 0.0F);
    Require(batch.applied == 1 && g_updateCalls == updatesAfterCapture,
            "ragdoll repeated its graphics update within one simulation tick");
    (void)runtime.EndRender();

    fixture.Set(4.0F);
    fixture.MarkRendered(4);
    Require(runtime.StageRagdollProp(fixture.prop.data(), fixture.mesh.data(), 40,
                                     2, true, 3) ==
                atdd::enemy_pose::StageResult::stagedRagdoll,
            "first sleeping tick did not retain the ragdoll's final endpoint");
    runtime.CommitCompletedTick(3, 1);
    batch = runtime.BeginRender(Pair(2, 3, 0.5), 3, 0.0F);
    Require(batch.applied == 1,
            "ragdoll final sleeping endpoint was not interpolated");
    (void)runtime.EndRender();
    Require(runtime.StageRagdollProp(fixture.prop.data(), fixture.mesh.data(), 40,
                                     2, true, 4) ==
                atdd::enemy_pose::StageResult::ineligible,
            "settled sleeping ragdoll remained in the presentation path");

    Require(runtime.StageRagdollProp(fixture.prop.data(), fixture.mesh.data(), 40,
                                     1, false, 5) ==
                atdd::enemy_pose::StageResult::ineligible,
            "single-body prop entered the ragdoll pose path");
}

void TestRagdollVisibilityGate() {
    RagdollFixture fixture(50);
    atdd::enemy_pose::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&FakeUpdateGraphics)});

    fixture.Set(0.0F);
    Require(runtime.StageRagdollProp(fixture.prop.data(), fixture.mesh.data(), 50,
                                     2, false, 1) ==
                atdd::enemy_pose::StageResult::stagedRagdoll,
            "hidden ragdoll was not registered");
    runtime.CommitCompletedTick(1, 1);
    const std::uint32_t updatesBeforeHidden = g_updateCalls;
    auto batch = runtime.BeginRender(Pair(1, 2, 0.5), 1, 0.0F);
    Require(batch.targets == 0 && batch.visibilitySkipped == 1 &&
                g_updateCalls == updatesBeforeHidden,
            "never-rendered ragdoll performed pose work");
    (void)runtime.EndRender();

    fixture.MarkRendered(10);
    batch = runtime.BeginRender(Pair(1, 2, 0.5), 1, 0.0F);
    Require(batch.targets == 1 && batch.captured == 1 &&
                batch.visibilitySkipped == 0,
            "newly visible ragdoll did not enter the pose path");
    (void)runtime.EndRender();

    // Two missed presentations are retained as an occlusion grace window;
    // the third unchanged stamp must retire all palette work.
    batch = runtime.BeginRender(Pair(1, 2, 0.5), 1, 0.0F);
    Require(batch.targets == 1, "first ragdoll visibility grace frame was lost");
    (void)runtime.EndRender();
    batch = runtime.BeginRender(Pair(1, 2, 0.5), 1, 0.0F);
    Require(batch.targets == 1, "second ragdoll visibility grace frame was lost");
    (void)runtime.EndRender();
    batch = runtime.BeginRender(Pair(1, 2, 0.5), 1, 0.0F);
    Require(batch.targets == 0 && batch.visibilitySkipped == 1,
            "offscreen ragdoll survived its visibility grace window");
    (void)runtime.EndRender();

    fixture.MarkRendered(11);
    batch = runtime.BeginRender(Pair(1, 2, 0.5), 1, 0.0F);
    Require(batch.targets == 1 && batch.visibilitySkipped == 0,
            "visible-again ragdoll did not resume pose work");
    (void)runtime.EndRender();
}

} // namespace

int main() {
    TestMultipleGruntsOverrideAndRestore();
    TestLifecycleAndFailClosedValidation();
    TestTypeGate();
    TestRagdollPropOverrideAndRestore();
    TestRagdollVisibilityGate();
    Require(g_updateCalls != 0, "scene-boundary graphics update was never invoked");
    std::cout << "enemy pose runtime tests passed\n";
    return EXIT_SUCCESS;
}
