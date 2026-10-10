#include "image_trail_timing.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

double StockAlpha(const double amount, const double frameTimeSeconds) {
    return std::exp(-0.015 * amount / frameTimeSeconds);
}

void TestReferenceRateIsUnchanged() {
    float corrected = -1.0F;
    Require(atdd::image_trail::CorrectAmount(1.6F, 1.0F / 60.0F, corrected),
            "60 Hz correction failed");
    Require(std::abs(corrected - 1.6F) < 1.0e-5F,
            "60 Hz amount was not preserved");
}

void TestRealTimeRetentionMatchesSixtyHertz() {
    constexpr double amount = 1.6;
    constexpr std::array<double, 6> rates{60.0, 90.0, 120.0, 240.0, 350.0, 360.0};
    const double referenceAlpha = StockAlpha(amount, 1.0 / 60.0);
    const double referenceOneSecondRetention =
        std::pow(1.0 - referenceAlpha, 60.0);

    for (const double rate : rates) {
        float corrected = -1.0F;
        Require(atdd::image_trail::CorrectAmount(
                    static_cast<float>(amount), static_cast<float>(1.0 / rate), corrected),
                "high-rate correction failed");
        const double correctedAlpha = StockAlpha(corrected, 1.0 / rate);
        const double oneSecondRetention = std::pow(1.0 - correctedAlpha, rate);
        Require(std::abs(oneSecondRetention - referenceOneSecondRetention) < 1.0e-7,
                "real-time retention diverged from the stock 60 Hz effect");
    }
}

void TestVariableIntervalsComposeInRealTime() {
    constexpr float amount = 0.7F;
    constexpr std::array<float, 5> intervals{
        1.0F / 350.0F, 1.0F / 120.0F, 1.0F / 240.0F,
        1.0F / 90.0F, 1.0F / 360.0F};
    double retention = 1.0;
    double elapsed = 0.0;
    for (const float interval : intervals) {
        float corrected = -1.0F;
        Require(atdd::image_trail::CorrectAmount(amount, interval, corrected),
                "variable-interval correction failed");
        retention *= 1.0 - StockAlpha(corrected, interval);
        elapsed += interval;
    }

    const double referenceRetention =
        std::pow(1.0 - StockAlpha(amount, 1.0 / 60.0), elapsed * 60.0);
    Require(std::abs(retention - referenceRetention) < 1.0e-7,
            "variable frame intervals did not compose by elapsed real time");
}

void TestZeroAndInvalidInputs() {
    float corrected = -1.0F;
    Require(atdd::image_trail::CorrectAmount(0.0F, 1.0F / 350.0F, corrected) &&
                corrected == 0.0F,
            "zero trail amount was not preserved");
    Require(!atdd::image_trail::CorrectAmount(-0.1F, 1.0F / 60.0F, corrected),
            "negative trail amount was accepted");
    Require(!atdd::image_trail::CorrectAmount(1.0F, 0.0F, corrected),
            "zero frame interval was accepted");
    Require(!atdd::image_trail::CorrectAmount(
                std::numeric_limits<float>::quiet_NaN(), 1.0F / 60.0F, corrected),
            "non-finite trail amount was accepted");
    Require(!atdd::image_trail::CorrectAmount(
                1.0F, std::numeric_limits<float>::infinity(), corrected),
            "non-finite frame interval was accepted");
}

} // namespace

int main() {
    TestReferenceRateIsUnchanged();
    TestRealTimeRetentionMatchesSixtyHertz();
    TestVariableIntervalsComposeInRealTime();
    TestZeroAndInvalidInputs();
    std::cout << "image_trail_timing_tests: PASS\n";
    return EXIT_SUCCESS;
}
