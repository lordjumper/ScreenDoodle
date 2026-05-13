#include "settings.h"
#include "state.h"
#include "tray.h"

static const wchar_t kRunKey[]   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t kRunValue[] = L"ScreenDoodle";

bool IsAutoStartEnabled() {
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS)
        return false;
    DWORD type = 0;
    LONG  r    = RegQueryValueExW(hk, kRunValue, nullptr, &type, nullptr, nullptr);
    RegCloseKey(hk);
    return r == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ);
}

bool SetAutoStart(bool enabled) {
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return false;
    LONG r = ERROR_SUCCESS;
    if (enabled) {
        WCHAR path[MAX_PATH] = {};
        DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            RegCloseKey(hk);
            return false;
        }
        WCHAR quoted[MAX_PATH + 4];
        int n = _snwprintf_s(quoted, ARRAYSIZE(quoted), _TRUNCATE, L"\"%s\"", path);
        if (n < 0) {
            RegCloseKey(hk);
            return false;
        }
        r = RegSetValueExW(hk, kRunValue, 0, REG_SZ, (const BYTE*)quoted,
                           (DWORD)((wcslen(quoted) + 1) * sizeof(WCHAR)));
    } else {
        r = RegDeleteValueW(hk, kRunValue);
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }
    RegCloseKey(hk);
    return r == ERROR_SUCCESS;
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

            HWND cb = CreateWindowExW(0, L"BUTTON",
                L"Launch ScreenDoodle on Windows startup",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                24, 28, 360, 22,
                hwnd, (HMENU)(INT_PTR)IDC_AUTOSTART, A.hInst, nullptr);
            SendMessageW(cb, WM_SETFONT, (WPARAM)font, TRUE);
            if (IsAutoStartEnabled())
                SendMessageW(cb, BM_SETCHECK, BST_CHECKED, 0);

            HWND info = CreateWindowExW(0, L"STATIC",
                L"Hotkeys:\n"
                L"  Ctrl+Shift+D  —  Toggle drawing mode\n"
                L"  Ctrl+Shift+Z  —  Undo last stroke\n"
                L"  Ctrl+Shift+X  —  Clear canvas\n"
                L"  Esc           —  Exit drawing mode",
                WS_CHILD | WS_VISIBLE,
                24, 66, 380, 92,
                hwnd, (HMENU)(INT_PTR)IDC_HOTKEYINFO, A.hInst, nullptr);
            SendMessageW(info, WM_SETFONT, (WPARAM)font, TRUE);

            HWND done = CreateWindowExW(0, L"BUTTON", L"Done",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                300, 170, 96, 28,
                hwnd, (HMENU)(INT_PTR)IDOK, A.hInst, nullptr);
            SendMessageW(done, WM_SETFONT, (WPARAM)font, TRUE);

            HICON small_ = LoadAppIcon(GetSystemMetrics(SM_CXSMICON));
            HICON big_   = LoadAppIcon(GetSystemMetrics(SM_CXICON));
            if (small_) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small_);
            if (big_)   SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)big_);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_AUTOSTART: {
                    HWND cb = (HWND)lp;
                    bool checked = (SendMessageW(cb, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    if (!SetAutoStart(checked)) {
                        SendMessageW(cb, BM_SETCHECK,
                                     checked ? BST_UNCHECKED : BST_CHECKED, 0);
                        MessageBoxW(hwnd,
                            L"Couldn't update the startup entry. "
                            L"Check Windows permissions and try again.",
                            L"ScreenDoodle",
                            MB_OK | MB_ICONWARNING);
                    }
                    return 0;
                }
                case IDOK:
                case IDCANCEL:
                    DestroyWindow(hwnd);
                    return 0;
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: {
            HICON s = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
            HICON b = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_BIG,   0);
            if (s) DestroyIcon(s);
            if (b) DestroyIcon(b);
            A.settings = nullptr;
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void OpenSettingsWindow() {
    if (A.settings) {
        if (IsIconic(A.settings)) ShowWindow(A.settings, SW_RESTORE);
        SetForegroundWindow(A.settings);
        return;
    }

    RECT work{0, 0, 800, 600};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int W = 420, H = 230;
    int x = work.left + ((work.right  - work.left) - W) / 2;
    int y = work.top  + ((work.bottom - work.top)  - H) / 2;
    A.settings = CreateWindowExW(0, L"ScreenDoodle_Settings",
        L"ScreenDoodle Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, W, H, nullptr, nullptr, A.hInst, nullptr);
    if (A.settings) {
        ShowWindow(A.settings, SW_SHOW);
        SetForegroundWindow(A.settings);
    }
}
