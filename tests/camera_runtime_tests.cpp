#include "camera_runtime.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

template <typename T, std::size_t Size>
void Write(std::array<std::uint8_t, Size>& bytes, const std::size_t offset, const T& value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

template <typename T, std::size_t Size>
T Read(const std::array<std::uint8_t, Size>& bytes, const std::size_t offset) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void __fastcall FakeSetPitch(void* self, void*, const float value) {
    auto* bytes = static_cast<std::uint8_t*>(self);
    std::memcpy(bytes + 0x28, &value, sizeof(value));
    bytes[0x851] = 1;
    bytes[0x853] = 1;
    bytes[0x854] = 1;
}

void __fastcall FakeSetYaw(void* self, void*, const float value) {
    auto* bytes = static_cast<std::uint8_t*>(self);
    float applied = value;
    float minimum = 0.0F;
    float maximum = 0.0F;
    std::memcpy(&minimum, bytes + 0x3C, sizeof(minimum));
    std::memcpy(&maximum, bytes + 0x40, sizeof(maximum));
    if (minimum != 0.0F || maximum != 0.0F) {
        if (applied > maximum) applied = maximum;
        if (applied < minimum) applied = minimum;
    }
    std::memcpy(bytes + 0x2C, &applied, sizeof(applied));
    bytes[0x851] = 1;
    bytes[0x853] = 1;
    bytes[0x854] = 1;
}

void TestWrappedYawRespectsStatefulLimits() {
    std::array<std::uint8_t, 0x48> scene{};
    std::array<std::uint8_t, 0x0F> viewport{};
    std::array<std::uint8_t, 0x855> camera{};
    void* viewportPointer = viewport.data();
    void* cameraPointer = camera.data();
    void* worldPointer = scene.data();
    Write(scene, 0x40, viewportPointer);
    Write(viewport, 0x04, cameraPointer);
    Write(viewport, 0x08, worldPointer);
    viewport[0x0D] = 1;

    const std::uint32_t eulerMode = 0;
    const float yawMinimum = 4.188F;
    const float yawMaximum = 8.377F;
    Write(camera, 0x3C, yawMinimum);
    Write(camera, 0x40, yawMaximum);
    Write(camera, 0x44, eulerMode);

    const atdd::camera::Pose first{{0.0F, 0.0F, 0.0F}, 0.0F, 6.2F, 0.0F};
    Write(camera, 0x00, first.position);
    Write(camera, 0x28, first.pitch);
    Write(camera, 0x2C, first.yaw);
    Write(camera, 0x30, first.roll);

    atdd::camera::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(1), reinterpret_cast<void*>(&FakeSetPitch),
                       reinterpret_cast<void*>(&FakeSetYaw), reinterpret_cast<void*>(1)});
    atdd::clock::Sample warmup{};
    warmup.generation = 3;
    Require(runtime.Apply(scene.data(), 0, warmup) ==
                atdd::camera::FrameResult::identityChanged,
            "wrapped-yaw camera did not warm history");
    Require(runtime.CaptureCompletedTick(1, 3) == atdd::camera::FrameResult::captured,
            "wrapped-yaw first endpoint was not captured");

    const atdd::camera::Pose second{{0.0F, 0.0F, 0.0F}, 0.0F, 7.3F, 0.0F};
    Write(camera, 0x2C, second.yaw);
    Require(runtime.CaptureCompletedTick(2, 3) == atdd::camera::FrameResult::captured,
            "wrapped-yaw second endpoint was not captured");

    atdd::clock::Sample sample{};
    sample.kind = atdd::clock::SampleKind::interpolated;
    sample.generation = 3;
    sample.olderSerial = 1;
    sample.newerSerial = 2;
    sample.alpha = 0.5;
    Require(runtime.Apply(scene.data(), 2, sample) == atdd::camera::FrameResult::applied,
            "wrapped-yaw interpolation was not applied");

    atdd::camera::Pose before{};
    Require(runtime.ReadActivePose(before),
            "wrapped-yaw principal-range pose was unavailable");
    Require(std::abs(before.yaw) < 3.1415927F,
            "test interpolation did not produce a principal-range yaw");
    atdd::camera::Pose after{};
    Require(runtime.ApplyAngularOffset(0.0F, 0.01F, nullptr, &after),
            "wrapped-yaw preview was rejected");
    Require(after.yaw > yawMinimum && after.yaw < yawMaximum,
            "wrapped-yaw preview was clamped to the wrong limit endpoint");
    constexpr float kTwoPi = 6.28318530717958647692F;
    Require(std::abs(std::remainder(after.yaw - (before.yaw + 0.01F), kTwoPi)) < 1.0e-4F,
            "wrapped-yaw preview changed physical orientation");
    Require(runtime.Restore(), "wrapped-yaw camera state restoration failed");
    Require(std::abs(Read<float>(camera, 0x2C) - second.yaw) < 1.0e-5F,
            "wrapped-yaw authoritative value was not restored");
}

void TestScopedSubstitutionAndRestoration() {
    std::array<std::uint8_t, 0x48> scene{};
    std::array<std::uint8_t, 0x0F> viewport{};
    std::array<std::uint8_t, 0x855> camera{};

    void* viewportPointer = viewport.data();
    void* cameraPointer = camera.data();
    void* worldPointer = scene.data();
    Write(scene, 0x40, viewportPointer);
    Write(viewport, 0x04, cameraPointer);
    Write(viewport, 0x08, worldPointer);
    // cScene::Render gates on visibility, not cViewport::mbActive. The
    // authoritative current listener remains valid while this flag is false.
    viewport[0x0C] = 0;
    viewport[0x0D] = 1;

    const atdd::camera::Pose first{{0.0F, 1.0F, 2.0F}, 0.1F, 0.2F, 0.3F};
    Write(camera, 0x00, first.position);
    Write(camera, 0x28, first.pitch);
    Write(camera, 0x2C, first.yaw);
    Write(camera, 0x30, first.roll);
    const atdd::camera::AngularLimits expectedLimits{
        -1.0F, 1.1F, -2.0F, 2.1F};
    Write(camera, 0x34, expectedLimits.pitchMin);
    Write(camera, 0x38, expectedLimits.pitchMax);
    Write(camera, 0x3C, expectedLimits.yawMin);
    Write(camera, 0x40, expectedLimits.yawMax);
    const std::uint32_t eulerMode = 0;
    Write(camera, 0x44, eulerMode);
    for (std::size_t index = 0x4C; index < camera.size(); ++index) {
        camera[index] = static_cast<std::uint8_t>((index * 37U) & 0xFFU);
    }
    Write(camera, 0x44, eulerMode);

    atdd::camera::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(1), reinterpret_cast<void*>(&FakeSetPitch),
                       reinterpret_cast<void*>(&FakeSetYaw), reinterpret_cast<void*>(1)});

    atdd::clock::Sample warmup{};
    warmup.generation = 7;
    Require(runtime.Apply(scene.data(), 0, warmup) ==
                atdd::camera::FrameResult::identityChanged,
            "first main-view identity did not warm history");
    Require(runtime.CaptureCompletedTick(1, 7) == atdd::camera::FrameResult::captured,
            "first completed-tick endpoint was not captured");

    const atdd::camera::Pose second{{2.0F, 3.0F, 4.0F}, 0.2F, 0.4F, 0.6F};
    Write(camera, 0x00, second.position);
    Write(camera, 0x28, second.pitch);
    Write(camera, 0x2C, second.yaw);
    Write(camera, 0x30, second.roll);
    const auto authoritative = camera;
    Require(runtime.CaptureCompletedTick(2, 7) == atdd::camera::FrameResult::captured,
            "catch-up endpoint was not captured without an intervening render");

    atdd::clock::Sample sample{};
    sample.kind = atdd::clock::SampleKind::interpolated;
    sample.generation = 7;
    sample.olderSerial = 1;
    sample.newerSerial = 2;
    sample.alpha = 0.5;
    Require(runtime.Apply(scene.data(), 2, sample) == atdd::camera::FrameResult::applied,
            "valid main camera pair was not applied");
    Require(std::abs(Read<float>(camera, 0x00) - 1.0F) < 1.0e-5F,
            "temporary position was not interpolated");
    const float presentedPitch = Read<float>(camera, 0x28);
    const float presentedYaw = Read<float>(camera, 0x2C);
    Require(runtime.ActiveCamera() == camera.data(),
            "active override camera was not exposed");
    atdd::camera::AngularLimits activeLimits{};
    Require(runtime.ReadActiveAngularLimits(activeLimits),
            "active camera angular limits were not exposed");
    Require(std::abs(activeLimits.pitchMin - expectedLimits.pitchMin) < 1.0e-5F &&
                std::abs(activeLimits.pitchMax - expectedLimits.pitchMax) < 1.0e-5F &&
                std::abs(activeLimits.yawMin - expectedLimits.yawMin) < 1.0e-5F &&
                std::abs(activeLimits.yawMax - expectedLimits.yawMax) < 1.0e-5F,
            "active camera angular limits did not match camera storage");
    atdd::camera::Pose activePose{};
    atdd::camera::Pose authoritativePose{};
    Require(runtime.ReadActivePose(activePose),
            "active interpolated pose was not exposed");
    Require(runtime.ReadAuthoritativePose(authoritativePose),
            "active authoritative pose was not exposed");
    Require(std::abs(activePose.position.x - 1.0F) < 1.0e-5F,
            "exposed active pose was not interpolated");
    Require(atdd::camera::NearlyEqual(authoritativePose, second),
            "exposed authoritative pose did not match the saved state");
    Require(runtime.ApplyAngularOffset(0.05F, -0.10F),
            "render-only angular offset was rejected");
    Require(std::abs(Read<float>(camera, 0x28) - (presentedPitch + 0.05F)) < 1.0e-5F,
            "render-only pitch offset was not applied");
    Require(std::abs(Read<float>(camera, 0x2C) - (presentedYaw - 0.10F)) < 1.0e-5F,
            "render-only yaw offset was not applied");

    // Simulate GetFrustum rebuilding the cache and the renderer updating its
    // independent previous-view history while the override is active.
    std::memset(camera.data() + 0x4C, 0xA5, 64 * 3);
    std::memset(camera.data() + 0x354, 0x5A, 0x4FC + 5);
    camera[0x10C] = 0xE7;
    Require(runtime.Restore(), "camera state restoration failed");
    Require(runtime.ActiveCamera() == nullptr,
            "active override camera survived restoration");
    Require(!runtime.ReadActivePose(activePose) &&
                !runtime.ReadAuthoritativePose(authoritativePose) &&
                !runtime.ReadActiveAngularLimits(activeLimits),
            "camera poses remained exposed after restoration");

    Require(std::memcmp(camera.data(), authoritative.data(), 0x0C) == 0,
            "authoritative position bytes were not restored");
    Require(std::memcmp(camera.data() + 0x28, authoritative.data() + 0x28, 0x0C) == 0,
            "authoritative angle bytes were not restored");
    Require(std::memcmp(camera.data() + 0x4C, authoritative.data() + 0x4C, 64 * 3) == 0,
            "camera matrix caches were not restored");
    Require(std::memcmp(camera.data() + 0x354, authoritative.data() + 0x354,
                        0x4FC + 5) == 0,
            "frustum and dirty flags were not restored");
    Require(camera[0x10C] == 0xE7,
            "renderer-owned previous-view history was incorrectly rolled back");
}

void TestCurrentListenerFailureReasons() {
    std::array<std::uint8_t, 0x48> scene{};
    std::array<std::uint8_t, 0x0F> viewport{};
    std::array<std::uint8_t, 0x855> camera{};
    void* viewportPointer = viewport.data();
    void* cameraPointer = camera.data();
    void* worldPointer = scene.data();

    atdd::camera::Runtime runtime;
    runtime.Configure({reinterpret_cast<void*>(1), reinterpret_cast<void*>(1),
                       reinterpret_cast<void*>(1), reinterpret_cast<void*>(1)});
    atdd::clock::Sample sample{};
    Require(runtime.Apply(scene.data(), 0, sample) ==
                atdd::camera::FrameResult::noCurrentListener,
            "missing current listener was not diagnosed");

    Write(scene, 0x40, viewportPointer);
    Require(runtime.Apply(scene.data(), 0, sample) ==
                atdd::camera::FrameResult::listenerHidden,
            "hidden current listener was not diagnosed");

    viewport[0x0D] = 1;
    Write(viewport, 0x08, worldPointer);
    Require(runtime.Apply(scene.data(), 0, sample) ==
                atdd::camera::FrameResult::listenerMissingCamera,
            "missing listener camera was not diagnosed");

    Write(viewport, 0x04, cameraPointer);
    void* nullWorld = nullptr;
    Write(viewport, 0x08, nullWorld);
    Require(runtime.Apply(scene.data(), 0, sample) ==
                atdd::camera::FrameResult::listenerMissingWorld,
            "missing listener world was not diagnosed");
}

} // namespace

int main() {
    TestScopedSubstitutionAndRestoration();
    TestWrappedYawRespectsStatefulLimits();
    TestCurrentListenerFailureReasons();
    std::cout << "camera runtime tests passed\n";
    return EXIT_SUCCESS;
}
