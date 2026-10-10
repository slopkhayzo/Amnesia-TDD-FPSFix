#include "world_runtime.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

namespace {

std::uint32_t g_parentReadCount = 0;

template <std::size_t Size, typename T>
void Write(std::array<std::uint8_t, Size>& bytes, const std::size_t offset, const T& value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

atdd::world::Matrix MakeTranslation(const float x) {
    atdd::world::Matrix matrix = atdd::hands::Identity();
    matrix.values[3] = x;
    return matrix;
}

atdd::world::Matrix* __fastcall FakeGetWorldMatrix(void* entity) {
    return reinterpret_cast<atdd::world::Matrix*>(static_cast<std::uint8_t*>(entity) + 0x6C);
}

void* __fastcall FakeGetEntityParent(void* entity) {
    ++g_parentReadCount;
    void* parent = nullptr;
    std::memcpy(&parent, static_cast<std::uint8_t*>(entity) + 0x288, sizeof(parent));
    return parent;
}

bool __fastcall FakeGetBodyEnabled(void* body) {
    std::uint8_t enabled = 0;
    std::memcpy(&enabled, static_cast<std::uint8_t*>(body) + 0x30, sizeof(enabled));
    return enabled != 0;
}

atdd::clock::Sample Pair(const double alpha, const std::uint64_t olderSerial = 1,
                         const std::uint64_t newerSerial = 2) {
    atdd::clock::Sample sample{};
    sample.kind = atdd::clock::SampleKind::interpolated;
    sample.generation = 1;
    sample.olderSerial = olderSerial;
    sample.newerSerial = newerSerial;
    sample.alpha = alpha;
    return sample;
}

void PrepareMesh(std::array<std::uint8_t, 0x400>& mesh, const atdd::world::Matrix& matrix) {
    const std::uint8_t active = 1;
    const std::uint8_t enabled = 1;
    Write(mesh, 0x24, active);
    Write(mesh, 0x30, enabled);
    Write(mesh, 0x6C, matrix);
}

void SetBodyEnabled(std::array<std::uint8_t, 0x400>& body, const bool enabled) {
    const std::uint8_t value = enabled ? 1U : 0U;
    Write(body, 0x30, value);
}

void TestPropAndGruntInterpolation() {
    std::array<std::uint8_t, 0x500> prop{};
    std::array<std::uint8_t, 0x900> enemy{};
    std::array<std::uint8_t, 0x400> propMesh{};
    std::array<std::uint8_t, 0x400> propBody{};
    std::array<std::uint8_t, 0x400> enemyMesh{};
    std::array<std::uint8_t, 0x400> propSubMesh{};
    std::array<std::uint8_t, 0x400> enemySubMesh{};
    void* propMeshPointer = propMesh.data();
    void* propBodyPointer = propBody.data();
    std::array<void*, 1> propBodies{propBodyPointer};
    void* propBodiesBegin = propBodies.data();
    void* propBodiesEnd = propBodies.data() + propBodies.size();
    void* enemyMeshPointer = enemyMesh.data();
    const std::int32_t propId = 10;
    const std::int32_t enemyId = 20;
    const std::int32_t objectType = 0;
    const std::int32_t gruntType = 0;
    const std::uint8_t dynamic = 0;
    Write(prop, 0x40, propId);
    Write(prop, 0x119, dynamic);
    Write(prop, 0x260, propBodiesBegin);
    Write(prop, 0x264, propBodiesEnd);
    Write(prop, 0x340, propMeshPointer);
    Write(prop, 0x484, objectType);
    Write(enemy, 0x40, enemyId);
    Write(enemy, 0x54C, enemyMeshPointer);
    Write(enemy, 0x82C, gruntType);
    Write(propSubMesh, 0x370, propMeshPointer);
    Write(enemySubMesh, 0x370, enemyMeshPointer);

    auto runtimeStorage = std::make_unique<atdd::world::Runtime>();
    atdd::world::Runtime& runtime = *runtimeStorage;
    runtime.Configure({reinterpret_cast<void*>(&FakeGetWorldMatrix),
                       reinterpret_cast<void*>(&FakeGetEntityParent),
                       reinterpret_cast<void*>(&FakeGetBodyEnabled)});
    PrepareMesh(propMesh, MakeTranslation(10.0F));
    PrepareMesh(propBody, MakeTranslation(0.0F));
    PrepareMesh(enemyMesh, MakeTranslation(2.0F));
    atdd::world::PropStageInfo propStage{};
    Require(runtime.StageProp(prop.data(), 1, &propStage) ==
                atdd::world::StageResult::stagedProp,
            "dynamic Object prop was not staged");
    Require(propStage.valid && propStage.mesh == propMesh.data() &&
                propStage.id == propId && propStage.bodyCount == 1,
            "world prop stage descriptor did not preserve validated identity");
    Require(runtime.StageEnemy(enemy.data(), 1) == atdd::world::StageResult::stagedGrunt,
            "Grunt was not staged");
    const auto first = runtime.CommitCompletedTick(1, 1);
    Require(first.reset == 2, "first world snapshots did not establish histories");

    PrepareMesh(propBody, MakeTranslation(0.5F));
    PrepareMesh(enemyMesh, MakeTranslation(2.5F));
    Require(runtime.StageProp(prop.data(), 2) == atdd::world::StageResult::stagedProp,
            "second prop root was not staged");
    Require(runtime.StageEnemy(enemy.data(), 2) == atdd::world::StageResult::stagedGrunt,
            "second Grunt root was not staged");
    const auto second = runtime.CommitCompletedTick(2, 1);
    Require(second.captured == 2, "second world roots were not captured");
    const auto render = runtime.BeginRender(Pair(0.5));
    Require(render.ready == 2, "world roots were not render-ready");

    std::array<std::uint8_t, 0x400> unrelatedSubMesh{};
    std::array<std::uint8_t, 0x400> unrelatedOwner{};
    void* unrelatedOwnerPointer = unrelatedOwner.data();
    Write(unrelatedSubMesh, 0x370, unrelatedOwnerPointer);
    const atdd::world::Matrix unrelatedOriginal = MakeTranslation(8.0F);
    const atdd::world::Matrix* unrelatedResult = nullptr;
    g_parentReadCount = 0;
    Require(runtime.OverrideModelMatrix(unrelatedSubMesh.data(), &unrelatedOriginal,
                                        unrelatedResult) == atdd::world::OverrideResult::notWorld,
            "unrelated scene geometry entered world interpolation");
    Require(g_parentReadCount == 0, "unrelated scene geometry touched the parent hierarchy");
    Require(unrelatedResult == &unrelatedOriginal,
            "unrelated scene geometry changed its model matrix");

    const atdd::world::Matrix originalProp = MakeTranslation(0.5F);
    const atdd::world::Matrix originalEnemy = MakeTranslation(2.5F);
    const atdd::world::Matrix* result = nullptr;
    Require(runtime.OverrideModelMatrix(propSubMesh.data(), &originalProp, result) ==
                atdd::world::OverrideResult::appliedProp,
            "prop model matrix was not overridden");
    Require(std::abs(result->values[3] - 0.25F) < 1.0e-4F, "prop midpoint was incorrect");
    Require(runtime.OverrideModelMatrix(enemySubMesh.data(), &originalEnemy, result) ==
                atdd::world::OverrideResult::appliedGrunt,
            "Grunt model matrix was not overridden");
    Require(std::abs(result->values[3] - 2.25F) < 1.0e-4F, "Grunt midpoint was incorrect");
}

void TestAllowlistAndDiscontinuity() {
    std::array<std::uint8_t, 0x500> prop{};
    std::array<std::uint8_t, 0x400> mesh{};
    std::array<std::uint8_t, 0x400> body{};
    void* meshPointer = mesh.data();
    void* bodyPointer = body.data();
    std::array<void*, 1> bodies{bodyPointer};
    void* bodiesBegin = bodies.data();
    void* bodiesEnd = bodies.data() + bodies.size();
    const std::int32_t id = 1;
    std::int32_t objectType = 6; // eLuxPropType_MoveObject
    std::uint8_t staticPhysics = 1;
    Write(prop, 0x40, id);
    Write(prop, 0x119, staticPhysics);
    Write(prop, 0x260, bodiesBegin);
    Write(prop, 0x264, bodiesEnd);
    Write(prop, 0x340, meshPointer);
    Write(prop, 0x484, objectType);
    PrepareMesh(mesh, MakeTranslation(0.0F));
    PrepareMesh(body, MakeTranslation(0.0F));

    auto runtimeStorage = std::make_unique<atdd::world::Runtime>();
    atdd::world::Runtime& runtime = *runtimeStorage;
    runtime.Configure({reinterpret_cast<void*>(&FakeGetWorldMatrix),
                       reinterpret_cast<void*>(&FakeGetEntityParent),
                       reinterpret_cast<void*>(&FakeGetBodyEnabled)});
    Require(runtime.StageProp(prop.data(), 1) == atdd::world::StageResult::ineligible,
            "static prop entered the world allowlist");

    staticPhysics = 0;
    Write(prop, 0x119, staticPhysics);
    Require(runtime.StageProp(prop.data(), 1) == atdd::world::StageResult::stagedProp,
            "dynamic MoveObject prop did not enter the allowlist");
    (void)runtime.CommitCompletedTick(1, 1);
    PrepareMesh(body, MakeTranslation(2.0F));
    Require(runtime.StageProp(prop.data(), 2) == atdd::world::StageResult::stagedProp,
            "jumped prop was not staged");
    const auto jumped = runtime.CommitCompletedTick(2, 1);
    Require(jumped.reset >= 1, "large world-root jump did not reset history");
    Require(runtime.BeginRender(Pair(0.5)).ready == 0, "render crossed a world-root discontinuity");

    objectType = 16; // eLuxPropType_LastEnum is never a valid instance type.
    Write(prop, 0x484, objectType);
    Require(runtime.StageProp(prop.data(), 3) == atdd::world::StageResult::ineligible,
            "unknown prop type entered the world allowlist");
}

void TestMultiBodyPropInterpolation() {
    std::array<std::uint8_t, 0x500> prop{};
    std::array<std::uint8_t, 0x400> mesh{};
    std::array<std::uint8_t, 0x400> firstBody{};
    std::array<std::uint8_t, 0x400> secondBody{};
    std::array<std::uint8_t, 0x400> intermediate{};
    std::array<std::uint8_t, 0x400> firstSubMesh{};
    std::array<std::uint8_t, 0x400> secondSubMesh{};
    std::array<std::uint8_t, 0x400> historySubMesh{};
    std::array<std::uint8_t, 0x400> foreignOwnerSubMesh{};
    std::array<std::uint8_t, 0x400> directOnlySubMesh{};
    std::array<std::uint8_t, 0x400> nestedOnlySubMesh{};
    std::array<std::uint8_t, 0x400> foreignOwner{};
    void* meshPointer = mesh.data();
    void* firstBodyPointer = firstBody.data();
    void* secondBodyPointer = secondBody.data();
    void* intermediatePointer = intermediate.data();
    std::array<void*, 2> bodies{firstBodyPointer, secondBodyPointer};
    std::array<void*, 4> subMeshes{firstSubMesh.data(), secondSubMesh.data(), historySubMesh.data(),
                                   foreignOwnerSubMesh.data()};
    void* bodiesBegin = bodies.data();
    void* bodiesEnd = bodies.data() + bodies.size();
    void* subMeshesBegin = subMeshes.data();
    void* subMeshesEnd = subMeshes.data() + subMeshes.size();
    const std::int32_t id = 30;
    const std::int32_t objectType = 0;
    const std::uint8_t dynamic = 0;
    Write(prop, 0x40, id);
    Write(prop, 0x119, dynamic);
    Write(prop, 0x260, bodiesBegin);
    Write(prop, 0x264, bodiesEnd);
    Write(prop, 0x340, meshPointer);
    Write(prop, 0x484, objectType);
    Write(mesh, 0x30C, subMeshesBegin);
    Write(mesh, 0x310, subMeshesEnd);
    Write(firstSubMesh, 0x370, meshPointer);
    Write(firstSubMesh, 0x288, firstBodyPointer);
    Write(secondSubMesh, 0x370, meshPointer);
    Write(secondSubMesh, 0x288, intermediatePointer);
    Write(intermediate, 0x288, secondBodyPointer);
    Write(historySubMesh, 0x370, meshPointer);
    void* foreignOwnerPointer = foreignOwner.data();
    Write(foreignOwnerSubMesh, 0x370, foreignOwnerPointer);
    Write(foreignOwnerSubMesh, 0x288, firstBodyPointer);
    Write(directOnlySubMesh, 0x370, meshPointer);
    Write(directOnlySubMesh, 0x288, firstBodyPointer);
    Write(nestedOnlySubMesh, 0x370, meshPointer);
    Write(nestedOnlySubMesh, 0x288, intermediatePointer);

    auto runtimeStorage = std::make_unique<atdd::world::Runtime>();
    atdd::world::Runtime& runtime = *runtimeStorage;
    runtime.Configure({reinterpret_cast<void*>(&FakeGetWorldMatrix),
                       reinterpret_cast<void*>(&FakeGetEntityParent),
                       reinterpret_cast<void*>(&FakeGetBodyEnabled)});
    PrepareMesh(mesh, MakeTranslation(0.0F));
    PrepareMesh(firstBody, MakeTranslation(0.0F));
    PrepareMesh(secondBody, MakeTranslation(10.0F));
    PrepareMesh(firstSubMesh, MakeTranslation(0.0F));
    PrepareMesh(secondSubMesh, MakeTranslation(10.0F));
    PrepareMesh(historySubMesh, MakeTranslation(0.0F));
    PrepareMesh(foreignOwnerSubMesh, MakeTranslation(0.0F));
    Require(runtime.StageProp(prop.data(), 1) == atdd::world::StageResult::stagedProp,
            "multi-body Object prop was not staged");
    const auto first = runtime.CommitCompletedTick(1, 1);
    Require(first.reset == 2, "multi-body prop did not establish one history per body");
    Require(first.modelCandidates == 4 && first.modelCaptured == 4 && first.modelInvalid == 0,
            "multi-body submesh endpoints were not captured from the mesh vector");

    PrepareMesh(firstBody, MakeTranslation(0.4F));
    PrepareMesh(secondBody, MakeTranslation(9.7F));
    PrepareMesh(firstSubMesh, MakeTranslation(0.4F));
    PrepareMesh(secondSubMesh, MakeTranslation(9.7F));
    PrepareMesh(historySubMesh, MakeTranslation(0.4F));
    PrepareMesh(foreignOwnerSubMesh, MakeTranslation(0.4F));
    Require(runtime.StageProp(prop.data(), 2) == atdd::world::StageResult::stagedProp,
            "multi-body Object prop was not restaged");
    const auto second = runtime.CommitCompletedTick(2, 1);
    Require(second.captured == 2 && second.modelCaptured == 4,
            "multi-body prop roots were not captured independently");
    const auto secondRender = runtime.BeginRender(Pair(0.5));
    Require(secondRender.ready == 2, "multi-body prop roots were not independently render-ready");
    Require(secondRender.modelCacheApplied == 4 && secondRender.modelCacheInvalid == 0,
            "moving multi-body final models were not applied to the render cache");
    Require(std::abs(FakeGetWorldMatrix(historySubMesh.data())->values[3] - 0.2F) < 1.0e-4F,
            "render-cache override did not expose the exact midpoint");
    Require(std::abs(FakeGetWorldMatrix(firstSubMesh.data())->values[3] - 0.2F) < 1.0e-4F,
            "exact multi-body render-cache history did not retain priority");

    const atdd::world::Matrix firstOriginal = MakeTranslation(0.4F);
    const atdd::world::Matrix secondOriginal = MakeTranslation(9.7F);
    const atdd::world::Matrix* result = nullptr;
    Require(runtime.OverrideModelMatrix(firstSubMesh.data(), &firstOriginal, result) ==
                atdd::world::OverrideResult::appliedPropModelHistory,
            "first prop final-model history was not returned from its cache");
    Require(std::abs(result->values[3] - 0.2F) < 1.0e-4F, "first prop body midpoint was incorrect");
    Require(runtime.OverrideModelMatrix(secondSubMesh.data(), &secondOriginal, result) ==
                atdd::world::OverrideResult::appliedPropModelHistory,
            "nested second prop final-model history was not returned from its cache");
    Require(std::abs(result->values[3] - 9.85F) < 1.0e-4F,
            "second prop body midpoint was incorrect");
    Require(runtime.OverrideModelMatrix(firstSubMesh.data(), nullptr, result) ==
                atdd::world::OverrideResult::appliedPropModelHistory,
            "cached multi-body model was not returned for a null original");
    Require(std::abs(result->values[3] - 0.2F) < 1.0e-4F,
            "cached multi-body model midpoint was incorrect");
    Require(runtime.OverrideModelMatrix(foreignOwnerSubMesh.data(), &firstOriginal, result) ==
                atdd::world::OverrideResult::appliedPropModelHistory,
            "captured foreign-owner submesh did not retain exact history priority");
    Require(std::abs(result->values[3] - 0.2F) < 1.0e-4F,
            "foreign-owner final-model midpoint was incorrect");
    Require(runtime.OverrideModelMatrix(directOnlySubMesh.data(), &firstOriginal, result) ==
                atdd::world::OverrideResult::appliedPropDirectParent,
            "uncaptured submesh did not resolve its corrected direct entity parent");
    Require(std::abs(result->values[3] - 0.2F) < 1.0e-4F,
            "corrected direct-parent body midpoint was incorrect");
    Require(runtime.OverrideModelMatrix(nestedOnlySubMesh.data(), &secondOriginal, result) ==
                atdd::world::OverrideResult::appliedPropParent,
            "uncaptured nested submesh did not traverse corrected entity parents");
    Require(std::abs(result->values[3] - 9.85F) < 1.0e-4F,
            "corrected nested-parent body midpoint was incorrect");

    // A multi-body prop can expose only its final rendered submesh matrix, with
    // no usable body in the entity-parent chain. The completed-tick capture
    // enumerates that submesh directly from its owning cMeshEntity.
    const atdd::world::Matrix firstHistoryOriginal = MakeTranslation(0.4F);
    atdd::world::ModelHistoryGate historyGate = atdd::world::ModelHistoryGate::notFound;
    Require(runtime.OverrideModelMatrix(historySubMesh.data(), &firstHistoryOriginal, result,
                                        &historyGate) ==
                atdd::world::OverrideResult::appliedPropModelHistory,
            "unassociated submesh did not use its captured final-model history");
    Require(historyGate == atdd::world::ModelHistoryGate::appliedMovingChanged,
            "successful final-model history did not report its gate outcome");
    Require(std::abs(result->values[3] - 0.2F) < 1.0e-4F,
            "first final-model history midpoint was incorrect");
    Require(runtime.EndRender() == 0, "final-model render cache could not be restored");
    Require(std::abs(FakeGetWorldMatrix(historySubMesh.data())->values[3] - 0.4F) < 1.0e-4F,
            "final-model render cache did not restore authoritative bytes");

    PrepareMesh(firstBody, MakeTranslation(0.8F));
    PrepareMesh(secondBody, MakeTranslation(9.4F));
    PrepareMesh(firstSubMesh, MakeTranslation(0.8F));
    PrepareMesh(secondSubMesh, MakeTranslation(9.4F));
    PrepareMesh(historySubMesh, MakeTranslation(0.8F));
    PrepareMesh(foreignOwnerSubMesh, MakeTranslation(0.8F));
    Require(runtime.StageProp(prop.data(), 3) == atdd::world::StageResult::stagedProp,
            "third multi-body endpoint was not staged");
    const auto third = runtime.CommitCompletedTick(3, 1);
    Require(third.captured == 2 && third.modelCaptured == 4,
            "third multi-body endpoint was not captured");
    const auto thirdRender = runtime.BeginRender(Pair(0.5, 2, 3));
    Require(thirdRender.ready == 2 && thirdRender.multiBodyOwners == 1,
            "multi-body owner was not classified for model-history fallback");
    const atdd::world::Matrix secondHistoryOriginal = MakeTranslation(0.8F);
    Require(runtime.OverrideModelMatrix(historySubMesh.data(), &secondHistoryOriginal, result,
                                        &historyGate) ==
                atdd::world::OverrideResult::appliedPropModelHistory,
            "unassociated submesh did not use its final-model history");
    Require(std::abs(result->values[3] - 0.6F) < 1.0e-4F,
            "final-model history midpoint was incorrect");
    Require(runtime.EndRender() == 0, "third final-model render cache could not be restored");

    // Reproduce a joint-driven child whose final model changes while both
    // physics-root histories remain unchanged. Exact captured history must be
    // sufficient to activate rendering without a moving-owner index entry.
    PrepareMesh(historySubMesh, MakeTranslation(1.2F));
    Require(runtime.StageProp(prop.data(), 4) == atdd::world::StageResult::stagedProp,
            "stationary-root multi-body prop was not restaged");
    const auto fourth = runtime.CommitCompletedTick(4, 1);
    Require(fourth.modelCaptured == 4, "joint-driven child endpoint was not captured");
    const auto fourthRender = runtime.BeginRender(Pair(0.5, 3, 4));
    Require(fourthRender.ready == 0 && fourthRender.modelReady == 1 &&
                fourthRender.modelMovingReady == 1 && fourthRender.modelCacheApplied == 1,
            "only the changed exact model history activated without a moving owner");
    const atdd::world::Matrix thirdHistoryOriginal = MakeTranslation(1.2F);
    Require(runtime.OverrideModelMatrix(historySubMesh.data(), &thirdHistoryOriginal, result,
                                        &historyGate) ==
                atdd::world::OverrideResult::appliedPropModelHistory,
            "ownerless joint-driven child did not use exact model history");
    Require(std::abs(result->values[3] - 1.0F) < 1.0e-4F,
            "ownerless final-model history midpoint was incorrect");
    Require(runtime.EndRender() == 0, "joint-driven render cache could not be restored");

    // Multi-body props must keep their exact final-model endpoints current
    // even when Newton marks every body as sleeping. Joint-driven interaction
    // state can still move a drawer or hinge in that condition.
    SetBodyEnabled(firstBody, false);
    SetBodyEnabled(secondBody, false);
    Require(runtime.StageProp(prop.data(), 5) == atdd::world::StageResult::stagedProp,
            "multi-body prop incorrectly entered the sleeping fast path");
    const auto sleepTransition = runtime.CommitCompletedTick(5, 1);
    Require(sleepTransition.modelCaptured == 4,
            "multi-body sleep transition lost final-model capture");
    PrepareMesh(historySubMesh, MakeTranslation(1.6F));
    Require(runtime.StageProp(prop.data(), 6) == atdd::world::StageResult::stagedProp,
            "sleeping multi-body prop did not keep staging model endpoints");
    const auto sleepingModels = runtime.CommitCompletedTick(6, 1);
    Require(sleepingModels.sleeping == 2 && sleepingModels.modelCaptured == 4,
            "sleeping multi-body prop did not capture exact model endpoints");
    const auto sleepingRender = runtime.BeginRender(Pair(0.5, 5, 6));
    Require(sleepingRender.ready == 0 && sleepingRender.modelReady == 1 &&
                sleepingRender.modelMovingReady == 1 && sleepingRender.modelCacheApplied == 1,
            "sleeping multi-body final model was not render-ready");
    const atdd::world::Matrix sleepingHistoryOriginal = MakeTranslation(1.6F);
    Require(runtime.OverrideModelMatrix(historySubMesh.data(), &sleepingHistoryOriginal, result,
                                        &historyGate) ==
                atdd::world::OverrideResult::appliedPropModelHistory,
            "sleeping joint-driven child did not use exact model history");
    Require(std::abs(result->values[3] - 1.4F) < 1.0e-4F,
            "sleeping final-model history midpoint was incorrect");
    Require(runtime.EndRender() == 0, "sleeping final-model render cache could not be restored");

    // Catch-up can publish several endpoints before another render. Only the
    // final adjacent previous/current pair should survive for presentation.
    PrepareMesh(historySubMesh, MakeTranslation(2.0F));
    Require(runtime.StageProp(prop.data(), 7) == atdd::world::StageResult::stagedProp,
            "ahead-of-presentation multi-body prop was not staged");
    Require(runtime.CommitCompletedTick(7, 1).modelCaptured == 4,
            "ahead-of-presentation endpoint was not captured");
    for (std::uint64_t serial = 8; serial <= 10; ++serial) {
        PrepareMesh(historySubMesh, MakeTranslation(2.0F + 0.4F * static_cast<float>(serial - 7)));
        Require(runtime.StageProp(prop.data(), serial) == atdd::world::StageResult::stagedProp,
                "far-ahead multi-body prop was not staged");
        Require(runtime.CommitCompletedTick(serial, 1).modelCaptured == 4,
                "far-ahead endpoint was not captured");
    }
    const auto delayedRender = runtime.BeginRender(Pair(0.5, 9, 10));
    Require(delayedRender.modelReady != 0,
            "latest previous/current model pair was not retained");
    const atdd::world::Matrix farAheadOriginal = MakeTranslation(3.2F);
    Require(runtime.OverrideModelMatrix(historySubMesh.data(), &farAheadOriginal, result,
                                        &historyGate) ==
                atdd::world::OverrideResult::appliedPropModelHistory,
            "latest exact model pair was not applied");
    Require(std::abs(result->values[3] - 3.0F) < 1.0e-4F,
            "latest exact-model midpoint was incorrect");
    Require(runtime.EndRender() == 0, "latest final-model render cache could not be restored");
}

void TestSleepingBodyCaptureFilter() {
    std::array<std::uint8_t, 0x500> prop{};
    std::array<std::uint8_t, 0x400> mesh{};
    std::array<std::uint8_t, 0x400> body{};
    void* meshPointer = mesh.data();
    void* bodyPointer = body.data();
    std::array<void*, 1> bodies{bodyPointer};
    void* bodiesBegin = bodies.data();
    void* bodiesEnd = bodies.data() + bodies.size();
    const std::int32_t id = 40;
    const std::int32_t objectType = 0;
    const std::uint8_t dynamic = 0;
    Write(prop, 0x40, id);
    Write(prop, 0x119, dynamic);
    Write(prop, 0x260, bodiesBegin);
    Write(prop, 0x264, bodiesEnd);
    Write(prop, 0x340, meshPointer);
    Write(prop, 0x484, objectType);

    auto runtimeStorage = std::make_unique<atdd::world::Runtime>();
    atdd::world::Runtime& runtime = *runtimeStorage;
    runtime.Configure({reinterpret_cast<void*>(&FakeGetWorldMatrix),
                       reinterpret_cast<void*>(&FakeGetEntityParent),
                       reinterpret_cast<void*>(&FakeGetBodyEnabled)});
    PrepareMesh(mesh, MakeTranslation(0.0F));
    PrepareMesh(body, MakeTranslation(0.0F));
    Require(runtime.StageProp(prop.data(), 1) == atdd::world::StageResult::stagedProp,
            "awake prop was not staged");
    Require(runtime.CommitCompletedTick(1, 1).reset == 1, "awake prop did not establish history");

    PrepareMesh(body, MakeTranslation(0.4F));
    (void)runtime.StageProp(prop.data(), 2);
    Require(runtime.CommitCompletedTick(2, 1).captured == 1,
            "awake prop did not capture its second endpoint");
    Require(runtime.BeginRender(Pair(0.5)).ready == 1, "awake prop was not render-ready");
    Require(runtime.EndRender() == 0, "awake prop render state could not be restored");

    PrepareMesh(body, MakeTranslation(0.5F));
    SetBodyEnabled(body, false);
    (void)runtime.StageProp(prop.data(), 3);
    const auto finalEndpoint = runtime.CommitCompletedTick(3, 1);
    Require(finalEndpoint.captured == 1 && finalEndpoint.sleeping == 0,
            "sleep transition did not retain its final endpoint");

    atdd::world::PropStageInfo sleepingStage{prop.data(), 1, 1, false, true};
    Require(runtime.StageProp(prop.data(), 4, &sleepingStage) ==
                atdd::world::StageResult::cachedSleepingProp,
            "sleeping prop did not take the cached fast path");
    Require(!sleepingStage.valid && sleepingStage.mesh == nullptr &&
                sleepingStage.bodyCount == 0,
            "sleeping fast path leaked a pose-stage descriptor");
    const auto sleeping = runtime.CommitCompletedTick(4, 1);
    Require(sleeping.captured == 0 && sleeping.sleeping == 0,
            "cached sleeping body performed commit-time work");
    Require(runtime.BeginRender(Pair(0.5, 3, 4)).ready == 0, "sleeping body remained render-ready");

    PrepareMesh(body, MakeTranslation(0.6F));
    Require(runtime.StageProp(prop.data(), 5) == atdd::world::StageResult::cachedSleepingProp,
            "sleeping prop did not remain cached before its wake callback");
    runtime.NotifyBodyMoved(body.data());
    const auto wake = runtime.CommitCompletedTick(5, 1);
    Require(wake.movedCallbacks == 1 && wake.movedMatched == 1 && wake.movedOverflow == 0 &&
                wake.reset >= 1,
            "waking body did not rebuild history across the skipped ticks");
    PrepareMesh(body, MakeTranslation(0.7F));
    (void)runtime.StageProp(prop.data(), 6);
    Require(runtime.CommitCompletedTick(6, 1).captured == 1,
            "waking body did not resume endpoint capture");
    Require(runtime.BeginRender(Pair(0.5, 5, 6)).ready == 1,
            "waking body did not resume interpolation");
}

void TestStandalonePhysicsBodyInterpolation() {
    std::array<std::uint8_t, 0x400> body{};
    std::array<std::uint8_t, 0x400> owner{};
    std::array<std::uint8_t, 0x400> subMesh{};
    void* bodyPointer = body.data();
    void* ownerPointer = owner.data();
    const std::int32_t id = 77;
    const std::uint32_t childCount = 1;
    Write(body, 0x40, id);
    Write(body, 0x280, childCount);
    Write(subMesh, 0x288, bodyPointer);
    Write(subMesh, 0x370, ownerPointer);

    auto runtimeStorage = std::make_unique<atdd::world::Runtime>();
    atdd::world::Runtime& runtime = *runtimeStorage;
    runtime.Configure({reinterpret_cast<void*>(&FakeGetWorldMatrix),
                       reinterpret_cast<void*>(&FakeGetEntityParent),
                       reinterpret_cast<void*>(&FakeGetBodyEnabled)});
    PrepareMesh(body, MakeTranslation(0.0F));
    runtime.NotifyBodyMoved(bodyPointer);
    const auto first = runtime.CommitCompletedTick(1, 1);
    Require(first.movedCallbacks == 1 && first.movedStandalone == 1 && first.reset == 1,
            "standalone render body did not establish a provisional history");

    PrepareMesh(body, MakeTranslation(0.4F));
    runtime.NotifyBodyMoved(bodyPointer);
    const auto second = runtime.CommitCompletedTick(2, 1);
    Require(second.movedStandalone == 1 && second.captured == 1,
            "standalone render body did not capture its second endpoint");
    Require(runtime.BeginRender(Pair(0.5)).ready == 1,
            "standalone render body was not render-ready");

    const atdd::world::Matrix original = MakeTranslation(0.4F);
    const atdd::world::Matrix* result = nullptr;
    Require(runtime.OverrideModelMatrix(subMesh.data(), &original, result) ==
                atdd::world::OverrideResult::appliedStandaloneBody,
            "standalone body's directly attached submesh was not overridden");
    Require(std::abs(result->values[3] - 0.2F) < 1.0e-4F,
            "standalone body midpoint was incorrect");
    Require(runtime.OverrideModelMatrix(subMesh.data(), nullptr, result) ==
                atdd::world::OverrideResult::appliedStandaloneBodyNullModel,
            "standalone body's null model did not preserve HPL identity semantics");
    Require(std::abs(result->values[3] + 0.2F) < 1.0e-4F,
            "standalone body's null-model correction was incorrect");

    std::array<std::uint8_t, 0x400> childlessBody{};
    PrepareMesh(childlessBody, MakeTranslation(1.0F));
    runtime.NotifyBodyMoved(childlessBody.data());
    const auto rejected = runtime.CommitCompletedTick(3, 1);
    Require(rejected.movedCallbacks == 1 && rejected.movedStandalone == 0,
            "childless Newton body entered standalone render interpolation");
}

} // namespace

int main() {
    TestPropAndGruntInterpolation();
    TestAllowlistAndDiscontinuity();
    TestMultiBodyPropInterpolation();
    TestSleepingBodyCaptureFilter();
    TestStandalonePhysicsBodyInterpolation();
    std::cout << "world runtime tests passed\n";
    return 0;
}
