#pragma once

#include "common.h"

enum class Tool { Pen, Pencil, Highlighter, Eraser };
enum class SideAnchor { Left, Right };

struct Swatch { BYTE r, g, b; };

inline constexpr Swatch kPalette[] = {
    {235,  60,  60},
    {245, 140,  40},
    {250, 205,  60},
    { 90, 200, 110},
    { 60, 160, 230},
    {145,  95, 215},
    {235, 105, 165},
    {  0,   0,   0},
    {245, 245, 245},
};
inline constexpr int kPaletteCount =
    (int)(sizeof(kPalette) / sizeof(kPalette[0]));

inline constexpr int kThickness[] = {2, 4, 7, 12, 18};
inline constexpr int kThicknessCount =
    (int)(sizeof(kThickness) / sizeof(kThickness[0]));

struct AppState {
    Tool tool = Tool::Pen;
    int  paletteIdx = 0;
    int  thicknessIdx = 1;

    bool active = false;
    bool drawing = false;
    SideAnchor anchor = SideAnchor::Right;

    HINSTANCE hInst = nullptr;
    HWND msgWnd   = nullptr;
    HWND canvas   = nullptr;
    HWND widget   = nullptr;
    HWND settings = nullptr;

    int sx = 0, sy = 0, sw = 0, sh = 0;

    HDC       drawDC     = nullptr;
    HBITMAP   drawBmp    = nullptr;
    HGDIOBJ   drawOldBmp = nullptr;
    uint32_t* drawBits   = nullptr;

    uint32_t* savedBits = nullptr;
    uint8_t*  strokeCov = nullptr;

    POINT lastPt{0, 0};
    RECT  strokeDirty{0, 0, 0, 0};

    struct UndoEntry {
        RECT rect;
        std::vector<uint32_t> pixels;
    };
    std::vector<UndoEntry> history;
    static constexpr size_t kMaxHistory = 24;

    NOTIFYICONDATAW nid{};
    ULONG_PTR gdiToken = 0;

    int wgW = 0, wgH = 0;
};

extern AppState A;

inline int currentThickness() { return kThickness[A.thicknessIdx]; }
