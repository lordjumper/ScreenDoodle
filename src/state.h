#pragma once

#include "common.h"

enum class Tool { Pen, Pencil, Highlighter, Eraser, Text };
inline constexpr int kToolCount = 5;
inline bool IsDrawTool(Tool t) {
    return t == Tool::Pen || t == Tool::Pencil || t == Tool::Highlighter;
}

enum class SideAnchor { Left, Right };

struct Swatch { BYTE r, g, b; };

inline constexpr int kThickness[] = {2, 4, 7, 12, 18};
inline constexpr int kThicknessCount =
    (int)(sizeof(kThickness) / sizeof(kThickness[0]));

inline constexpr int kTextSize[]  = {16, 22, 30, 42, 58};

// HSV <-> RGB
inline Swatch HSVtoRGB(float h, float s, float v) {
    h = fmodf(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r, g, b;
    if      (h <  60.0f) { r = c; g = x; b = 0; }
    else if (h < 120.0f) { r = x; g = c; b = 0; }
    else if (h < 180.0f) { r = 0; g = c; b = x; }
    else if (h < 240.0f) { r = 0; g = x; b = c; }
    else if (h < 300.0f) { r = x; g = 0; b = c; }
    else                 { r = c; g = 0; b = x; }
    return Swatch{
        (BYTE)lroundf((r + m) * 255.0f),
        (BYTE)lroundf((g + m) * 255.0f),
        (BYTE)lroundf((b + m) * 255.0f)
    };
}

inline void RGBtoHSV(Swatch col, float& h, float& s, float& v) {
    float r = col.r / 255.0f, g = col.g / 255.0f, b = col.b / 255.0f;
    float mx = max(max(r, g), b);
    float mn = min(min(r, g), b);
    float d = mx - mn;
    v = mx;
    s = (mx <= 0.0f) ? 0.0f : d / mx;
    if (d <= 0.0f)        h = 0.0f;
    else if (mx == r)     h = 60.0f * fmodf((g - b) / d, 6.0f);
    else if (mx == g)     h = 60.0f * ((b - r) / d + 2.0f);
    else                  h = 60.0f * ((r - g) / d + 4.0f);
    if (h < 0.0f) h += 360.0f;
}

struct TextEditState {
    bool         active = false;
    bool         dragging = false;
    POINT        origin{0, 0};
    POINT        dragOffset{0, 0};
    std::wstring buffer;
    RECT         lastRect{0, 0, 0, 0};
    bool         caretVisible = true;
    UINT_PTR     caretTimer = 0;
};

struct PlacedTextBox {
    std::wstring          text;
    POINT                 origin{0, 0};
    int                   sizeIdx = 0;
    Swatch                color{235, 60, 60};
    RECT                  bounds{0, 0, 0, 0};
    std::vector<uint32_t> backdrop;
    RECT                  backdropRect{0, 0, 0, 0};
};

struct AppState {
    Tool tool = Tool::Pen;
    Tool drawTool = Tool::Pen;     // last pen/pencil/highlighter, for the collapsed group button
    bool toolGroupOpen = false;    // draw-tool group expanded?
    int  thicknessIdx = 1;

    // Current drawing colour plus its HSV decomposition (drives the wheel).
    Swatch color{235, 60, 60};
    float  hue = 0.0f, sat = 0.745f, val = 0.922f;

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
    float lastMidX = 0.0f, lastMidY = 0.0f;
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

    TextEditState text;
    std::vector<PlacedTextBox> textBoxes;
    static constexpr size_t kMaxTextBoxes = 32;
};

extern AppState A;

inline int currentThickness() { return kThickness[A.thicknessIdx]; }
inline int currentTextSize()  { return kTextSize[A.thicknessIdx]; }

inline void SetPickerHSV(float h, float s, float v) {
    A.hue = h; A.sat = s; A.val = v;
    A.color = HSVtoRGB(h, s, v);
}
inline void SetPickerRGB(Swatch c) {
    A.color = c;
    RGBtoHSV(c, A.hue, A.sat, A.val);
}
