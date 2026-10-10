#pragma once

#include <Windows.h>

#include <string>

namespace atdd::telemetry {

[[nodiscard]] bool Initialize(HMODULE executable,
                              bool telemetryEnabled,
                              bool renderUnlockEnabled,
                              bool cameraInterpolationEnabled,
                              bool handsInterpolationEnabled,
                              bool worldInterpolationEnabled,
                              bool handsPoseInterpolationEnabled,
                              bool worldPoseInterpolationEnabled,
                              bool ragdollPoseInterpolationEnabled,
                              bool particleInterpolationEnabled,
                              bool imageTrailTimingEnabled,
                              bool mouseAuditEnabled,
                              bool motionTraceEnabled,
                              const std::wstring& motionTracePath,
                              unsigned int reportIntervalSeconds,
                              std::wstring& error);

} // namespace atdd::telemetry
