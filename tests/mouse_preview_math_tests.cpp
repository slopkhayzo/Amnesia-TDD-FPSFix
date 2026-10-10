#include "mouse_preview_math.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

int g_failures = 0;

void Expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

void TestContinuousSensitivity() {
    float angle = 0.0F;
    constexpr float sensitivity = 1.2F;
    constexpr float perCount = -0.001F;
    float previous = 0.0F;
    for (std::int64_t raw = 0; raw >= -4; --raw) {
        Expect(atdd::mouse_preview::ContinuousAngle(
                   raw, sensitivity, perCount, angle),
               "valid raw count conversion failed");
        const float expected = static_cast<float>(raw) * sensitivity * perCount;
        Expect(std::fabs(angle - expected) < 1.0e-7F,
               "continuous raw count conversion was incorrect");
        if (raw != 0) {
            Expect(std::fabs((angle - previous) - 0.0012F) < 1.0e-7F,
                   "successive raw counts did not produce equal angular steps");
        }
        previous = angle;
    }
}

void TestInvalidInput() {
    float angle = 1.0F;
    Expect(!atdd::mouse_preview::ContinuousAngle(
               1, 0.0F, -0.001F, angle) && angle == 0.0F,
           "zero sensitivity should fail closed");
    Expect(!atdd::mouse_preview::ContinuousAngle(
               1, 1.0F, std::numeric_limits<float>::quiet_NaN(), angle),
           "non-finite angular scale should fail closed");
}

void TestTickRouting() {
    const auto gameplay = atdd::mouse_preview::RouteTickCounts(7, -3, true);
    Expect(gameplay.simulationCounts == -3,
           "gameplay routing did not retire only previewed counts");
    Expect(gameplay.deferredCounts == 7,
           "gameplay routing did not defer fresh fixed-tick counts");

    const auto inactive = atdd::mouse_preview::RouteTickCounts(7, -3, false);
    Expect(inactive.simulationCounts == 4,
           "inactive routing did not preserve stock merged delivery");
    Expect(inactive.deferredCounts == 0,
           "inactive routing unexpectedly deferred input");
}

void TestContinuousPlayerAmount() {
    float adjusted = 0.0F;
    Expect(atdd::mouse_preview::ContinuousPlayerAmount(
               1, 4.0F / 3.0F, 1, -0.008F, -0.01F, adjusted),
           "valid player-look correction failed");
    Expect(std::fabs(adjusted - (-0.011333333F)) < 1.0e-7F,
           "player-look correction did not replace only rounding error");

    Expect(!atdd::mouse_preview::ContinuousPlayerAmount(
               1, 0.0F, 0, 0.0F, -0.01F, adjusted),
           "invalid player-look sensitivity should fail closed");
}

void TestGuiPresentationRouting() {
    using atdd::mouse_preview::GuiPosition;
    const GuiPosition preview = atdd::mouse_preview::PreviewGuiPosition(
        {100, 200}, 17, -31, 1920, 1080);
    Expect(preview.x == 117 && preview.y == 169,
           "GUI preview did not add pending raw counts");

    const GuiPosition clamped = atdd::mouse_preview::PreviewGuiPosition(
        {2, 1078}, -50, 50, 1920, 1080);
    Expect(clamped.x == 0 && clamped.y == 1079,
           "GUI preview did not clamp to the client area");

    const GuiPosition relative = atdd::mouse_preview::GuiRelativeDelta(
        {110, 180}, preview);
    Expect(relative.x == 7 && relative.y == -11,
           "GUI relative movement was not retired from the last preview");
}

void TestTransformedCommandGate() {
    using atdd::mouse_preview::ClassifyCommandShape;
    using atdd::mouse_preview::CommandShape;
    using atdd::mouse_preview::PendingRawGate;

    Expect(ClassifyCommandShape(0.002F, -0.003F, 0.002F, -0.003F) ==
               CommandShape::direct,
           "matching camera command was not classified as direct");
    Expect(ClassifyCommandShape(0.0F, 0.0F, 0.0F, 0.0F) ==
               CommandShape::noSignal,
           "stationary camera command should not change gate state");
    Expect(ClassifyCommandShape(0.004F, -0.0002F, 0.0F, -0.003F) ==
               CommandShape::transformed,
           "filtered camera command was not classified as transformed");
    Expect(ClassifyCommandShape(std::numeric_limits<float>::quiet_NaN(), 0.0F,
                                0.0F, 0.0F) == CommandShape::invalid,
           "non-finite camera command did not fail closed");

    PendingRawGate gate;
    Expect(gate.Allowed(), "direct preview should start enabled");
    Expect(!gate.Update(CommandShape::transformed),
           "transformed command did not disable direct preview immediately");
    Expect(!gate.Update(CommandShape::direct),
           "direct preview recovered without hysteresis");
    Expect(!gate.Update(CommandShape::noSignal),
           "stationary sample unexpectedly recovered direct preview");
    for (std::uint32_t index = 1; index < PendingRawGate::kRecoverySamples; ++index) {
        Expect(!gate.Update(CommandShape::direct),
               "direct preview recovered too early");
    }
    Expect(gate.Update(CommandShape::direct),
           "direct preview did not recover after stable direct commands");

    Expect(atdd::mouse_preview::ShouldUpdateDirectScale(
               false, true, true, CommandShape::direct),
           "stable direct command should refine its calibration");
    Expect(!atdd::mouse_preview::ShouldUpdateDirectScale(
               false, true, false, CommandShape::transformed),
           "transformed fallback was allowed to alter calibration");
    Expect(!atdd::mouse_preview::ShouldUpdateDirectScale(
               false, false, true, CommandShape::direct),
           "the recovery command was allowed to alter its own reference");
    Expect(!atdd::mouse_preview::ShouldUpdateDirectScale(
               true, true, true, CommandShape::direct),
           "initial calibration was applied twice");
}

} // namespace

int main() {
    TestContinuousSensitivity();
    TestInvalidInput();
    TestTickRouting();
    TestContinuousPlayerAmount();
    TestGuiPresentationRouting();
    TestTransformedCommandGate();
    if (g_failures != 0) {
        std::cerr << g_failures << " mouse-preview assertion(s) failed\n";
        return 1;
    }
    std::cout << "continuous raw mouse-preview tests passed\n";
    return 0;
}
