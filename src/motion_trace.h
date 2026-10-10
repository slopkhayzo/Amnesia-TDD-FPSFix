#pragma once

#include "camera_interpolation.h"

#include <cstdint>
#include <string>

namespace atdd::motion_trace {

struct Frame {
    std::int64_t qpc = 0;
    std::uint64_t tickSerial = 0;
    std::uint64_t olderSerial = 0;
    std::uint64_t newerSerial = 0;
    double rawAlpha = 0.0;
    double phaseDelayTicks = 0.0;
    double alpha = 0.0;
    camera::Pose authoritative{};
    camera::Pose base{};
    camera::Pose final{};
    std::int32_t pollX = 0;
    std::int32_t pollY = 0;
    std::int64_t deferredX = 0;
    std::int64_t deferredY = 0;
    std::int64_t pendingX = 0;
    std::int64_t pendingY = 0;
    float historyPitch = 0.0F;
    float historyYaw = 0.0F;
    float pendingPitch = 0.0F;
    float pendingYaw = 0.0F;
    std::uint32_t flags = 0;
};

struct FlushResult {
    std::uint64_t written = 0;
    std::uint64_t dropped = 0;
};

[[nodiscard]] bool Initialize(const std::wstring& path,
                              std::int64_t qpcFrequency,
                              std::wstring& error);
void Record(const Frame& frame) noexcept;
[[nodiscard]] FlushResult Flush() noexcept;

} // namespace atdd::motion_trace
