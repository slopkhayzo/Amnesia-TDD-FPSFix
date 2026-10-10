#include "hands_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

constexpr std::size_t kWorldOffset = 0x100;
constexpr std::size_t kParentOffset = 0x180;

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

atdd::hands::Matrix MakeTranslation(const float x) {
    auto matrix = atdd::hands::Identity();
    matrix.values[3] = x;
    return matrix;
}

atdd::hands::Matrix* __fastcall FakeGetWorldMatrix(void* entity) {
    return reinterpret_cast<atdd::hands::Matrix*>(
        static_cast<std::uint8_t*>(entity) + kWorldOffset);
}

void* __fastcall FakeGetEntityParent(void* entity) {
    void* parent = nullptr;
    std::memcpy(&parent, static_cast<std::uint8_t*>(entity) + kParentOffset,
                sizeof(parent));
    return parent;
}

atdd::clock::Sample Pair(const double alpha) {
    atdd::clock::Sample sample{};
    sample.kind = atdd::clock::SampleKind::interpolated;
    sample.generation = 1;
    sample.olderSerial = 1;
    sample.newerSerial = 2;
    sample.alpha = alpha;
    return sample;
}

atdd::clock::Sample Pair(const std::uint64_t older,
                         const std::uint64_t newer,
                         const double alpha) {
    auto sample = Pair(alpha);
    sample.olderSerial = older;
    sample.newerSerial = newer;
    return sample;
}

void TestRenderFacingDeltaOnlyTouchesHands() {
    std::array<std::uint8_t, 0x200> helper{};
    std::array<std::uint8_t, 0x200> entity{};
    std::array<std::uint8_t, 0x200> handObject{};
    std::array<std::uint8_t, 0x200> handObjectEntity{};
    std::array<std::uint8_t, 0x400> handsSubMesh{};
    std::array<std::uint8_t, 0x400> handObjectSubMesh{};
    std::array<std::uint8_t, 0x400> otherSubMesh{};
    void* entityPointer = entity.data();
    void* handObjectPointer = handObject.data();
    void* handObjectEntityPointer = handObjectEntity.data();
    std::array<void*, 1> handObjects{handObjectPointer};
    const std::uint32_t state = 3;
    const std::uint8_t active = 1;
    Write(helper, 0x44, entityPointer);
    Write(helper, 0x5C, handObjectPointer);
    void* handObjectsBegin = handObjects.data();
    void* handObjectsEnd = handObjects.data() + handObjects.size();
    Write(helper, 0x60, handObjectsBegin);
    Write(helper, 0x64, handObjectsEnd);
    Write(helper, 0x88, state);
    Write(handObject, 0xF4, handObjectEntityPointer);
    Write(entity, 0x24, active);
    Write(handObjectEntity, 0x24, active);
    Write(handsSubMesh, 0x370, entityPointer);
    Write(handObjectSubMesh, 0x370, handObjectEntityPointer);

    atdd::hands::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&FakeGetWorldMatrix)});

    Write(entity, kWorldOffset, MakeTranslation(0.0F));
    Require(runtime.Stage(helper.data()) == atdd::hands::FrameResult::staged,
            "first hands root was not staged");
    Require(runtime.CommitCompletedTick(1, 1) ==
                atdd::hands::FrameResult::identityChanged,
            "first hands identity was not established");

    Write(entity, kWorldOffset, MakeTranslation(2.0F));
    Require(runtime.Stage(helper.data()) == atdd::hands::FrameResult::staged,
            "second hands root was not staged");
    Require(runtime.CommitCompletedTick(2, 1) == atdd::hands::FrameResult::captured,
            "second hands root was not captured");
    const atdd::hands::Matrix presentationCorrection = MakeTranslation(10.0F);
    Require(runtime.SetPresentationCorrection(&presentationCorrection),
            "valid presentation correction was rejected");
    Require(runtime.BeginRender(Pair(0.5)) == atdd::hands::FrameResult::renderReady,
            "hands render override was not prepared");

    const atdd::hands::Matrix original = MakeTranslation(3.0F);
    const atdd::hands::Matrix* result = nullptr;
    Require(runtime.OverrideModelMatrix(handsSubMesh.data(), &original, result) ==
                atdd::hands::OverrideResult::applied,
            "hands-owned submesh was not overridden");
    Require(result != &original && std::abs(result->values[3] - 12.0F) < 1.0e-5F,
            "camera presentation correction was not composed with the hands root delta");

    result = nullptr;
    Require(runtime.OverrideModelMatrix(handObjectSubMesh.data(), &original, result) ==
                atdd::hands::OverrideResult::appliedHandObject,
            "attached hand-object submesh was not overridden");
    Require(result != &original && std::abs(result->values[3] - 12.0F) < 1.0e-5F,
            "camera correction did not preserve the hand-object local offset");

    result = nullptr;
    Require(runtime.OverrideModelMatrix(otherSubMesh.data(), &original, result) ==
                atdd::hands::OverrideResult::notHands && result == &original,
            "non-hands submesh was modified");
    Require(runtime.EndRender() == 0, "hands render cleanup failed");
}

void TestHandObjectAttachmentsOverrideAndRestore() {
    std::array<std::uint8_t, 0x200> helper{};
    std::array<std::uint8_t, 0x200> entity{};
    std::array<std::uint8_t, 0x200> handObject{};
    std::array<std::uint8_t, 0x200> handObjectEntity{};
    std::array<std::uint8_t, 0x200> billboard{};
    std::array<std::uint8_t, 0x200> particle{};
    std::array<std::uint8_t, 0x200> lightOne{};
    std::array<std::uint8_t, 0x200> lightTwo{};
    std::array<std::uint8_t, 0x200> unrelatedParent{};

    void* entityPointer = entity.data();
    void* handObjectPointer = handObject.data();
    void* handObjectEntityPointer = handObjectEntity.data();
    void* unrelatedParentPointer = unrelatedParent.data();
    std::array<void*, 1> handObjects{handObjectPointer};
    std::array<void*, 1> billboards{billboard.data()};
    std::array<void*, 1> particles{particle.data()};
    std::array<void*, 2> lights{lightOne.data(), lightTwo.data()};
    void* handObjectsBegin = handObjects.data();
    void* handObjectsEnd = handObjects.data() + handObjects.size();
    void* billboardsBegin = billboards.data();
    void* billboardsEnd = billboards.data() + billboards.size();
    void* particlesBegin = particles.data();
    void* particlesEnd = particles.data() + particles.size();
    void* lightsBegin = lights.data();
    void* lightsEnd = lights.data() + lights.size();
    const std::uint8_t active = 1;

    Write(helper, 0x44, entityPointer);
    Write(helper, 0x60, handObjectsBegin);
    Write(helper, 0x64, handObjectsEnd);
    Write(entity, 0x24, active);
    Write(handObject, 0xF4, handObjectEntityPointer);
    Write(handObject, 0xF8, billboardsBegin);
    Write(handObject, 0xFC, billboardsEnd);
    Write(handObject, 0x108, particlesBegin);
    Write(handObject, 0x10C, particlesEnd);
    Write(handObject, 0x118, lightsBegin);
    Write(handObject, 0x11C, lightsEnd);
    Write(handObjectEntity, 0x24, active);
    Write(billboard, kParentOffset, handObjectEntityPointer);
    Write(particle, kParentOffset, unrelatedParentPointer);
    Write(lightOne, kParentOffset, handObjectEntityPointer);
    Write(lightTwo, kParentOffset, handObjectEntityPointer);

    atdd::hands::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&FakeGetWorldMatrix),
                       reinterpret_cast<void*>(&FakeGetEntityParent)});
    Write(entity, kWorldOffset, MakeTranslation(0.0F));
    (void)runtime.Stage(helper.data());
    (void)runtime.CommitCompletedTick(1, 1);
    Write(entity, kWorldOffset, MakeTranslation(2.0F));
    (void)runtime.Stage(helper.data());
    (void)runtime.CommitCompletedTick(2, 1);

    const auto attachmentWorld = MakeTranslation(5.0F);
    Write(billboard, kWorldOffset, attachmentWorld);
    Write(particle, kWorldOffset, attachmentWorld);
    Write(lightOne, kWorldOffset, attachmentWorld);
    Write(lightTwo, kWorldOffset, attachmentWorld);
    Require(runtime.BeginRender(Pair(0.5)) == atdd::hands::FrameResult::renderReady,
            "attachment render override was not prepared");
    const auto& batch = runtime.LastAttachmentBatch();
    Require(batch.candidates == 4 && batch.applied == 3 &&
                batch.parentMismatch == 1 && batch.invalid == 0 &&
                batch.capacityExhausted == 0,
            "attachment provenance counters were incorrect");
    Require(std::abs(FakeGetWorldMatrix(billboard.data())->values[3] - 4.0F) < 1.0e-5F &&
                std::abs(FakeGetWorldMatrix(lightOne.data())->values[3] - 4.0F) < 1.0e-5F &&
                std::abs(FakeGetWorldMatrix(lightTwo.data())->values[3] - 4.0F) < 1.0e-5F,
            "attached billboard/light did not receive the hands root delta");
    Require(std::abs(FakeGetWorldMatrix(particle.data())->values[3] - 5.0F) < 1.0e-5F,
            "non-child particle was modified");
    Require(runtime.EndRender() == 0, "attachment restoration failed");
    Require(std::abs(FakeGetWorldMatrix(billboard.data())->values[3] - 5.0F) < 1.0e-5F &&
                std::abs(FakeGetWorldMatrix(lightOne.data())->values[3] - 5.0F) < 1.0e-5F &&
                std::abs(FakeGetWorldMatrix(lightTwo.data())->values[3] - 5.0F) < 1.0e-5F,
            "attachment world matrices were not restored byte-for-byte");
}

void TestHandsEntityChangeRejectsOldPair() {
    std::array<std::uint8_t, 0x200> helper{};
    std::array<std::uint8_t, 0x200> firstEntity{};
    std::array<std::uint8_t, 0x200> secondEntity{};
    void* firstEntityPointer = firstEntity.data();
    void* secondEntityPointer = secondEntity.data();
    const std::uint32_t state = 3;
    const std::uint8_t active = 1;
    Write(helper, 0x44, firstEntityPointer);
    Write(helper, 0x88, state);
    Write(firstEntity, 0x24, active);
    Write(firstEntity, kWorldOffset, MakeTranslation(0.0F));

    atdd::hands::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&FakeGetWorldMatrix)});
    (void)runtime.Stage(helper.data());
    (void)runtime.CommitCompletedTick(1, 1);
    Write(helper, 0x44, secondEntityPointer);
    Write(secondEntity, 0x24, active);
    Write(secondEntity, kWorldOffset, MakeTranslation(1.0F));
    (void)runtime.Stage(helper.data());
    Require(runtime.CommitCompletedTick(2, 1) ==
                atdd::hands::FrameResult::identityChanged,
            "hands entity change did not reset identity");
    Require(runtime.BeginRender(Pair(0.5)) == atdd::hands::FrameResult::snapshotsMissing,
            "render crossed a hands entity change");
}

void TestHolsterTracksActiveObjectNotCurrentPointer() {
    std::array<std::uint8_t, 0x200> helper{};
    std::array<std::uint8_t, 0x200> entity{};
    std::array<std::uint8_t, 0x200> handObject{};
    std::array<std::uint8_t, 0x200> handObjectEntity{};
    std::array<std::uint8_t, 0x400> handObjectSubMesh{};
    void* entityPointer = entity.data();
    void* handObjectPointer = handObject.data();
    void* handObjectEntityPointer = handObjectEntity.data();
    std::array<void*, 1> handObjects{handObjectPointer};
    void* begin = handObjects.data();
    void* end = handObjects.data() + handObjects.size();
    void* noCurrentObject = nullptr;
    const std::uint8_t active = 1;
    Write(helper, 0x44, entityPointer);
    Write(helper, 0x5C, handObjectPointer);
    Write(helper, 0x60, begin);
    Write(helper, 0x64, end);
    Write(entity, 0x24, active);
    Write(handObject, 0xF4, handObjectEntityPointer);
    Write(handObjectEntity, 0x24, active);
    Write(handObjectSubMesh, 0x370, handObjectEntityPointer);

    atdd::hands::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&FakeGetWorldMatrix)});
    Write(entity, kWorldOffset, MakeTranslation(0.0F));
    (void)runtime.Stage(helper.data());
    (void)runtime.CommitCompletedTick(1, 1);

    Write(helper, 0x5C, noCurrentObject);
    Write(entity, kWorldOffset, MakeTranslation(2.0F));
    (void)runtime.Stage(helper.data());
    Require(runtime.CommitCompletedTick(2, 1) == atdd::hands::FrameResult::captured,
            "holster current-object change incorrectly reset root history");
    Require(runtime.BeginRender(Pair(0.5)) == atdd::hands::FrameResult::renderReady,
            "holster current-object change lost the presentation pair");
    const atdd::hands::Matrix original = MakeTranslation(3.0F);
    const atdd::hands::Matrix* result = nullptr;
    Require(runtime.OverrideModelMatrix(handObjectSubMesh.data(), &original, result) ==
                atdd::hands::OverrideResult::appliedHandObject,
            "active holstering object was not tracked after current pointer cleared");
}

void TestHandsStateTransitionPreservesRootHistory() {
    std::array<std::uint8_t, 0x200> helper{};
    std::array<std::uint8_t, 0x200> entity{};
    std::array<std::uint8_t, 0x200> handObject{};
    void* entityPointer = entity.data();
    void* handObjectPointer = handObject.data();
    const std::uint8_t active = 1;
    const std::uint32_t drawState = 2;
    const std::uint32_t idleState = 3;
    Write(helper, 0x44, entityPointer);
    Write(helper, 0x5C, handObjectPointer);
    Write(helper, 0x88, drawState);
    Write(entity, 0x24, active);

    atdd::hands::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&FakeGetWorldMatrix)});
    Write(entity, kWorldOffset, MakeTranslation(0.0F));
    (void)runtime.Stage(helper.data());
    (void)runtime.CommitCompletedTick(1, 1);

    Write(helper, 0x88, idleState);
    Write(entity, kWorldOffset, MakeTranslation(2.0F));
    (void)runtime.Stage(helper.data());
    Require(runtime.CommitCompletedTick(2, 1) == atdd::hands::FrameResult::captured,
            "draw-to-idle state transition incorrectly reset root history");
    Require(runtime.BeginRender(Pair(0.5)) == atdd::hands::FrameResult::renderReady,
            "draw-to-idle state transition lost the presentation pair");
}

void TestHandObjectFinalModelHistoryIncludesLocalSway() {
    std::array<std::uint8_t, 0x200> helper{};
    std::array<std::uint8_t, 0x200> entity{};
    std::array<std::uint8_t, 0x200> handObject{};
    std::array<std::uint8_t, 0x200> handObjectEntity{};
    std::array<std::uint8_t, 0x400> handObjectSubMesh{};
    void* entityPointer = entity.data();
    void* handObjectPointer = handObject.data();
    void* handObjectEntityPointer = handObjectEntity.data();
    std::array<void*, 1> handObjects{handObjectPointer};
    void* begin = handObjects.data();
    void* end = handObjects.data() + handObjects.size();
    const std::uint8_t active = 1;
    Write(helper, 0x44, entityPointer);
    Write(helper, 0x60, begin);
    Write(helper, 0x64, end);
    Write(entity, 0x24, active);
    Write(handObject, 0xF4, handObjectEntityPointer);
    Write(handObjectEntity, 0x24, active);
    Write(handObjectSubMesh, 0x370, handObjectEntityPointer);

    atdd::hands::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(&FakeGetWorldMatrix)});
    Write(entity, kWorldOffset, MakeTranslation(0.0F));
    (void)runtime.Stage(helper.data());
    (void)runtime.CommitCompletedTick(1, 1);
    Write(entity, kWorldOffset, MakeTranslation(2.0F));
    (void)runtime.Stage(helper.data());
    (void)runtime.CommitCompletedTick(2, 1);

    Require(runtime.BeginRender(Pair(1, 2, 0.5)) ==
                atdd::hands::FrameResult::renderReady,
            "first hand-object history capture was not render-ready");
    const atdd::hands::Matrix firstObject = MakeTranslation(10.0F);
    const atdd::hands::Matrix* result = nullptr;
    Require(runtime.OverrideModelMatrix(handObjectSubMesh.data(), &firstObject, result) ==
                atdd::hands::OverrideResult::appliedHandObject,
            "single hand-object endpoint did not use the safe root fallback");
    (void)runtime.EndRender();

    Write(entity, kWorldOffset, MakeTranslation(4.0F));
    (void)runtime.Stage(helper.data());
    (void)runtime.CommitCompletedTick(3, 1);
    const auto correction = MakeTranslation(10.0F);
    Require(runtime.SetPresentationCorrection(&correction),
            "hand-object history correction was rejected");
    Require(runtime.BeginRender(Pair(2, 3, 0.5)) ==
                atdd::hands::FrameResult::renderReady,
            "second hand-object history capture was not render-ready");
    const atdd::hands::Matrix secondObject = MakeTranslation(14.0F);
    result = nullptr;
    Require(runtime.OverrideModelMatrix(handObjectSubMesh.data(), &secondObject, result) ==
                atdd::hands::OverrideResult::appliedHandObjectHistory,
            "completed hand-object model pair was not interpolated");
    Require(result != &secondObject && std::abs(result->values[3] - 22.0F) < 1.0e-5F,
            "hand-object history did not interpolate the final local model then apply mouse correction");
}

} // namespace

int main() {
    TestRenderFacingDeltaOnlyTouchesHands();
    TestHandObjectAttachmentsOverrideAndRestore();
    TestHandsEntityChangeRejectsOldPair();
    TestHolsterTracksActiveObjectNotCurrentPointer();
    TestHandsStateTransitionPreservesRootHistory();
    TestHandObjectFinalModelHistoryIncludesLocalSway();
    std::cout << "hands runtime tests passed\n";
    return EXIT_SUCCESS;
}
