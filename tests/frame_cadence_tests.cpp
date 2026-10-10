#include "frame_cadence.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void Expect(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

} // namespace

int main() {
    atdd::cadence::FrameCadence cadence;
    for (unsigned int index = 0; index < 100; ++index) {
        cadence.RecordIntervalMicroseconds(index < 99 ? 2'778 : 4'000);
    }
    cadence.RecordIntervalMicroseconds(120'000);
    cadence.RecordIntervalMicroseconds(300'000);
    cadence.RecordIntervalMicroseconds(0);

    const auto snapshot = cadence.TakeSnapshot();
    Expect(snapshot.samples == 101, "ordinary and overflow samples should be retained");
    Expect(snapshot.p50Milliseconds > 2.77 && snapshot.p50Milliseconds < 2.79,
           "360 Hz median should resolve within one 10 us bin");
    Expect(snapshot.p95Milliseconds > 2.77 && snapshot.p95Milliseconds < 2.79,
           "360 Hz p95 should remain in the steady bin");
    Expect(snapshot.p99Milliseconds >= 3.99 && snapshot.p99Milliseconds <= 100.01,
           "p99 should expose the injected long frame");
    Expect(snapshot.maxMilliseconds > 99.99 && snapshot.maxMilliseconds < 100.01,
           "overflow samples should clamp to the documented final bin");
    Expect(snapshot.overflow == 1, "100-250 ms interval should report histogram overflow");
    Expect(snapshot.longGaps == 1, "interval above 250 ms should report a long gap");
    Expect(snapshot.invalid == 1, "zero interval should report invalid input");

    const auto cleared = cadence.TakeSnapshot();
    Expect(cleared.samples == 0 && cleared.longGaps == 0 && cleared.overflow == 0 &&
               cleared.invalid == 0,
           "snapshot collection should atomically retire interval data");

    atdd::cadence::FrameCadence qpc;
    qpc.RecordQpc(1'000, 1'000);
    qpc.RecordQpc(1'003, 1'000);
    const auto qpcSnapshot = qpc.TakeSnapshot();
    Expect(qpcSnapshot.samples == 1 && std::fabs(qpcSnapshot.meanMilliseconds - 3.005) < 0.011,
           "QPC conversion should record the elapsed presentation interval");

    if (g_failures != 0) {
        std::cerr << g_failures << " frame-cadence assertion(s) failed\n";
        return 1;
    }
    std::cout << "frame cadence histogram and retirement tests passed\n";
    return 0;
}
