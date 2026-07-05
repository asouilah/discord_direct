#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <unordered_set>
#include <mutex>
#include <cstdint>
#include <cstring>
#include <random>
#include "hook.h"

#pragma comment(lib, "ws2_32.lib")

namespace {

// ------------------------------------------------------------------
// Minimal x64 inline hook: overwrite the first 12 bytes of the target
// function with `mov rax, imm64; jmp rax`, redirecting to our hook.
// The original bytes are preserved in a small executable trampoline
// so the real function can still be called from inside the hook.
// ------------------------------------------------------------------

struct HookInfo {
    void* target = nullptr;
    void* trampoline = nullptr;
    unsigned char original[16] = {};
    size_t patchLen = 0;
    bool installed = false;
};

constexpr size_t kPatchSize = 12; // 2 (mov opcode) + 8 (imm64) + 2 (jmp rax)

HookInfo g_sendtoHook;
HookInfo g_wsaSendToHook;

void WriteAbsoluteJump(unsigned char* at, void* dest) {
    at[0] = 0x48; at[1] = 0xB8;               // mov rax, imm64
    memcpy(at + 2, &dest, sizeof(void*));
    at[10] = 0xFF; at[11] = 0xE0;             // jmp rax
}

bool InstallHook(HookInfo& info, void* target, void* hookFn) {
    info.target = target;
    info.patchLen = kPatchSize;

    info.trampoline = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!info.trampoline) return false;

    DWORD oldProt;
    if (!VirtualProtect(target, info.patchLen, PAGE_EXECUTE_READWRITE, &oldProt))
        return false;

    memcpy(info.original, target, info.patchLen);

    unsigned char* tramp = reinterpret_cast<unsigned char*>(info.trampoline);
    memcpy(tramp, info.original, info.patchLen);
    WriteAbsoluteJump(tramp + info.patchLen, reinterpret_cast<unsigned char*>(target) + info.patchLen);

    WriteAbsoluteJump(reinterpret_cast<unsigned char*>(target), hookFn);

    VirtualProtect(target, info.patchLen, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), target, info.patchLen);
    info.installed = true;
    return true;
}

void RemoveHookImpl(HookInfo& info) {
    if (!info.installed) return;
    DWORD oldProt;
    if (VirtualProtect(info.target, info.patchLen, PAGE_EXECUTE_READWRITE, &oldProt)) {
        memcpy(info.target, info.original, info.patchLen);
        VirtualProtect(info.target, info.patchLen, oldProt, &oldProt);
    }
    if (info.trampoline) VirtualFree(info.trampoline, 0, MEM_RELEASE);
    info.installed = false;
}

// ------------------------------------------------------------------
// Track which (socket, destination) pairs we've already seen so we
// only inject one padding packet per new voice connection, not per
// real-time voice packet.
// ------------------------------------------------------------------

std::mutex g_mutex;
std::unordered_set<uint64_t> g_seenDest;

uint64_t KeyFor(SOCKET s, const sockaddr* addr) {
    uint64_t key = static_cast<uint64_t>(s) << 32;
    if (addr->sa_family == AF_INET) {
        auto* in = reinterpret_cast<const sockaddr_in*>(addr);
        key ^= (static_cast<uint64_t>(in->sin_addr.s_addr) << 16) ^ in->sin_port;
    }
    return key;
}

bool FirstTimeSeen(SOCKET s, const sockaddr* addr) {
    uint64_t key = KeyFor(s, addr);
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_seenDest.insert(key).second;
}

using SendtoFn = int(WINAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
using WsaSendToFn = int(WINAPI*)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const sockaddr*, int, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);

// Sends one random-payload UDP packet to `to` ahead of the real traffic.
// This is what actually disrupts DPI signature matching on the initial
// handshake of Discord's voice protocol, without touching the real
// voice payloads that follow.
void SendPaddingPacket(SOCKET s, const sockaddr* to, int tolen, SendtoFn realSendto) {
    static thread_local std::mt19937 rng(GetTickCount() ^ static_cast<unsigned>(reinterpret_cast<uintptr_t>(&rng)));
    unsigned char buf[40];
    for (auto& b : buf) b = static_cast<unsigned char>(rng() & 0xFF);
    realSendto(s, reinterpret_cast<char*>(buf), sizeof(buf), 0, to, tolen);
}

int WINAPI HookedSendto(SOCKET s, const char* buf, int len, int flags, const sockaddr* to, int tolen) {
    auto real = reinterpret_cast<SendtoFn>(g_sendtoHook.trampoline);
    if (to && to->sa_family == AF_INET && len > 0 && FirstTimeSeen(s, to)) {
        SendPaddingPacket(s, to, tolen, real);
    }
    return real(s, buf, len, flags, to, tolen);
}

int WINAPI HookedWSASendTo(SOCKET s, LPWSABUF lpBuffers, DWORD bufCount, LPDWORD lpBytesSent,
                            DWORD flags, const sockaddr* to, int tolen,
                            LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    auto real = reinterpret_cast<WsaSendToFn>(g_wsaSendToHook.trampoline);
    if (to && to->sa_family == AF_INET && bufCount > 0 && FirstTimeSeen(s, to)) {
        // Use the real (unhooked) sendto for the padding packet, falling back
        // to the live import if sendto itself wasn't hooked in this process.
        SendtoFn realSendto = g_sendtoHook.trampoline
            ? reinterpret_cast<SendtoFn>(g_sendtoHook.trampoline)
            : reinterpret_cast<SendtoFn>(::sendto);
        SendPaddingPacket(s, to, tolen, realSendto);
    }
    return real(s, lpBuffers, bufCount, lpBytesSent, flags, to, tolen, overlapped, completion);
}

} // namespace

bool InstallDirectModeHooks() {
    HMODULE ws2 = GetModuleHandleW(L"ws2_32.dll");
    if (!ws2) ws2 = LoadLibraryW(L"ws2_32.dll");
    if (!ws2) return false;

    void* sendtoAddr = reinterpret_cast<void*>(GetProcAddress(ws2, "sendto"));
    void* wsaSendToAddr = reinterpret_cast<void*>(GetProcAddress(ws2, "WSASendTo"));

    bool ok1 = sendtoAddr && InstallHook(g_sendtoHook, sendtoAddr, reinterpret_cast<void*>(HookedSendto));
    bool ok2 = wsaSendToAddr && InstallHook(g_wsaSendToHook, wsaSendToAddr, reinterpret_cast<void*>(HookedWSASendTo));
    return ok1 || ok2;
}

void RemoveDirectModeHooks() {
    RemoveHookImpl(g_sendtoHook);
    RemoveHookImpl(g_wsaSendToHook);
}
