#include "common.h"
#include "state.h"
#include "canvas.h"
#include "widget.h"
#include "settings.h"
#include "tray.h"

using namespace Gdiplus;

static LRESULT CALLBACK MsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_HOTKEY:
            if (wp == HOTKEY_TOGGLE) {
                if (A.active) DeactivateOverlay(); else ActivateOverlay();
            } else if (wp == HOTKEY_UNDO && A.active) {
                Undo();
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
                case ID_TRAY_EXIT:
                    PostQuitMessage(0);
                    break;
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInst,
                      _In_opt_ HINSTANCE,
                      _In_ LPWSTR,
                      _In_ int) {
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"ScreenDoodle_Singleton_4F2A");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mtx) CloseHandle(mtx);
        return 0;
    }

    A.hInst = hInst;

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
                   MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'D');
    RegisterHotKey(A.msgWnd, HOTKEY_UNDO,
                   MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'Z');
    RegisterHotKey(A.msgWnd, HOTKEY_CLEAR,
                   MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'X');

    A.nid.uFlags = NIF_INFO;
    wcscpy_s(A.nid.szInfoTitle, L"ScreenDoodle is running");
    wcscpy_s(A.nid.szInfo, L"Press Ctrl+Shift+D to draw on your screen.");
    A.nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &A.nid);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        if (A.settings && IsDialogMessageW(A.settings, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

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
