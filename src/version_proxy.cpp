// This DLL side-loads as version.dll, so it must still behave like the
// real one for anything that queries file version info. Every export
// here just forwards to the genuine version.dll in System32.

#include <windows.h>
#include <string>

namespace {

HMODULE RealVersionDll() {
    static HMODULE h = nullptr;
    if (!h) {
        wchar_t sysDir[MAX_PATH];
        GetSystemDirectoryW(sysDir, MAX_PATH);
        std::wstring path = std::wstring(sysDir) + L"\\version.dll";
        h = LoadLibraryW(path.c_str());
    }
    return h;
}

template <typename T>
T GetReal(const char* name) {
    HMODULE h = RealVersionDll();
    return h ? reinterpret_cast<T>(GetProcAddress(h, name)) : nullptr;
}

} // namespace

extern "C" {

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoW(LPCWSTR file, DWORD handle, DWORD len, LPVOID data) {
    using Fn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    auto fn = GetReal<Fn>("GetFileVersionInfoW");
    return fn ? fn(file, handle, len, data) : FALSE;
}

__declspec(dllexport) BOOL WINAPI GetFileVersionInfoA(LPCSTR file, DWORD handle, DWORD len, LPVOID data) {
    using Fn = BOOL(WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID);
    auto fn = GetReal<Fn>("GetFileVersionInfoA");
    return fn ? fn(file, handle, len, data) : FALSE;
}

__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR file, LPDWORD handle) {
    using Fn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    auto fn = GetReal<Fn>("GetFileVersionInfoSizeW");
    return fn ? fn(file, handle) : 0;
}

__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR file, LPDWORD handle) {
    using Fn = DWORD(WINAPI*)(LPCSTR, LPDWORD);
    auto fn = GetReal<Fn>("GetFileVersionInfoSizeA");
    return fn ? fn(file, handle) : 0;
}

__declspec(dllexport) BOOL WINAPI VerQueryValueW(LPCVOID block, LPCWSTR subBlock, LPVOID* buffer, PUINT len) {
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    auto fn = GetReal<Fn>("VerQueryValueW");
    return fn ? fn(block, subBlock, buffer, len) : FALSE;
}

__declspec(dllexport) BOOL WINAPI VerQueryValueA(LPCVOID block, LPCSTR subBlock, LPVOID* buffer, PUINT len) {
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    auto fn = GetReal<Fn>("VerQueryValueA");
    return fn ? fn(block, subBlock, buffer, len) : FALSE;
}

} // extern "C"
