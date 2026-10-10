#include "bootstrap.h"

#include "config.h"
#include "logger.h"
#include "plugin_version.h"
#include "telemetry.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <string_view>
#include <vector>

namespace atdd {
namespace {

enum class BootstrapState : unsigned int {
    dormant,
    starting,
    disabled,
    rejected,
    readyWithoutHooks,
    readyWithHooks,
};

std::atomic<BootstrapState> g_state{BootstrapState::dormant};

std::wstring ModulePath(const HMODULE module) {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD copied = GetModuleFileNameW(module, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            return {};
        }
        if (copied < buffer.size() - 1) {
            return std::wstring(buffer.data(), copied);
        }
        if (buffer.size() >= 32768) {
            return {};
        }
        buffer.resize(std::min<std::size_t>(buffer.size() * 2, 32768));
    }
}

std::wstring ReplaceExtension(const std::wstring& path, const std::wstring_view extension) {
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) {
        return path + std::wstring(extension);
    }
    return path.substr(0, dot) + std::wstring(extension);
}

std::wstring LocalAppDataLogPath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD copied = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (copied == 0 || copied >= buffer.size()) {
        return {};
    }

    std::wstring directory(buffer.data(), copied);
    directory.append(L"\\AmnesiaTDD.FPSFix");
    if (!CreateDirectoryW(directory.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        return {};
    }
    return directory + L"\\AmnesiaTDD.FPSFix.log";
}

std::wstring BoolText(const bool value) {
    return value ? L"true" : L"false";
}

void LogRequestedFeatures(const config::Settings& settings) {
    const auto& f = settings.features;
    log::Write(log::Level::debug,
               L"Requested features: RenderUnlock=" + BoolText(f.renderUnlock) +
                   L", PresentationLimiter=" + BoolText(f.presentationLimiter) +
                   L", CameraInterpolation=" + BoolText(f.cameraInterpolation) +
                   L", HandsRootInterpolation=" + BoolText(f.handsRootInterpolation) +
                   L", WorldRootInterpolation=" + BoolText(f.worldRootInterpolation) +
                   L", HandsPoseInterpolation=" + BoolText(f.handsPoseInterpolation) +
                   L", WorldPoseInterpolation=" + BoolText(f.worldPoseInterpolation) +
                   L", RagdollPoseInterpolation=" +
                   BoolText(f.ragdollPoseInterpolation) +
                   L", ParticleInterpolation=" + BoolText(f.particleInterpolation) +
                   L", ImageTrailTiming=" + BoolText(f.imageTrailTiming) +
                   L", MouseOverlay=" + BoolText(f.mouseOverlay) +
                   L", TargetFPS=" + std::to_wstring(settings.targetFps) +
                   L", Telemetry=" + BoolText(settings.telemetryEnabled) +
                   L", MotionTrace=" + BoolText(settings.motionTraceEnabled) +
                   L", TelemetryInterval=" +
                   std::to_wstring(settings.telemetryReportIntervalSeconds) + L"s");
}

} // namespace

DWORD WINAPI BootstrapThread(void* moduleParameter) {
    g_state.store(BootstrapState::starting, std::memory_order_release);
    const HMODULE pluginModule = static_cast<HMODULE>(moduleParameter);
    const std::wstring pluginPath = ModulePath(pluginModule);
    if (pluginPath.empty()) {
        OutputDebugStringW(L"[AmnesiaTDD.FPSFix] Could not resolve plugin module path.\n");
        g_state.store(BootstrapState::rejected, std::memory_order_release);
        return 1;
    }

    const std::wstring iniPath = ReplaceExtension(pluginPath, L".ini");
    const std::wstring logPath = ReplaceExtension(pluginPath, L".log");
    const std::wstring motionTracePath = ReplaceExtension(pluginPath, L".motion.csv");
    if (!log::Initialize(logPath)) {
        const std::wstring fallbackLogPath = LocalAppDataLogPath();
        if (fallbackLogPath.empty() || !log::Initialize(fallbackLogPath)) {
            OutputDebugStringW(
                L"[AmnesiaTDD.FPSFix] Could not open either diagnostic log path.\n");
        } else {
            log::Write(log::Level::warning,
                       L"Could not write the sibling log; using: " + fallbackLogPath);
        }
    }
    log::Write(log::Level::info,
               std::wstring(AMNESIA_TDD_FPS_FIX_NAME) + L" " +
                   AMNESIA_TDD_FPS_FIX_VERSION + L" bootstrap starting (x86)");
    log::Write(log::Level::debug, L"Plugin path: " + pluginPath);
    log::Write(log::Level::debug, L"INI path: " + iniPath);

    const config::LoadResult loaded = config::Load(iniPath);
    if (!loaded.ok) {
        log::Write(log::Level::error, loaded.error);
        log::Write(log::Level::error, L"Fail-closed: no engine hooks were installed.");
        g_state.store(BootstrapState::rejected, std::memory_order_release);
        return 2;
    }

    log::SetDiagnosticsEnabled(loaded.settings.diagnostics);
    if (!loaded.settings.enabled) {
        log::Write(log::Level::info, L"Plugin disabled by [General] Enabled=false.");
        g_state.store(BootstrapState::disabled, std::memory_order_release);
        return 0;
    }
    LogRequestedFeatures(loaded.settings);

    const std::wstring executablePath = ModulePath(nullptr);
    if (executablePath.empty()) {
        log::Write(log::Level::warning,
                   L"Could not resolve the host path; compatibility will be determined "
                   L"from the mapped PE image.");
    } else {
        log::Write(log::Level::debug, L"Host path: " + executablePath);
    }

    config::Features unimplementedFeatures = loaded.settings.features;
    unimplementedFeatures.renderUnlock = false;
    unimplementedFeatures.cameraInterpolation = false;
    unimplementedFeatures.handsRootInterpolation = false;
    unimplementedFeatures.worldRootInterpolation = false;
    unimplementedFeatures.handsPoseInterpolation = false;
    unimplementedFeatures.worldPoseInterpolation = false;
    unimplementedFeatures.particleInterpolation = false;
    unimplementedFeatures.imageTrailTiming = false;
    unimplementedFeatures.mouseOverlay = false;
    if (config::AnyBehavioralFeatureEnabled(unimplementedFeatures)) {
        log::Write(log::Level::warning,
                   L"One or more requested behavioral features are not implemented in "
                   L"this milestone and will remain disabled.");
    }

    if (loaded.settings.telemetryEnabled || loaded.settings.motionTraceEnabled ||
        loaded.settings.features.renderUnlock ||
        loaded.settings.features.cameraInterpolation ||
        loaded.settings.features.handsRootInterpolation ||
        loaded.settings.features.worldRootInterpolation ||
        loaded.settings.features.handsPoseInterpolation ||
        loaded.settings.features.worldPoseInterpolation ||
        loaded.settings.features.particleInterpolation ||
        loaded.settings.features.imageTrailTiming ||
        loaded.settings.features.mouseOverlay) {
        std::wstring hookError;
        if (!telemetry::Initialize(GetModuleHandleW(nullptr),
                                   loaded.settings.telemetryEnabled,
                                   loaded.settings.features.renderUnlock,
                                   loaded.settings.features.cameraInterpolation,
                                   loaded.settings.features.handsRootInterpolation,
                                   loaded.settings.features.worldRootInterpolation,
                                   loaded.settings.features.handsPoseInterpolation,
                                   loaded.settings.features.worldPoseInterpolation,
                                   loaded.settings.features.ragdollPoseInterpolation,
                                   loaded.settings.features.particleInterpolation,
                                   loaded.settings.features.imageTrailTiming,
                                   loaded.settings.features.mouseOverlay,
                                   loaded.settings.motionTraceEnabled,
                                   motionTracePath,
                                   loaded.settings.telemetryReportIntervalSeconds,
                                   hookError)) {
            log::Write(log::Level::error,
                       L"Hook initialization failed: " + hookError);
            log::Write(log::Level::error,
                       L"Fail-closed: engine hooks were not left installed.");
            g_state.store(BootstrapState::rejected, std::memory_order_release);
            return 3;
        }
        log::Write(log::Level::info,
                   L"Compatible x86 executable signatures accepted. Requested hooks are active.");
        g_state.store(BootstrapState::readyWithHooks, std::memory_order_release);
        return 0;
    }

    log::Write(log::Level::info,
               L"No engine hooks were requested; executable signature validation was skipped.");
    g_state.store(BootstrapState::readyWithoutHooks, std::memory_order_release);
    return 0;
}

} // namespace atdd
