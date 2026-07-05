// installer.cpp - single-exe installer for drover-direct, similar to the
// original drover.exe. version.dll is embedded as a resource (see
// resource.rc) so this one .exe is all you need to distribute/run.
//
// It does NOT inject into a running Discord process. It:
//   1. Checks whether Discord is currently running and alerts you if so
//      (a locked file can't be overwritten).
//   2. Offers to close Discord gracefully.
//   3. Extracts the embedded version.dll and copies it into the current
//      Discord app-X.X.X folder, so Windows side-loads it next launch.

#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <algorithm>
#include "resource.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr int ID_INSTALL = 1001;
constexpr int ID_UNINSTALL = 1002;
constexpr int ID_STATUS = 1003;
constexpr int ID_TITLE = 1004;
constexpr int ID_SUBTITLE = 1005;
constexpr int ID_GITHUB_LINK = 1006;
constexpr wchar_t kDiscordExe[] = L"Discord.exe";
constexpr wchar_t kGithubUrl[] = L"https://github.com/asouilah/discord_direct";

// ---- palette -------------------------------------------------------
constexpr COLORREF kBg          = RGB(0xF6, 0xF7, 0xFB);
constexpr COLORREF kPanelBg     = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF kPanelBorder = RGB(0xE4, 0xE6, 0xEF);
constexpr COLORREF kAccent      = RGB(0x58, 0x65, 0xF2); // Discord-ish blurple
constexpr COLORREF kAccentDark  = RGB(0x46, 0x52, 0xD6);
constexpr COLORREF kTitleText   = RGB(0x23, 0x25, 0x2A);
constexpr COLORREF kSubText     = RGB(0x6B, 0x6E, 0x7B);
constexpr COLORREF kStatusText  = RGB(0x3A, 0x3C, 0x45);
constexpr COLORREF kOutlineText = RGB(0x2E, 0x30, 0x38);
constexpr COLORREF kOutlineBg   = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF kOutlineBrd  = RGB(0xD8, 0xDA, 0xE3);

HWND g_statusLabel = nullptr;
HWND g_mainWnd = nullptr;
HWND g_installBtn = nullptr;
HWND g_uninstallBtn = nullptr;
HFONT g_fontTitle = nullptr;
HFONT g_fontSubtitle = nullptr;
HFONT g_fontButton = nullptr;
HFONT g_fontStatus = nullptr;
HBRUSH g_bgBrush = nullptr;
HBRUSH g_panelBrush = nullptr;

void SetStatus(const std::wstring& text) {
    if (g_statusLabel) SetWindowTextW(g_statusLabel, text.c_str());
}

void Alert(const std::wstring& text, UINT iconFlag = MB_OK | MB_ICONINFORMATION) {
    MessageBoxW(g_mainWnd, text.c_str(), L"Discord Direct", iconFlag);
}

// ---------------------------------------------------------------------
// Process / Discord helpers
// ---------------------------------------------------------------------

bool IsDiscordRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, kDiscordExe) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

bool TryCloseDiscordGracefully() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    std::vector<DWORD> discordPids;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, kDiscordExe) == 0) {
                discordPids.push_back(pe.th32ProcessID);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    EnumWindows([](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* pids = reinterpret_cast<std::vector<DWORD>*>(lparam);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (std::find(pids->begin(), pids->end(), pid) != pids->end() && IsWindowVisible(hwnd)) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&discordPids));

    for (int i = 0; i < 10; ++i) {
        Sleep(300);
        if (!IsDiscordRunning()) return true;
    }
    return !IsDiscordRunning();
}

bool EnsureDiscordClosed() {
    if (!IsDiscordRunning()) return true;

    int choice = MessageBoxW(g_mainWnd,
        L"Discord is currently running.\n\n"
        L"It needs to be fully closed before installing, otherwise the file "
        L"is locked and the copy will fail.\n\n"
        L"Close Discord now?",
        L"Discord Direct", MB_YESNO | MB_ICONWARNING);

    if (choice != IDYES) {
        Alert(L"Install cancelled. Close Discord manually and try again.", MB_OK | MB_ICONWARNING);
        return false;
    }

    if (!TryCloseDiscordGracefully()) {
        Alert(L"Couldn't confirm Discord fully closed (it may still be "
              L"running in the background, e.g. system tray).\n\n"
              L"Please close it manually via Task Manager and try again.",
              MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

std::wstring FindDiscordAppFolder() {
    wchar_t localAppData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData)))
        return L"";

    std::wstring discordRoot = std::wstring(localAppData) + L"\\Discord";
    std::wstring searchPattern = discordRoot + L"\\app-*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    std::vector<std::wstring> candidates;
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                std::wstring full = discordRoot + L"\\" + fd.cFileName;
                if (GetFileAttributesW((full + L"\\Discord.exe").c_str()) != INVALID_FILE_ATTRIBUTES) {
                    candidates.push_back(full);
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    if (candidates.empty()) return L"";
    std::sort(candidates.begin(), candidates.end());
    return candidates.back();
}

bool ExtractEmbeddedDll(const std::wstring& destPath) {
    HMODULE self = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(self, MAKEINTRESOURCEW(IDR_VERSION_DLL), RT_RCDATA);
    if (!res) return false;
    HGLOBAL loaded = LoadResource(self, res);
    if (!loaded) return false;
    void* data = LockResource(loaded);
    DWORD size = SizeofResource(self, res);
    if (!data || size == 0) return false;

    HANDLE file = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(file, data, size, &written, nullptr);
    CloseHandle(file);
    return ok && written == size;
}

void DoInstall() {
    if (!EnsureDiscordClosed()) return;

    std::wstring discordFolder = FindDiscordAppFolder();
    if (discordFolder.empty()) {
        Alert(L"Could not find a Discord install under %LOCALAPPDATA%\\Discord.",
              MB_OK | MB_ICONERROR);
        SetStatus(L"Discord install not found.");
        return;
    }

    std::wstring dst = discordFolder + L"\\version.dll";
    if (!ExtractEmbeddedDll(dst)) {
        Alert(L"Failed to write version.dll to:\n" + discordFolder +
              L"\n\nCheck that Discord is fully closed and try again.",
              MB_OK | MB_ICONERROR);
        SetStatus(L"Install failed.");
        return;
    }

    SetStatus(L"Installed to:\r\n" + discordFolder + L"\r\n\r\nStart Discord to activate.");
    Alert(L"Installed successfully to:\n" + discordFolder +
          L"\n\nStart Discord normally to activate it.",
          MB_OK | MB_ICONINFORMATION);
}

void DoUninstall() {
    if (!EnsureDiscordClosed()) return;

    std::wstring discordFolder = FindDiscordAppFolder();
    if (discordFolder.empty()) {
        Alert(L"Could not find a Discord install under %LOCALAPPDATA%\\Discord.",
              MB_OK | MB_ICONERROR);
        return;
    }

    std::wstring dst = discordFolder + L"\\version.dll";
    if (GetFileAttributesW(dst.c_str()) == INVALID_FILE_ATTRIBUTES) {
        SetStatus(L"Nothing to remove.");
        Alert(L"version.dll isn't present there - nothing to remove.", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (!DeleteFileW(dst.c_str())) {
        Alert(L"Failed to remove version.dll. Check that Discord is fully closed and try again.",
              MB_OK | MB_ICONERROR);
        SetStatus(L"Uninstall failed.");
        return;
    }

    SetStatus(L"Removed successfully.");
    Alert(L"Removed successfully.", MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------

void DrawRoundedButton(LPDRAWITEMSTRUCT di, const wchar_t* text, bool filled, bool pressed) {
    HDC hdc = di->hDC;
    RECT r = di->rcItem;

    COLORREF fill = filled ? (pressed ? kAccentDark : kAccent) : kOutlineBg;
    COLORREF border = filled ? fill : kOutlineBrd;
    COLORREF text_color = filled ? RGB(0xFF, 0xFF, 0xFF) : kOutlineText;

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);

    RoundRect(hdc, r.left, r.top, r.right, r.bottom, 8, 8);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text_color);
    HFONT oldFont = (HFONT)SelectObject(hdc, g_fontButton);
    DrawTextW(hdc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

// ---------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_fontTitle = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g_fontSubtitle = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g_fontButton = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g_fontStatus = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        g_bgBrush = CreateSolidBrush(kBg);
        g_panelBrush = CreateSolidBrush(kPanelBg);

        HWND title = CreateWindowW(L"STATIC", L"Discord Direct",
            WS_VISIBLE | WS_CHILD | SS_LEFT, 28, 22, 300, 32, hwnd, (HMENU)ID_TITLE, nullptr, nullptr);
        SendMessageW(title, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

        HWND subtitle = CreateWindowW(L"STATIC", L"Bypass local voice chat restrictions in Discord",
            WS_VISIBLE | WS_CHILD | SS_LEFT, 28, 58, 400, 20, hwnd, (HMENU)ID_SUBTITLE, nullptr, nullptr);
        SendMessageW(subtitle, WM_SETFONT, (WPARAM)g_fontSubtitle, TRUE);

        g_installBtn = CreateWindowW(L"BUTTON", L"Install",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 28, 92, 150, 40, hwnd, (HMENU)ID_INSTALL, nullptr, nullptr);
        g_uninstallBtn = CreateWindowW(L"BUTTON", L"Uninstall",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 190, 92, 150, 40, hwnd, (HMENU)ID_UNINSTALL, nullptr, nullptr);

        g_statusLabel = CreateWindowW(L"STATIC", L"Ready to install.",
            WS_VISIBLE | WS_CHILD | SS_LEFT, 44, 156, 356, 40, hwnd, (HMENU)ID_STATUS, nullptr, nullptr);
        SendMessageW(g_statusLabel, WM_SETFONT, (WPARAM)g_fontStatus, TRUE);

        HWND link = CreateWindowW(L"SysLink", L"<a href=\"1\">View on GitHub</a>",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP, 28, 228, 200, 20, hwnd, (HMENU)ID_GITHUB_LINK, nullptr, nullptr);
        SendMessageW(link, WM_SETFONT, (WPARAM)g_fontSubtitle, TRUE);
        break;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_bgBrush);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT panel = {28, 136, 28 + 372, 136 + 76};
        HBRUSH fill = CreateSolidBrush(kPanelBg);
        HPEN border = CreatePen(PS_SOLID, 1, kPanelBorder);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, fill);
        HPEN oldPen = (HPEN)SelectObject(hdc, border);
        RoundRect(hdc, panel.left, panel.top, panel.right, panel.bottom, 10, 10);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(fill);
        DeleteObject(border);

        EndPaint(hwnd, &ps);
        break;
    }
    case WM_DRAWITEM: {
        auto* di = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        bool pressed = (di->itemState & ODS_SELECTED) != 0;
        if (di->CtlID == ID_INSTALL) {
            DrawRoundedButton(di, L"Install", true, pressed);
            return TRUE;
        } else if (di->CtlID == ID_UNINSTALL) {
            DrawRoundedButton(di, L"Uninstall", false, pressed);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND ctl = (HWND)lParam;
        SetBkMode(hdc, TRANSPARENT);
        if (ctl == g_statusLabel) {
            SetTextColor(hdc, kStatusText);
            return (LRESULT)g_panelBrush;
        }
        int id = GetDlgCtrlID(ctl);
        if (id == ID_TITLE) {
            SetTextColor(hdc, kTitleText);
        } else if (id == ID_SUBTITLE) {
            SetTextColor(hdc, kSubText);
        } else {
            SetTextColor(hdc, kTitleText);
        }
        return (LRESULT)g_bgBrush;
    }
    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<LPNMHDR>(lParam);
        if (hdr->idFrom == ID_GITHUB_LINK && (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
            ShellExecuteW(nullptr, L"open", kGithubUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_INSTALL) DoInstall();
        else if (LOWORD(wParam) == ID_UNINSTALL) DoUninstall();
        break;
    case WM_DESTROY:
        DeleteObject(g_bgBrush);
        DeleteObject(g_panelBrush);
        DeleteObject(g_fontTitle);
        DeleteObject(g_fontSubtitle);
        DeleteObject(g_fontButton);
        DeleteObject(g_fontStatus);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LINK_CLASS };
    InitCommonControlsEx(&icc);

    const wchar_t* className = L"DiscordDirectInstaller";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = className;
    wc.hbrBackground = nullptr; // painted manually (WM_ERASEBKGND)
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    RegisterClassW(&wc);

    RECT wr = {0, 0, 428, 280};
    AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    g_mainWnd = CreateWindowW(className, L"Discord Direct",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                               CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
                               nullptr, nullptr, hInst, nullptr);

    HICON iconBig = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    SendMessageW(g_mainWnd, WM_SETICON, ICON_BIG, (LPARAM)iconBig);
    SendMessageW(g_mainWnd, WM_SETICON, ICON_SMALL, (LPARAM)iconBig);

    ShowWindow(g_mainWnd, nCmdShow);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
