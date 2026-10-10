#include <Windows.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::wstring ReplaceExtension(const std::wstring& path, const std::wstring_view extension) {
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) {
        return path + std::wstring(extension);
    }
    return path.substr(0, dot) + std::wstring(extension);
}

std::string ReadFileContents(const std::wstring& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024) {
        CloseHandle(file);
        return {};
    }

    std::string contents(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD bytesRead = 0;
    const bool read = ReadFile(file, contents.data(), static_cast<DWORD>(contents.size()),
                               &bytesRead, nullptr) != FALSE;
    CloseHandle(file);
    if (!read) {
        return {};
    }
    contents.resize(bytesRead);
    return contents;
}

} // namespace

int wmain(const int argc, wchar_t** argv) {
    if (argc != 2) {
        std::wcerr << L"usage: asi_bootstrap_host <plugin.asi>\n";
        return 2;
    }

    std::array<wchar_t, 32768> absolutePath{};
    if (GetFullPathNameW(argv[1], static_cast<DWORD>(absolutePath.size()),
                         absolutePath.data(), nullptr) == 0) {
        std::wcerr << L"GetFullPathNameW failed\n";
        return 3;
    }

    const std::wstring pluginPath(absolutePath.data());
    const std::wstring logPath = ReplaceExtension(pluginPath, L".log");
    DeleteFileW(logPath.c_str());

    const HMODULE plugin = LoadLibraryW(pluginPath.c_str());
    if (plugin == nullptr) {
        std::wcerr << L"LoadLibraryW failed with " << GetLastError() << L"\n";
        return 4;
    }

    constexpr std::string_view expectedMappingFailure = "Hook initialization failed:";
    constexpr std::string_view expectedFailure =
        "Fail-closed: engine hooks were not left installed.";
    bool passed = false;
    for (unsigned int attempt = 0; attempt < 50; ++attempt) {
        const std::string contents = ReadFileContents(logPath);
        if (contents.find(expectedMappingFailure) != std::string::npos &&
            contents.find(expectedFailure) != std::string::npos) {
            passed = true;
            break;
        }
        Sleep(100);
    }

    FreeLibrary(plugin);
    if (!passed) {
        std::wcerr << L"Plugin did not produce the expected fail-closed log\n";
        return 5;
    }

    std::wcout << L"ASI loaded and rejected an incompatible host without installing hooks\n";
    return 0;
}

