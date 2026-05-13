#pragma once

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <objidl.h>
#include <sal.h>

#ifndef _In_
#define _In_
#endif
#ifndef _In_opt_
#define _In_opt_
#endif

#include <gdiplus.h>
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <new>

#include "resource.h"

template <class A, class B>
static inline auto imin(A a, B b) -> typename std::common_type<A, B>::type {
    using T = typename std::common_type<A, B>::type;
    return (T)a < (T)b ? (T)a : (T)b;
}
template <class A, class B>
static inline auto imax(A a, B b) -> typename std::common_type<A, B>::type {
    using T = typename std::common_type<A, B>::type;
    return (T)a > (T)b ? (T)a : (T)b;
}
#define min imin
#define max imax

inline constexpr int    HOTKEY_TOGGLE    = 1;
inline constexpr int    HOTKEY_UNDO      = 2;
inline constexpr int    HOTKEY_CLEAR     = 3;
inline constexpr UINT   WM_TRAYICON      = WM_USER + 1;
inline constexpr int    ID_TRAY_TOGGLE   = 1001;
inline constexpr int    ID_TRAY_EXIT     = 1002;
inline constexpr int    ID_TRAY_SETTINGS = 1003;

inline constexpr size_t kHistoryByteBudget = 12 * 1024 * 1024;
