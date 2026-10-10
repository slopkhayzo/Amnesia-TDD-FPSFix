#pragma once

#include <string>
#include <string_view>

namespace atdd::log {

enum class Level {
    debug,
    info,
    warning,
    error,
};

[[nodiscard]] bool Initialize(const std::wstring& path);
void SetDiagnosticsEnabled(bool enabled);
void Write(Level level, std::wstring_view message);

} // namespace atdd::log

