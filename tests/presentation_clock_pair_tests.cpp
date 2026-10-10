#include "presentation_clock.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

namespace {

int g_failures = 0;

void Expect(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

void TestCadence(const unsigned int presentationRate) {
    using atdd::clock::PresentationClock;
    using atdd::clock::SampleKind;

    PresentationClock clock;
    const double fixedStep = PresentationClock::kFixedStepSeconds;
    const double renderStep = 1.0 / static_cast<double>(presentationRate);
    std::uint64_t serial = 0;
    double latestTickTime = 0.0;
    double nextTickTime = fixedStep;
    double previousDisplayed = -1.0;
    unsigned int valid = 0;

    (void)clock.Publish(serial, 0.0);
    for (double now = 0.0; now <= 4.0; now += renderStep) {
        while (nextTickTime <= now + 1.0e-9) {
            latestTickTime = nextTickTime;
            ++serial;
            const auto published = clock.Publish(serial, now);
            Expect(published.accepted, "fixed endpoint should publish");
            nextTickTime += fixedStep;
        }
        const double alpha = (now - latestTickTime) / fixedStep;
        const auto sample = clock.Evaluate(now, alpha);
        if (sample.kind != SampleKind::interpolated) {
            continue;
        }
        Expect(sample.newerSerial == serial,
               "presentation pair must end at the current fixed serial");
        Expect(sample.olderSerial + 1U == sample.newerSerial,
               "presentation must retain exactly two adjacent states");
        Expect(sample.alpha >= 0.0 && sample.alpha <= 1.0,
               "logic-timer alpha must remain bounded");
        const double displayed = static_cast<double>(sample.olderSerial) + sample.alpha;
        Expect(displayed + 1.0e-8 >= previousDisplayed,
               "displayed fixed time must not regress");
        Expect(static_cast<double>(serial) - displayed <= 1.0 + 1.0e-8,
               "presentation latency exceeded one fixed tick");
        previousDisplayed = displayed;
        ++valid;
    }
    Expect(valid > presentationRate * 3U,
           "cadence did not produce enough two-state samples");
}

void TestTwoStateSamplingAndCatchUp() {
    using atdd::clock::PresentationClock;
    using atdd::clock::SampleKind;

    const double step = PresentationClock::kFixedStepSeconds;
    PresentationClock clock;
    (void)clock.Publish(10, 0.0);
    Expect(clock.Evaluate(0.0, 0.0).kind == SampleKind::unavailable,
           "one endpoint must not interpolate");
    (void)clock.Publish(11, step);

    const auto midpoint = clock.Evaluate(1.5 * step, 0.5);
    Expect(midpoint.kind == SampleKind::interpolated && midpoint.olderSerial == 10 &&
               midpoint.newerSerial == 11 && std::abs(midpoint.alpha - 0.5) < 1.0e-8,
           "two-state midpoint was not sampled directly");

    const auto bounded = clock.Evaluate(1.75 * step, 1.25);
    Expect(bounded.kind == SampleKind::interpolated && bounded.olderSerial == 10 &&
               bounded.newerSerial == 11 && std::abs(bounded.alpha - 1.0) < 1.0e-8,
           "late timer residual should clamp within the current pair");

    (void)clock.Publish(12, 2.0 * step);
    (void)clock.Publish(13, 2.0 * step);
    (void)clock.Publish(14, 2.0 * step);
    Expect(clock.SnapshotCount() == 2,
           "catch-up must retain only its final previous/current endpoints");
    const auto catchUp = clock.Evaluate(2.25 * step, 0.25);
    Expect(catchUp.kind == SampleKind::interpolated && catchUp.olderSerial == 13 &&
               catchUp.newerSerial == 14 && std::abs(catchUp.alpha - 0.25) < 1.0e-8,
           "catch-up did not collapse to its final adjacent pair");

    Expect(clock.Evaluate(2.5 * step, 0.5, false).kind == SampleKind::unavailable,
           "invalid engine phase must fail closed");
}

void TestLogicTimerResidual() {
    using atdd::clock::LogicTimerAlpha;

    double alpha = -1.0;
    Expect(LogicTimerAlpha(100.0, 1000.0 / 60.0, 1.0,
                           100.0 - (1000.0 / 120.0), 0.0, alpha) &&
               std::abs(alpha - 0.5) < 1.0e-8,
           "HPL logic-timer midpoint residual was incorrect");
    Expect(LogicTimerAlpha(100.0, 1000.0 / 60.0, 2.0,
                           100.0 - (1000.0 / 240.0), 0.0, alpha) &&
               std::abs(alpha - 0.5) < 1.0e-8,
           "logic-timer speed multiplier was not reflected in alpha");
    double rawAlpha = 0.0;
    Expect(LogicTimerAlpha(100.0, 1000.0 / 60.0, 1.0, 106.0,
                           2.0 / 3.0, alpha, &rawAlpha) &&
               std::abs(rawAlpha - 1.36) < 1.0e-8 &&
               std::abs(alpha - (1.36 - (2.0 / 3.0))) < 1.0e-8,
           "one-render headroom did not recover a post-deadline phase");
    Expect(!LogicTimerAlpha(100.0, 0.0, 1.0, 100.0, 0.0, alpha),
           "invalid logic-timer step must fail closed");
}

void TestNinetyHzSchedulerHeadroom() {
    using atdd::clock::LogicTimerAlpha;

    constexpr double renderStepTicks = 2.0 / 3.0;
    constexpr double delay = renderStepTicks * 0.55;
    const double rawPhases[] = {1.025, 0.692, 1.359, 1.026, 0.693, 1.360};
    const std::uint64_t newerSerials[] = {4551, 4552, 4552, 4553, 4554, 4554};
    double previousDisplayed = 0.0;
    for (std::size_t index = 0; index < std::size(rawPhases); ++index) {
        const double interval = 1000.0 / 60.0;
        const double nextTick = 100.0;
        const double now = nextTick - interval + rawPhases[index] * interval;
        double alpha = 0.0;
        double raw = 0.0;
        Expect(LogicTimerAlpha(nextTick, interval, 1.0, now, delay,
                               alpha, &raw),
               "90 Hz post-deadline phase should remain usable");
        const double displayed = static_cast<double>(newerSerials[index] - 1U) + alpha;
        if (index != 0) {
            Expect(displayed > previousDisplayed,
                   "one-render headroom should not create a held presentation frame");
            Expect(std::abs((displayed - previousDisplayed) - renderStepTicks) < 0.003,
                   "90 Hz presentation progression should remain near two-thirds tick");
        }
        previousDisplayed = displayed;
    }
}

void TestResetPaths() {
    using atdd::clock::PresentationClock;
    using atdd::clock::ResetReason;
    using atdd::clock::SampleKind;

    const double step = PresentationClock::kFixedStepSeconds;
    PresentationClock skipped;
    (void)skipped.Publish(10, 0.0);
    const auto missing = skipped.Publish(12, step);
    Expect(missing.resetReason == ResetReason::missingSnapshots &&
               skipped.SnapshotCount() == 1,
           "a skipped capture must reset to the new endpoint");
    (void)skipped.Publish(13, 2.0 * step);
    const auto recovered = skipped.Evaluate(2.25 * step, 0.25);
    Expect(recovered.kind == SampleKind::interpolated && recovered.olderSerial == 12 &&
               recovered.newerSerial == 13,
           "clock did not recover with the next adjacent endpoint");

    PresentationClock explicitReset;
    (void)explicitReset.Publish(0, 0.0);
    (void)explicitReset.Publish(1, step);
    const auto discontinuity = explicitReset.Publish(2, 2.0 * step, true);
    Expect(discontinuity.resetReason == ResetReason::explicitDiscontinuity &&
               explicitReset.SnapshotCount() == 1,
           "explicit discontinuity should retain only its endpoint");

    PresentationClock backwards;
    (void)backwards.Publish(0, 1.0);
    const auto wallReset = backwards.Publish(1, 0.5);
    Expect(wallReset.resetReason == ResetReason::nonMonotonicWallTime,
           "backwards publish wall time should reset");

    PresentationClock stalled;
    (void)stalled.Publish(0, 0.0);
    (void)stalled.Publish(1, step);
    (void)stalled.Evaluate(2.0 * step, 1.0);
    const auto stall = stalled.Evaluate(
        2.0 * step + PresentationClock::kLongStallSeconds + 0.001, 1.0);
    Expect(stall.kind == SampleKind::reset &&
               stall.resetReason == ResetReason::longStall &&
               stalled.SnapshotCount() == 0,
           "long presentation stall should clear both endpoints");

    PresentationClock invalid;
    const auto invalidPublish =
        invalid.Publish(0, std::numeric_limits<double>::quiet_NaN());
    Expect(!invalidPublish.accepted &&
               invalidPublish.resetReason == ResetReason::nonMonotonicWallTime,
           "non-finite wall time should fail closed");
}

} // namespace

int main() {
    for (const unsigned int rate : {60U, 90U, 119U, 120U, 121U, 144U, 165U, 240U, 360U}) {
        TestCadence(rate);
    }
    TestTwoStateSamplingAndCatchUp();
    TestLogicTimerResidual();
    TestNinetyHzSchedulerHeadroom();
    TestResetPaths();

    if (g_failures != 0) {
        std::cerr << g_failures << " presentation-clock assertion(s) failed\n";
        return 1;
    }
    std::cout << "two-state HPL logic-timer presentation tests passed\n";
    return 0;
}
