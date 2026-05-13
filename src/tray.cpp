#include "tray.h"
#include "state.h"

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
    wcscpy_s(A.nid.szTip, L"ScreenDoodle - Ctrl+Shift+D to draw");
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
    AppendMenuW(m, MF_STRING, ID_TRAY_TOGGLE,
                A.active ? L"Stop drawing" : L"Start drawing\tCtrl+Shift+D");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_TRAY_SETTINGS, L"Settings…");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, L"Quit ScreenDoodle");
    SetForegroundWindow(A.msgWnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, A.msgWnd, nullptr);
    DestroyMenu(m);
}
