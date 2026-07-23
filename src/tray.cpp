#include "tray.h"
#include "state.h"
#include "updater.h"
#include "config.h"

HICON LoadAppIcon(int size) {
    HICON h = (HICON)LoadImageW(A.hInst, MAKEINTRESOURCEW(IDI_APP),
                                IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
    if (!h) {
        h = LoadIconW(A.hInst, MAKEINTRESOURCEW(IDI_APP));
    }
    return h;
}

void CreateTrayIcon() {
    A.nid.cbSize = sizeof(A.nid);
    A.nid.hWnd = A.msgWnd;
    A.nid.uID = 1;
    A.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    A.nid.uCallbackMessage = WM_TRAYICON;
    A.nid.hIcon = LoadAppIcon(GetSystemMetrics(SM_CXSMICON));
    wchar_t tip[128];
    _snwprintf_s(tip, ARRAYSIZE(tip), _TRUNCATE, L"ScreenDoodle - %s to draw",
                 DescribeHotkey(C.toggle).c_str());
    wcscpy_s(A.nid.szTip, tip);
    Shell_NotifyIconW(NIM_ADD, &A.nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &A.nid);
    if (A.nid.hIcon) { DestroyIcon(A.nid.hIcon); A.nid.hIcon = nullptr; }
}

void ShowTrayMenu() {
    POINT pt;
    GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    const bool launcher = HasLauncher();
    if (A.active) {
        AppendMenuW(m, MF_STRING, ID_TRAY_TOGGLE, L"Stop drawing");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    } else if (!launcher) {
        wchar_t startLabel[96];
        _snwprintf_s(startLabel, ARRAYSIZE(startLabel), _TRUNCATE,
                     L"Start drawing\t%s", DescribeHotkey(C.toggle).c_str());
        AppendMenuW(m, MF_STRING, ID_TRAY_TOGGLE, startLabel);
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    }
    if (launcher) {
        AppendMenuW(m, MF_STRING, ID_TRAY_LAUNCHER,     L"Open Launcher");
        AppendMenuW(m, MF_STRING, ID_TRAY_LAUNCHER_SET, L"Settings…");
    } else {
        AppendMenuW(m, MF_STRING, ID_TRAY_SETTINGS, L"Settings…");
        if (IsUpdateAvailable()) {
            wchar_t label[128];
            _snwprintf_s(label, ARRAYSIZE(label), _TRUNCATE,
                         L"Install update %s", LatestVersionTag());
            AppendMenuW(m, MF_STRING, ID_TRAY_UPDATE, label);
        } else {
            AppendMenuW(m, MF_STRING, ID_TRAY_CHECK_UPDATE,
                        L"Check for updates");
        }
    }

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, L"Quit ScreenDoodle");
    SetForegroundWindow(A.msgWnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, A.msgWnd, nullptr);
    DestroyMenu(m);
}
