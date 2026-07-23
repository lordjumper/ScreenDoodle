#include "common.h"
#include "state.h"
#include "canvas.h"
#include "widget.h"
#include "settings.h"
#include "tray.h"
#include "updater.h"
#include "config.h"

using namespace Gdiplus;

static LRESULT CALLBACK MsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_HOTKEY:
            if (wp == HOTKEY_TOGGLE) {
                if (A.active) DeactivateOverlay(); else ActivateOverlay();
            } else if (wp == HOTKEY_UNDO && A.active) {
                if (!A.text.active) Undo();
            } else if (wp == HOTKEY_CLEAR && A.active) {
                ClearCanvas();
            }
            return 0;
        case WM_TRAYICON:
            if (LOWORD(lp) == WM_LBUTTONUP) {
                if (A.active) DeactivateOverlay(); else ActivateOverlay();
            } else if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
                ShowTrayMenu();
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case ID_TRAY_TOGGLE:
                    if (A.active) DeactivateOverlay(); else ActivateOverlay();
                    break;
                case ID_TRAY_SETTINGS:
                    OpenSettingsWindow();
                    break;
                case ID_TRAY_LAUNCHER:
                case ID_TRAY_LAUNCHER_SET:
                    OpenLauncher(LOWORD(wp) == ID_TRAY_LAUNCHER_SET);
                    break;
                case ID_TRAY_CHECK_UPDATE:
                    StartUpdateCheck(true);
                    break;
                case ID_TRAY_UPDATE:
                    if (HasInstallerUrl()) {
                        StartUpdateDownload();
                    } else {
                        ShellExecuteW(nullptr, L"open", ReleasesPageUrl(),
                                      nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    break;
                case ID_TRAY_EXIT:
                    PostQuitMessage(0);
                    break;
            }
            return 0;
        case WM_UPDATE_RESULT: {
            if (wp == kUpdateAvailable) {
                A.nid.uFlags = NIF_INFO;
                wcscpy_s(A.nid.szInfoTitle, L"ScreenDoodle update available");
                wchar_t msg[200];
                _snwprintf_s(msg, ARRAYSIZE(msg), _TRUNCATE,
                    L"Version %s is out. Right-click the tray icon to install.",
                    LatestVersionTag());
                wcscpy_s(A.nid.szInfo, msg);
                A.nid.dwInfoFlags = NIIF_INFO;
                Shell_NotifyIconW(NIM_MODIFY, &A.nid);
            } else if (wp == kUpdateDownloaded) {
                ShellExecuteW(nullptr, L"open",
                              DownloadedInstallerPath(),
                              nullptr, nullptr, SW_SHOWNORMAL);
                PostQuitMessage(0);
            } else if (wp == kUpdateNone && IsManualCheck()) {
                A.nid.uFlags = NIF_INFO;
                wcscpy_s(A.nid.szInfoTitle, L"ScreenDoodle is up to date");
                wchar_t msg[120];
                _snwprintf_s(msg, ARRAYSIZE(msg), _TRUNCATE,
                    L"You're running the latest version (%s).", kAppVersion);
                wcscpy_s(A.nid.szInfo, msg);
                A.nid.dwInfoFlags = NIIF_INFO;
                Shell_NotifyIconW(NIM_MODIFY, &A.nid);
            } else if (wp == kUpdateFailed && IsManualCheck()) {
                A.nid.uFlags = NIF_INFO;
                wcscpy_s(A.nid.szInfoTitle, L"ScreenDoodle update check failed");
                wcscpy_s(A.nid.szInfo,
                    L"Couldn't reach GitHub. Check your connection and try again.");
                A.nid.dwInfoFlags = NIIF_WARNING;
                Shell_NotifyIconW(NIM_MODIFY, &A.nid);
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool DeleteTreeOnce(const std::wstring& dir) {
    std::wstring from = dir;
    from.push_back(L'\0');
    from.push_back(L'\0');

    SHFILEOPSTRUCTW op{};
    op.wFunc  = FO_DELETE;
    op.pFrom  = from.c_str();
    op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    SHFileOperationW(&op);
    return GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES;
}

static int RunCleanup(const std::wstring& dir) {
    if (dir.empty()) return 1;
    for (int i = 0; i < 60; ++i) {
        Sleep(500);
        if (DeleteTreeOnce(dir)) break;
    }
    wchar_t self[MAX_PATH];
    if (GetModuleFileNameW(nullptr, self, MAX_PATH))
        MoveFileExW(self, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return 0;
}

static std::wstring CleanupTarget() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring target;
    if (argv) {
        for (int i = 1; i + 1 < argc; ++i) {
            if (wcscmp(argv[i], L"--cleanup") == 0) { target = argv[i + 1]; break; }
        }
        LocalFree(argv);
    }
    return target;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInst,
                      _In_opt_ HINSTANCE,
                      _In_ LPWSTR,
                      _In_ int) {
    {
        std::wstring cleanup = CleanupTarget();
        if (!cleanup.empty()) return RunCleanup(cleanup);
    }

    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"ScreenDoodle_Singleton_4F2A");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mtx) CloseHandle(mtx);
        return 0;
    }

    A.hInst = hInst;
    LoadConfig();

    {
        bool dpiSet = false;
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        if (u32) {
            typedef BOOL (WINAPI *PFnSetCtx)(void*);
            auto setCtx = (PFnSetCtx)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
            if (setCtx) {
                dpiSet = (setCtx((void*)-4) != 0);
            }
        }
        if (!dpiSet) SetProcessDPIAware();
    }

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    GdiplusStartupInput gsi;
    GdiplusStartup(&A.gdiToken, &gsi, nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MsgProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"ScreenDoodle_MsgWnd";
    RegisterClassExW(&wc);

    wc.lpfnWndProc = CanvasProc;
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wc.lpszClassName = L"ScreenDoodle_Canvas";
    RegisterClassExW(&wc);

    wc.lpfnWndProc = WidgetProc;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"ScreenDoodle_Widget";
    RegisterClassExW(&wc);

    wc.lpfnWndProc   = SettingsProc;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon         = LoadAppIcon(GetSystemMetrics(SM_CXICON));
    wc.hIconSm       = LoadAppIcon(GetSystemMetrics(SM_CXSMICON));
    wc.lpszClassName = L"ScreenDoodle_Settings";
    RegisterClassExW(&wc);

    A.msgWnd = CreateWindowExW(0, L"ScreenDoodle_MsgWnd", L"ScreenDoodle",
                               0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr, hInst, nullptr);

    A.widget = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"ScreenDoodle_Widget", L"ScreenDoodle Tools",
        WS_POPUP,
        0, 0, 72, 480,
        nullptr, nullptr, hInst, nullptr);

    CreateTrayIcon();

    RegisterHotKey(A.msgWnd, HOTKEY_TOGGLE,
                   C.toggle.mods | MOD_NOREPEAT, C.toggle.vk);
    RegisterHotKey(A.msgWnd, HOTKEY_UNDO,
                   C.undo.mods | MOD_NOREPEAT, C.undo.vk);
    RegisterHotKey(A.msgWnd, HOTKEY_CLEAR,
                   C.clear.mods | MOD_NOREPEAT, C.clear.vk);

    wchar_t tip[128];
    _snwprintf_s(tip, ARRAYSIZE(tip), _TRUNCATE,
                 L"Press %s to draw on your screen.",
                 DescribeHotkey(C.toggle).c_str());

    A.nid.uFlags = NIF_INFO;
    wcscpy_s(A.nid.szInfoTitle, L"ScreenDoodle is running");
    wcscpy_s(A.nid.szInfo, tip);
    A.nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &A.nid);
    if (!HasLauncher()) StartUpdateCheck(false);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        if (A.settings && IsDialogMessageW(A.settings, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    SaveConfig();

    UnregisterHotKey(A.msgWnd, HOTKEY_TOGGLE);
    UnregisterHotKey(A.msgWnd, HOTKEY_UNDO);
    UnregisterHotKey(A.msgWnd, HOTKEY_CLEAR);
    RemoveTrayIcon();
    if (A.canvas) DestroyWindow(A.canvas);
    if (A.widget) DestroyWindow(A.widget);
    DestroyCanvasSurface();

    GdiplusShutdown(A.gdiToken);
    if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); }
    return (int)m.wParam;
}
