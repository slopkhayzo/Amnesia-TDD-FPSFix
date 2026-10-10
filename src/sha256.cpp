#include "sha256.h"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace atdd::crypto {
namespace {

bool NtSucceeded(const NTSTATUS status) {
    return status >= 0;
}

std::wstring HexStatus(const NTSTATUS status) {
    wchar_t buffer[16]{};
    _snwprintf_s(buffer, std::size(buffer), _TRUNCATE, L"0x%08lX",
                 static_cast<unsigned long>(status));
    return buffer;
}

std::wstring HexDigest(const std::vector<unsigned char>& digest) {
    constexpr wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring result;
    result.reserve(digest.size() * 2);
    for (const unsigned char byte : digest) {
        result.push_back(digits[(byte >> 4U) & 0x0FU]);
        result.push_back(digits[byte & 0x0FU]);
    }
    return result;
}

} // namespace

Sha256Result HashFile(const std::wstring& path) {
    Sha256Result result{};
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        result.error = L"CreateFileW failed with Win32 error " + std::to_wstring(GetLastError());
        return result;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> hashObject;
    std::vector<unsigned char> digest;

    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!NtSucceeded(status)) {
        result.error = L"BCryptOpenAlgorithmProvider failed: " + HexStatus(status);
        CloseHandle(file);
        return result;
    }

    DWORD objectLength = 0;
    DWORD digestLength = 0;
    DWORD copied = 0;
    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                               reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                               &copied, 0);
    if (NtSucceeded(status)) {
        status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                                   reinterpret_cast<PUCHAR>(&digestLength), sizeof(digestLength),
                                   &copied, 0);
    }
    if (NtSucceeded(status)) {
        hashObject.resize(objectLength);
        digest.resize(digestLength);
        status = BCryptCreateHash(algorithm, &hash, hashObject.data(), objectLength,
                                  nullptr, 0, 0);
    }

    std::array<unsigned char, 64 * 1024> buffer{};
    while (NtSucceeded(status)) {
        DWORD bytesRead = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
            result.error = L"ReadFile failed with Win32 error " + std::to_wstring(GetLastError());
            status = static_cast<NTSTATUS>(0xC0000001L);
            break;
        }
        if (bytesRead == 0) {
            break;
        }
        status = BCryptHashData(hash, buffer.data(), bytesRead, 0);
    }

    if (NtSucceeded(status)) {
        status = BCryptFinishHash(hash, digest.data(), digestLength, 0);
    }
    if (NtSucceeded(status)) {
        result.ok = true;
        result.digest = HexDigest(digest);
    } else if (result.error.empty()) {
        result.error = L"BCrypt SHA-256 operation failed: " + HexStatus(status);
    }

    if (hash != nullptr) {
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return result;
}

} // namespace atdd::crypto

