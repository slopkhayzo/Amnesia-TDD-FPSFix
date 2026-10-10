#include "config.h"

#include <Windows.h>

#include <algorithm>
#include <cerrno>
#include <cwchar>
#include <cwctype>
#include <string_view>

namespace atdd::config {
namespace {

constexpr wchar_t kMissingValue[] = L"{7C190868-382E-4A3F-BB29-MISSING}";

std::wstring TrimAndLower(std::wstring value) {
    const auto isNotSpace = [](const wchar_t ch) { return std::iswspace(ch) == 0; };
    const auto first = std::find_if(value.begin(), value.end(), isNotSpace);
    const auto last = std::find_if(value.rbegin(), value.rend(), isNotSpace).base();
    if (first >= last) {
        return {};
    }

    std::wstring normalized(first, last);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](const wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return normalized;
}

bool ReadValue(const std::wstring& path,
               const wchar_t* section,
               const wchar_t* key,
               std::wstring& value) {
    wchar_t buffer[128]{};
    const DWORD copied = GetPrivateProfileStringW(
        section, key, kMissingValue, buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());
    if (copied == 0 || std::wstring_view(buffer) == kMissingValue) {
        return false;
    }
    value.assign(buffer, copied);
    return true;
}

bool ParseBool(const std::wstring& raw, bool& value) {
    const std::wstring normalized = TrimAndLower(raw);
    if (normalized == L"1" || normalized == L"true" || normalized == L"yes" ||
        normalized == L"on") {
        value = true;
        return true;
    }
    if (normalized == L"0" || normalized == L"false" || normalized == L"no" ||
        normalized == L"off") {
        value = false;
        return true;
    }
    return false;
}

bool ReadBool(const std::wstring& path,
              const wchar_t* section,
              const wchar_t* key,
              const bool defaultValue,
              bool& value,
              std::wstring& error) {
    std::wstring raw;
    if (!ReadValue(path, section, key, raw)) {
        value = defaultValue;
        return true;
    }
    if (ParseBool(raw, value)) {
        return true;
    }
    error = L"Invalid boolean value for [" + std::wstring(section) + L"] " + key + L": " + raw;
    return false;
}

bool ReadTargetFps(const std::wstring& path, unsigned int& value, std::wstring& error) {
    std::wstring raw;
    if (!ReadValue(path, L"Timing", L"TargetFPS", raw)) {
        value = 0;
        return true;
    }

    const std::wstring normalized = TrimAndLower(raw);
    if (normalized.empty() || normalized.front() == L'-') {
        error = L"Invalid [Timing] TargetFPS: " + raw;
        return false;
    }

    errno = 0;
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(normalized.c_str(), &end, 10);
    if (errno == ERANGE || end == normalized.c_str() || *end != L'\0' || parsed > 1000UL) {
        error = L"Invalid [Timing] TargetFPS (expected 0..1000): " + raw;
        return false;
    }
    value = static_cast<unsigned int>(parsed);
    return true;
}

bool ReadBoundedUnsigned(const std::wstring& path,
                         const wchar_t* section,
                         const wchar_t* key,
                         const unsigned int defaultValue,
                         const unsigned int minimum,
                         const unsigned int maximum,
                         unsigned int& value,
                         std::wstring& error) {
    std::wstring raw;
    if (!ReadValue(path, section, key, raw)) {
        value = defaultValue;
        return true;
    }

    const std::wstring normalized = TrimAndLower(raw);
    if (normalized.empty() || normalized.front() == L'-') {
        error = L"Invalid unsigned value for [" + std::wstring(section) + L"] " + key +
                L": " + raw;
        return false;
    }

    errno = 0;
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(normalized.c_str(), &end, 10);
    if (errno == ERANGE || end == normalized.c_str() || *end != L'\0' ||
        parsed < minimum || parsed > maximum) {
        error = L"Out-of-range value for [" + std::wstring(section) + L"] " + key +
                L" (expected " + std::to_wstring(minimum) + L".." +
                std::to_wstring(maximum) + L"): " + raw;
        return false;
    }
    value = static_cast<unsigned int>(parsed);
    return true;
}

} // namespace

LoadResult Load(const std::wstring& path) {
    LoadResult result{};
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        result.error = L"Configuration file not found: " + path;
        return result;
    }

    auto& settings = result.settings;
    if (!ReadBool(path, L"General", L"Enabled", true, settings.enabled, result.error) ||
        !ReadBool(path, L"General", L"Diagnostics", true, settings.diagnostics, result.error) ||
        !ReadBool(path, L"Telemetry", L"Enabled", true,
                  settings.telemetryEnabled, result.error) ||
        !ReadBool(path, L"Telemetry", L"MotionTrace", false,
                  settings.motionTraceEnabled, result.error) ||
        !ReadBoundedUnsigned(path, L"Telemetry", L"ReportIntervalSeconds", 5, 1, 60,
                             settings.telemetryReportIntervalSeconds, result.error) ||
        !ReadBool(path, L"Features", L"RenderUnlock", false,
                  settings.features.renderUnlock, result.error) ||
        !ReadBool(path, L"Features", L"PresentationLimiter", false,
                  settings.features.presentationLimiter, result.error) ||
        !ReadBool(path, L"Features", L"CameraInterpolation", false,
                  settings.features.cameraInterpolation, result.error) ||
        !ReadBool(path, L"Features", L"HandsRootInterpolation", false,
                  settings.features.handsRootInterpolation, result.error) ||
        !ReadBool(path, L"Features", L"WorldRootInterpolation", false,
                  settings.features.worldRootInterpolation, result.error) ||
        !ReadBool(path, L"Features", L"HandsPoseInterpolation", false,
                  settings.features.handsPoseInterpolation, result.error) ||
        !ReadBool(path, L"Features", L"WorldPoseInterpolation", false,
                  settings.features.worldPoseInterpolation, result.error) ||
        !ReadBool(path, L"Features", L"RagdollPoseInterpolation", true,
                  settings.features.ragdollPoseInterpolation, result.error) ||
        !ReadBool(path, L"Features", L"ParticleInterpolation", false,
                  settings.features.particleInterpolation, result.error) ||
        !ReadBool(path, L"Features", L"ImageTrailTiming", false,
                  settings.features.imageTrailTiming, result.error) ||
        !ReadBool(path, L"Features", L"MouseOverlay", false,
                  settings.features.mouseOverlay, result.error) ||
        !ReadTargetFps(path, settings.targetFps, result.error)) {
        return result;
    }

    result.ok = true;
    return result;
}

bool AnyBehavioralFeatureEnabled(const Features& features) {
    return features.renderUnlock || features.presentationLimiter ||
           features.cameraInterpolation || features.handsRootInterpolation ||
           features.worldRootInterpolation || features.handsPoseInterpolation ||
           features.worldPoseInterpolation || features.particleInterpolation ||
           features.imageTrailTiming || features.mouseOverlay;
}

} // namespace atdd::config
