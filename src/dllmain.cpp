#include "bootstrap.h"

#include <Windows.h>

BOOL WINAPI DllMain(const HINSTANCE instance, const DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }

    DisableThreadLibraryCalls(instance);
    const HANDLE bootstrap = CreateThread(nullptr, 0, atdd::BootstrapThread,
                                          instance, 0, nullptr);
    if (bootstrap == nullptr) {
        OutputDebugStringW(
            L"[AmnesiaTDD.FPSFix] Could not create deferred bootstrap thread; "
            L"no hooks were installed.\n");
        return TRUE;
    }
    CloseHandle(bootstrap);
    return TRUE;
}

