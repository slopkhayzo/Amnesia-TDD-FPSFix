#include "presentation_clock.h"

#include <cmath>
#include <cstdint>
#include <iostream>
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
    std::uint64_t nextSerial = 0;
    double nextTick = 0.0;
    double previousDisplayed = -1.0;
    std::uint64_t previousOlder = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t previousNewer = std::numeric_limits<std::uint64_t>::max();
    double previousAlpha = 0.0;
    unsigned int validSamples = 0;
    unsigned int interpolatedSamples = 0;
    const double presentationStep = 1.0 / static_cast<double>(presentationRate);

    for (double now = 0.0; now <= 4.0; now += presentationStep) {
        while (nextTick <= now + 1.0e-9) {
            const auto published = clock.Publish(nextSerial, nextTick);
            Expect(published.accepted, "cadence tick should publish");
            ++nextSerial;
            nextTick += PresentationClock::kFixedStepSeconds;
        }

        const auto sample = clock.Evaluate(now);
        Expect(sample.alpha >= 0.0 && sample.alpha <= 1.0,
               "alpha must remain bounded at " + std::to_string(presentationRate) + " Hz");
        if (sample.kind == SampleKind::interpolated) {
            ++interpolatedSamples;
            const double olderTime =
                static_cast<double>(sample.olderSerial) * PresentationClock::kFixedStepSeconds;
            const double newerTime =
                static_cast<double>(sample.newerSerial) * PresentationClock::kFixedStepSeconds;
            const double displayed = olderTime + (newerTime - olderTime) * sample.alpha;
            Expect(displayed + 1.0e-8 >= previousDisplayed, "displayed time must not regress at " +
                                                                std::to_string(presentationRate) +
                                                                " Hz");
            if (sample.olderSerial == previousOlder && sample.newerSerial == previousNewer) {
                Expect(sample.alpha + 1.0e-8 >= previousAlpha,
                       "alpha must be monotonic inside one pair at " +
                           std::to_string(presentationRate) + " Hz");
            }
            previousDisplayed = displayed;
            previousOlder = sample.olderSerial;
            previousNewer = sample.newerSerial;
            previousAlpha = sample.alpha;
            ++validSamples;
        } else if (sample.kind == SampleKind::clampedToOldest ||
                   sample.kind == SampleKind::clampedToNewest) {
            const double displayed =
                static_cast<double>(sample.newerSerial) * PresentationClock::kFixedStepSeconds;
            Expect(displayed + 1.0e-8 >= previousDisplayed,
                   "clamped displayed time must not regress");
            previousDisplayed = displayed;
            previousOlder = sample.olderSerial;
            previousNewer = sample.newerSerial;
            previousAlpha = sample.alpha;
            ++validSamples;
        }
        Expect(sample.kind != SampleKind::reset,
               "steady cadence must not reset at " + std::to_string(presentationRate) + " Hz");
    }

    Expect(validSamples > presentationRate * 3, "steady cadence should produce valid samples at " +
                                                    std::to_string(presentationRate) + " Hz");
    Expect(interpolatedSamples > presentationRate,
           "steady cadence should spend time interpolating at " + std::to_string(presentationRate) +
               " Hz");
}

void TestCatchUpAndSkippedObservation() {
    using atdd::clock::PresentationClock;
    using atdd::clock::ResetReason;
    using atdd::clock::SampleKind;

    PresentationClock clock;
    const double step = PresentationClock::kFixedStepSeconds;
    (void)clock.Publish(0, 0.0);
    (void)clock.Publish(1, step);
    (void)clock.Publish(2, 4.0 * step);
    (void)clock.Publish(3, 4.0 * step);
    (void)clock.Publish(4, 4.0 * step);
    Expect(clock.SnapshotCount() == 5, "catch-up burst should retain contiguous snapshots");
    const auto catchUpSample = clock.Evaluate(4.5 * step);
    Expect(catchUpSample.kind == SampleKind::interpolated,
           "catch-up history should remain interpolatable");
    Expect(catchUpSample.newerSerial == 4,
           "warm-up anchoring did not keep the short catch-up on the latest pair");
    const double catchUpTarget =
        static_cast<double>(catchUpSample.olderSerial) * step +
        catchUpSample.alpha *
            static_cast<double>(catchUpSample.newerSerial - catchUpSample.olderSerial) * step;
    Expect(static_cast<double>(4) * step - catchUpTarget <= step + 1.0e-8,
           "short catch-up exceeded the one-tick presentation buffer");

    PresentationClock bridged;
    (void)bridged.Publish(10, 0.0);
    const auto bridge = bridged.Publish(12, 2.0 * step);
    Expect(bridge.resetReason == ResetReason::none,
           "one skipped observation should form a two-tick bridge");
    Expect(bridged.SnapshotCount() == 2, "two-tick bridge should retain both endpoints");
    const auto bridgeSample = bridged.Evaluate(2.5 * step);
    Expect(bridgeSample.kind == SampleKind::interpolated,
           "two-tick bridge should evaluate continuously");

    const auto missing = bridged.Publish(16, 3.0 * step);
    Expect(missing.resetReason == ResetReason::missingSnapshots, "larger serial gap should reset");
    Expect(bridged.SnapshotCount() == 1, "larger serial gap should keep only the new endpoint");
}

void TestFullHistoryWarmupAnchoring() {
    using atdd::clock::PresentationClock;
    using atdd::clock::SampleKind;

    const double step = PresentationClock::kFixedStepSeconds;
    PresentationClock clock;
    // Model a level-entry catch-up burst: eight newer fixed ticks arrive
    // without equivalent wall-time progress, evicting the original anchor
    // from the bounded history.
    for (std::uint64_t serial = 0; serial <= 8; ++serial) {
        const auto published = clock.Publish(serial, 0.0);
        Expect(published.accepted, "catch-up burst endpoint should publish");
    }
    Expect(clock.SnapshotCount() == PresentationClock::kHistoryCapacity,
           "catch-up burst should fill the bounded history");

    const auto rebased = clock.Evaluate(step);
    Expect(rebased.kind == SampleKind::interpolated,
           "full stale history should rebase instead of clamping oldest");
    Expect(!rebased.phaseRebased,
           "history warm-up should prevent a late full-ring phase rebase");
    Expect(rebased.olderSerial == 7 && rebased.newerSerial == 8,
           "phase rebase did not select the normal one-tick-delayed pair");
    Expect(std::abs(rebased.alpha) < 1.0e-8,
           "phase rebase should begin exactly at the delayed older endpoint");

    const auto following = clock.Evaluate(1.5 * step);
    Expect(following.kind == SampleKind::interpolated && !following.phaseRebased,
           "phase rebase should be a one-time correction");
    Expect(following.olderSerial == 7 && following.newerSerial == 8 &&
               std::abs(following.alpha - 0.5) < 1.0e-8,
           "presentation did not advance smoothly after phase rebase");

    (void)clock.Publish(9, 2.0 * step);
    const auto advanced = clock.Evaluate(2.0 * step);
    Expect(advanced.kind == SampleKind::interpolated && !advanced.phaseRebased,
           "phase-corrected clock did not remain stable after a new tick");
    Expect(advanced.olderSerial == 8 && advanced.newerSerial == 9,
           "phase-corrected clock lost its one-tick delay");
}

void TestPostWarmupCatchUpCorrection() {
    using atdd::clock::PresentationClock;
    using atdd::clock::SampleKind;

    const double step = PresentationClock::kFixedStepSeconds;
    PresentationClock clock;
    for (std::uint64_t serial = 0; serial <= 7; ++serial) {
        (void)clock.Publish(serial, static_cast<double>(serial) * step);
    }

    // The clock is warm. Three fixed endpoints now arrive at one wall-time,
    // modeling a short catch-up that is too small to overtake the whole ring.
    (void)clock.Publish(8, 8.0 * step);
    (void)clock.Publish(9, 8.0 * step);
    (void)clock.Publish(10, 8.0 * step);
    const auto corrected = clock.Evaluate(8.25 * step);
    Expect(corrected.kind == SampleKind::interpolated,
           "post-warm-up catch-up should retain an interpolation pair");
    Expect(corrected.phaseRebased,
           "post-warm-up catch-up did not report phase correction");
    Expect(corrected.olderSerial == 9 && corrected.newerSerial == 10 &&
               std::abs(corrected.alpha) < 1.0e-8,
           "post-warm-up correction did not restore the one-tick buffer");

    const auto following = clock.Evaluate(8.75 * step);
    Expect(following.kind == SampleKind::interpolated && !following.phaseRebased &&
               following.olderSerial == 9 && following.newerSerial == 10 &&
               std::abs(following.alpha - 0.5) < 1.0e-8,
           "corrected clock did not advance smoothly inside the latest pair");
}

void TestResetPaths() {
    using atdd::clock::PresentationClock;
    using atdd::clock::ResetReason;
    using atdd::clock::SampleKind;

    const double step = PresentationClock::kFixedStepSeconds;
    PresentationClock explicitReset;
    (void)explicitReset.Publish(0, 0.0);
    (void)explicitReset.Publish(1, step);
    const auto discontinuity = explicitReset.Publish(2, 2.0 * step, true);
    Expect(discontinuity.resetReason == ResetReason::explicitDiscontinuity,
           "explicit discontinuity should reset");
    Expect(explicitReset.SnapshotCount() == 1,
           "explicit discontinuity should retain only its endpoint");

    PresentationClock backwardsWall;
    (void)backwardsWall.Publish(0, 1.0);
    const auto wallReset = backwardsWall.Publish(1, 0.5);
    Expect(wallReset.resetReason == ResetReason::nonMonotonicWallTime,
           "backwards publish wall time should reset");

    PresentationClock backwardsTick;
    (void)backwardsTick.Publish(4, 0.0);
    const auto tickReset = backwardsTick.Publish(4, step);
    Expect(tickReset.resetReason == ResetReason::nonMonotonicTick,
           "repeated tick serial should reset");

    PresentationClock stalled;
    (void)stalled.Publish(0, 0.0);
    (void)stalled.Publish(1, step);
    (void)stalled.Evaluate(2.0 * step);
    const auto stallSample =
        stalled.Evaluate(2.0 * step + PresentationClock::kLongStallSeconds + 0.001);
    Expect(stallSample.kind == SampleKind::reset &&
               stallSample.resetReason == ResetReason::longStall,
           "long presentation stall should reset");
    Expect(stalled.SnapshotCount() == 0, "stall reset should clear stale snapshots");

    PresentationClock invalid;
    const auto invalidPublish = invalid.Publish(0, std::numeric_limits<double>::quiet_NaN());
    Expect(!invalidPublish.accepted &&
               invalidPublish.resetReason == ResetReason::nonMonotonicWallTime,
           "non-finite wall time should fail closed");
}

} // namespace

int main() {
    for (const unsigned int rate : {60U, 90U, 119U, 120U, 121U, 144U, 165U, 240U, 360U}) {
        TestCadence(rate);
    }
    TestCatchUpAndSkippedObservation();
    TestFullHistoryWarmupAnchoring();
    TestPostWarmupCatchUpCorrection();
    TestResetPaths();

    if (g_failures != 0) {
        std::cerr << g_failures << " presentation-clock assertion(s) failed\n";
        return 1;
    }
    std::cout << "presentation clock cadence, bridge, catch-up, and reset tests passed\n";
    return 0;
}
