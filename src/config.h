#pragma once

#include <string>

namespace atdd::config {

struct Features {
    bool renderUnlock = false;
    bool presentationLimiter = false;
    bool cameraInterpolation = false;
    bool handsRootInterpolation = false;
    bool worldRootInterpolation = false;
    bool handsPoseInterpolation = false;
    bool worldPoseInterpolation = false;
    // Subordinate to WorldPoseInterpolation. Kept enabled by default so older
    // INIs retain articulated-prop/ragdoll smoothing unless explicitly isolated.
    bool ragdollPoseInterpolation = true;
    bool particleInterpolation = false;
    bool imageTrailTiming = false;
    bool mouseOverlay = false;
};

struct Settings {
    bool enabled = true;
    bool diagnostics = true;
    bool telemetryEnabled = true;
    bool motionTraceEnabled = false;
    unsigned int telemetryReportIntervalSeconds = 5;
    Features features{};
    unsigned int targetFps = 0;
};

struct LoadResult {
    bool ok = false;
    Settings settings{};
    std::wstring error;
};

[[nodiscard]] LoadResult Load(const std::wstring& path);
[[nodiscard]] bool AnyBehavioralFeatureEnabled(const Features& features);

} // namespace atdd::config
