#pragma once

#include <string>

namespace atdd::crypto {

struct Sha256Result {
    bool ok = false;
    std::wstring digest;
    std::wstring error;
};

[[nodiscard]] Sha256Result HashFile(const std::wstring& path);

} // namespace atdd::crypto

