#include <windows.h>
#include "hook.h"

namespace {

DWORD WINAPI InstallDelayed(LPVOID) {
    // Give Discord's own startup (which loads ws2_32.dll, etc.) a moment
    // to finish before we patch it.
    Sleep(500);
    InstallDirectModeHooks();
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, InstallDelayed, nullptr, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        RemoveDirectModeHooks();
        break;
    }
    return TRUE;
}
